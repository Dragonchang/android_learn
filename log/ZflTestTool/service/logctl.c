#define	LOG_TAG		"STT:logctl"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include "common.h"
#include "server.h"

#include "headers/fio.h"
#include "headers/sem.h"
#include "headers/poll.h"
#include "headers/glist.h"
#include "headers/process.h"
#include "headers/partition.h"
#include "client/client.h"

/*
 * include the static expiring checking function
 */
#include "src/expire.c"

#define	VERSION	"9.1"
/*
 * 9.1	: Set default LogPath to internal and copy default log config files from system/etc/ghost/ to /data/ghost/.
 * 9.0	: Do not set thread id to -1 inside thread to prevent timing issue.
 * 8.9	: Add debug log on stt.txt to check move log function behavior.
 * 8.8	: Add debug log to check why failed to query log path.
 * 8.7	: Change checking storage path order to avoid getting old storage path on M_REL.
 * 8.6	: On M_REL, don't cache phone and sdcard mount paths since it may cache symbolic link that cannot be found on mount table.
 * 8.5	: for log overload criteria equals to 0, no need to check log overload.
 * 8.4	: monitor some runtime values for debugging.
 * 8.3	: support logger extra configs and add logmeminfo.
 * 8.2	: calculate old log file remove count by logger weight.
 * 8.1	: 1. remove log files in low free space condition. 2. force checking excluding status on logging storage.
 * 8.0	: implement new designs:
 *	1. support logger services that export the same interface.
 *	2. control log file rotation.
 *	3. individual settings for loggers.
 *	4. individual settings for storages.
 *	5. require v3 config, but support v2 config as the global setting.
 *	6. remove uevent checking.
 *	7. do not allow customized logging path.
 *	8. unify session number across storages.
 *	9. add rotation serial number in file name.
 * 7.3	: add a local state for status changing.
 * 7.2	: check log file missing state.
 * 7.1	: add a killer thread to remove files older than 14 days in log folders.
 * 7.0	: add a watchdog to monitor logger status.
 * 6.9	: check loggers state and do more debug output.
 * 6.8	: stop loggers in reverse order.
 * 6.7	: clear whole log dir.
 * 6.6	: support usb storage.
 * 6.5	: retry statfs() when errno is EINTR or ENOMEM.
 * 6.4	: 1. unify sessions between loggers. 2. keep extra info in log file name.
 * 6.3	: support percentage reserved storage size.
 * 6.2	: 1. control loggers directly without socket operations. 2. exit daemon on SIGTERM.
 * 6.1	: 1. prevent uevent blocking logger status checking process. 2. force restarting loggers when a write error detected.
 * 6.0	: add dumplastkmsg to default loggers.
 * 5.9	: check expiring status about one day.
 * 5.8	: add auto clear setting back and reserve for compression setting, increase config version to 2.
 * 5.7	: 1. add storage code to identify the log path. 2. change default rotate size and rotate count.
 * 5.6	: move debug flag checking to main thread to prevent a timing issue.
 * 5.5	: check debug flag 5 1 to force auto start.
 * 5.4	: make default log path be auto.
 * 5.3	: update size_reserved when loading config file.
 * 5.2	: auto clear on the same session id (sn).
 * 5.1	: fix a buffer overwritten bug.
 * 5.0	: support sn.
 * 4.9	: 1. make the safe threshold values able to be changed. 2. add config version.
 * 4.8	: support :getphonestorage: command.
 * 4.7	: enlarge the safe free size threshold (110/100).
 * 4.6	: add :syncconf: command to force reading settings from config file.
 * 4.5	: in auto mode always save to internal storage when un-mounted.
 * 4.4	: keep local logs in LOG_DIR.
 * 4.3	: support auto select log path.
 * 4.2	: 1. return log paths in :run: command.
 *	  2. add :getlogfiles: command.
 * 4.1	: check external storage state before use it.
 * 4.0	: support getextstorage and statfs command.
 * 3.9	: support specific external storage.
 * 3.8	: correctly checking systemlogers.path.
 * 3.7	: daemon may crash in atoi() when db was not initialized and loggers were enabled by SQA's precondition tool.
 * 3.6	: 1. add .nomedia to the log folder.
 *	  2. if there is a path setting in conf file, just use the given path.
 * 3.5	: try to detect data file corruption.
 * 3.4	: fix an infinite loop bug.
 * 3.3	: disable auto choosing internal storage to prevent a media unmount bug.
 * 3.2	: add max session threshold to 5.
 * 3.1	: fix a bug of MAX_SESSION.
 * 3.0	: limit under MAX_SESSION.
 * 2.9	: fix a session_count bug that may crash when the value is 0.
 * 2.8	: support size limitation.
 * 2.7	: 1. use config file instead of .system.loggers.
 * 	  2. load loggers settings only in this service.
 * 	  3. add overall session/size limitation.
 * 	  4. enlarge auto clear threshold because other debug mechanisms would also consume the internal storage.
 * 2.6	: 1. add dumpsys command.
 * 	  2. return file path of dumpstate and dumpsys.
 * 2.5	: 1. queue all timestamps when clearing.
 * 	  2. change first time delay to 1 sec.
 * 2.4	: 1. do size checking in 2 seconds after main thread started.
 * 	  2. keep removing old log files until we got enough free space in auto clear thread.
 * 2.3	: get external storage path from environment variable.
 * 2.2	: suppress logs.
 * 2.1	: 1. support setting auto clear.
 * 	  2. support :help: command.
 * 2.0	: remove some codes.
 * 1.9	: handle the fail case of poll_wait().
 * 1.8	: add :bugreport: and :getdebug: and :setdebug: commands.
 * 1.7	: change compression function to auto clear function.
 * 1.6	: supports compression.
 * 1.5	: fix fs size overflow problem.
 * 1.4	: 1. add a command to check if specified path is writable.
 * 	  2. add a command to clear logs.
 * 1.3	: check more keywords of uevent.
 * 1.2	: support TI platform and fix some bugs.
 * 1.1	: monitor free size, auto start/stop on sdcard changed.
 * 1.0	: initial.
 */

#define	CONF_VERSION		"3"
#define	USE_LOCAL_SOCKET	(0)
#define	SIZE_CLEAR_BEFORE_STOP	(5)
#define	MB			(1024 * 1024)

#define	LOCK(x)		{ pthread_mutex_lock (x); /*DM ("MUTEX " #x " in %s LOCK (%d:%s)\n", __FUNCTION__, __LINE__, __FILE__);*/ }
#define	UNLOCK(x)	{ pthread_mutex_unlock (x); /*DM ("MUTEX " #x " in %s UNLOCK (%d:%s)\n", __FUNCTION__, __LINE__, __FILE__);*/ }

static const char *LOGGER_CONFIG_FILENAME = GHOST_DIR "SystemLoggers.conf";

/*
 * define the keys to unify wordings
 */
#define	KEY_LIMITED_TOTAL_LOG_SIZE_MB	"LimitedTotalLogSizeMB"
#define	KEY_RESERVED_STORAGE_SIZE_MB	"ReservedStorageSizeMB"
#define	KEY_ROTATE_SIZE_MB		"RotateSizeMB"
#define	KEY_PRIORITY			"Priority"
#define	KEY_COMPRESS			"Compress"
#define	KEY_ENABLE			"Enable"
#define	KEY_WEIGHT			"Weight"
#define	KEY_OVERLOAD			"Overload"

/*
 * v3 global keywords
 */
DECLARE_CONF_PAIR_STATIC (Version,			CONF_VERSION);
DECLARE_CONF_PAIR_STATIC (AutoStart,			"false");
DECLARE_CONF_PAIR_STATIC (LogPath,			STORAGE_KEY_INTERNAL);
/* v3 global storage keywords */
DECLARE_CONF_PAIR_STATIC (LimitedTotalLogSizeMB,	STORAGE_KEY_AUTO);
DECLARE_CONF_PAIR_STATIC (ReservedStorageSizeMB,	STORAGE_KEY_AUTO);
/* v3 global logger keywords */
DECLARE_CONF_PAIR_STATIC (RotateSizeMB,			STORAGE_KEY_AUTO);
DECLARE_CONF_PAIR_STATIC (Compress,			LOG_DEFAULT_COMPRESS);

enum {
	idx_Version = 0,
	idx_AutoStart,
	idx_LogPath,
	idx_LimitedTotalLogSizeMB,
	idx_ReservedStorageSizeMB,
	idx_RotateSizeMB,
	idx_Compress,
	KEYWORD_COUNT
};

static CONF_PAIR *keyword_pairs [KEYWORD_COUNT] = {
	[idx_Version]			= & pair_Version,
	[idx_AutoStart]			= & pair_AutoStart,
	[idx_LogPath]			= & pair_LogPath,
	[idx_LimitedTotalLogSizeMB]	= & pair_LimitedTotalLogSizeMB,
	[idx_ReservedStorageSizeMB]	= & pair_ReservedStorageSizeMB,
	[idx_RotateSizeMB]		= & pair_RotateSizeMB,
	[idx_Compress]			= & pair_Compress,
};

#define	STORAGE_EXCLUDED_MASK_USER_SETTING	0x01
#define	STORAGE_EXCLUDED_MASK_UNMOUNT		0x02
#define	STORAGE_EXCLUDED_MASK_READONLY		0x04
#define	STORAGE_EXCLUDED_MASK_NO_FREE_SPACE	0x08

typedef struct {
	const char	*name;
	const char	*conf_key_limited_size_mb;
	const char	*conf_key_reserved_size_mb;
	const char	*conf_key_priority;
	const char	*mountpoint;
	int		priority;
	unsigned long	excluded;
	unsigned long	real_limited_size_mb;
	unsigned long	real_reserved_size_mb;
	unsigned long	limited_size_mb;
	unsigned long	reserved_size_mb;
	unsigned long	free_size_mb;
	unsigned long	total_size_mb;
	const char *	(*getmountpoint)	();
} STORAGE;

#define	STORAGE_ENTRY(__name,__mountpoint,__priority,__getmountpoint)	\
	{__name,\
	STORAGE_CONFIG_KEY (__name, KEY_LIMITED_TOTAL_LOG_SIZE_MB),\
	STORAGE_CONFIG_KEY (__name, KEY_RESERVED_STORAGE_SIZE_MB),\
	STORAGE_CONFIG_KEY (__name, KEY_PRIORITY),\
	__mountpoint, __priority, 0, 0, 0, 0, 0, 0, 0, __getmountpoint}

typedef struct {
	const char		*name;
	const char		*conf_key_limited_size_mb;
	const char		*conf_key_rotate_size_mb;
	const char		*conf_key_compress;
	const char		*conf_key_enable; // private config key
	const char		*conf_key_weight; // private config key
	const char		*conf_key_overload; // private config key
	LOGGER_EXTRA_CONFIG	*conf_extras;
	int			conf_extras_count;
	int			enabled;
	float			weight;
	int			overload;
	int			serialno;
	unsigned long		state;
	unsigned long		real_limited_size_mb;
	unsigned long		limited_size_mb;
	unsigned long		rotate_size_mb;
	int			(*log_init)		(const char *, LOGGER_EXTRA_CONFIG **extra_configs, int *extra_config_count); // init logger
	int			(*log_start)		(const char *); // start logger
	int			(*log_stop)		(const char *); // stop logger
	unsigned long long	(*log_get_total_size)	(const char *); // get log total size
	int			(*log_update_state)	(const char *, int);
	int			(*log_rotate_and_limit)	(const char *, int, unsigned long, unsigned long, unsigned long); // check rotate and size limitation
	void			(*log_get_log_filepath)	(const char *, char *, int); // get all logging file paths separated by newline character
	void			(*log_clear_log_files)	(const char *);
	int			(*log_check_file_type)	(const char *, const char *);
	int			(*log_watchdog)		(const char *);
} LOGGER;

#define	LOGGER_ENTRY(__name,__enabled,__weight,__overload,__init,__start,__stop,__gettotalsize,__state,__rotate,__getfile,__clearlog,__checkfiletype,__watchdog) \
	{__name,\
	LOGGER_CONFIG_KEY (__name, KEY_LIMITED_TOTAL_LOG_SIZE_MB),\
	LOGGER_CONFIG_KEY (__name, KEY_ROTATE_SIZE_MB),\
	LOGGER_CONFIG_KEY (__name, KEY_COMPRESS),\
	LOGGER_CONFIG_KEY (__name, KEY_ENABLE),\
	LOGGER_CONFIG_KEY (__name, KEY_WEIGHT),\
	LOGGER_CONFIG_KEY (__name, KEY_OVERLOAD),\
	NULL, 0,\
	__enabled,__weight, __overload, 0, 0, 0, 0, 0, __init, __start, __stop, __gettotalsize, __state, __rotate, __getfile, __clearlog, __checkfiletype, __watchdog}

extern int logdevice_init (const char *, LOGGER_EXTRA_CONFIG **, int *);
extern int logradio_init (const char *, LOGGER_EXTRA_CONFIG **, int *);
extern int logevents_init (const char *, LOGGER_EXTRA_CONFIG **, int *);
extern int logmeminfo_init (const char *, LOGGER_EXTRA_CONFIG **, int *);

extern int logdevice_start (const char *);
extern int logradio_start (const char *);
extern int logevents_start (const char *);
extern int logkmsg_start (const char *);
extern int logmeminfo_start (const char *);
extern int dumplastkmsg_start (const char *);

extern int logdevice_stop (const char *);
extern int logradio_stop (const char *);
extern int logevents_stop (const char *);
extern int logkmsg_stop (const char *);
extern int logmeminfo_stop (const char *);
extern int dumplastkmsg_stop (const char *);

extern int logdevice_update_state (const char *, int);
extern int logradio_update_state (const char *, int);
extern int logevents_update_state (const char *, int);
extern int logkmsg_update_state (const char *, int);
extern int logmeminfo_update_state (const char *, int);
extern int dumplastkmsg_update_state (const char *, int);

extern unsigned long long logdevice_get_total_size (const char *);
extern unsigned long long logradio_get_total_size (const char *);
extern unsigned long long logevents_get_total_size (const char *);
extern unsigned long long logkmsg_get_total_size (const char *);
extern unsigned long long logmeminfo_get_total_size (const char *);
extern unsigned long long dumplastkmsg_get_total_size (const char *);

extern int logdevice_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);
extern int logradio_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);
extern int logevents_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);
extern int logkmsg_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);
extern int logmeminfo_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);
extern int dumplastkmsg_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);

extern void logdevice_get_log_filepath (const char *, char *, int);
extern void logradio_get_log_filepath (const char *, char *, int);
extern void logevents_get_log_filepath (const char *, char *, int);
extern void logkmsg_get_log_filepath (const char *, char *, int);
extern void logmeminfo_get_log_filepath (const char *, char *, int);
extern void dumplastkmsg_get_log_filepath (const char *, char *, int);

extern void logdevice_clear_log_files (const char *);
extern void logkmsg_clear_log_files (const char *);
extern void logevents_clear_log_files (const char *);
extern void logradio_clear_log_files (const char *);
extern void logmeminfo_clear_log_files (const char *);
extern void dumplastkmsg_clear_log_files (const char *);

extern int logdevice_check_file_type (const char *, const char *);
extern int logkmsg_check_file_type (const char *, const char *);
extern int logevents_check_file_type (const char *, const char *);
extern int logradio_check_file_type (const char *, const char *);
extern int logmeminfo_check_file_type (const char *, const char *);
extern int dumplastkmsg_check_file_type (const char *, const char *);

extern int logkmsg_watchdog (const char *);
extern int logkmsg_get_overload_size (const char *);

/*
 * logger weight percentage:
 * 	device	: 55 %
 * 	kernel	: 19 %
 * 	event	: 4 %
 * 	radio	: 20 %
 * 	meminfo	: 1 %
 * 	lastk	: 1 %
 */
static LOGGER loggers [] = {
	LOGGER_ENTRY ("logdevice", 1, 0.55, 64,
		logdevice_init,
		logdevice_start,
		logdevice_stop,
		logdevice_get_total_size,
		logdevice_update_state,
		logdevice_rotate_and_limit,
		logdevice_get_log_filepath,
		logdevice_clear_log_files,
		logdevice_check_file_type,
		NULL),
	LOGGER_ENTRY ("logkmsg", 1, 0.19, 20,
		NULL,
		logkmsg_start,
		logkmsg_stop,
		logkmsg_get_total_size,
		logkmsg_update_state,
		logkmsg_rotate_and_limit,
		logkmsg_get_log_filepath,
		logkmsg_clear_log_files,
		logkmsg_check_file_type,
		logkmsg_watchdog),
	LOGGER_ENTRY ("logevents", 1, 0.04, 0,
		logevents_init,
		logevents_start,
		logevents_stop,
		logevents_get_total_size,
		logevents_update_state,
		logevents_rotate_and_limit,
		logevents_get_log_filepath,
		logevents_clear_log_files,
		logevents_check_file_type,
		NULL),
	LOGGER_ENTRY ("logradio", 1, 0.20, 20,
		logradio_init,
		logradio_start,
		logradio_stop,
		logradio_get_total_size,
		logradio_update_state,
		logradio_rotate_and_limit,
		logradio_get_log_filepath,
		logradio_clear_log_files,
		logradio_check_file_type,
		NULL),
	LOGGER_ENTRY ("logmeminfo", 1, 0.01, 0,
		logmeminfo_init,
		logmeminfo_start,
		logmeminfo_stop,
		logmeminfo_get_total_size,
		logmeminfo_update_state,
		logmeminfo_rotate_and_limit,
		logmeminfo_get_log_filepath,
		logmeminfo_clear_log_files,
		logmeminfo_check_file_type,
		NULL),
	LOGGER_ENTRY ("dumplastkmsg", 1, 0.01, 0,
		NULL,
		dumplastkmsg_start,
		dumplastkmsg_stop,
		dumplastkmsg_get_total_size,
		dumplastkmsg_update_state,
		dumplastkmsg_rotate_and_limit,
		dumplastkmsg_get_log_filepath,
		dumplastkmsg_clear_log_files,
		dumplastkmsg_check_file_type,
		NULL),
	LOGGER_ENTRY ("",0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL) /* the name field cannot be NULL because we need it to compile config key strings */
};

static STORAGE storages [] = {
	STORAGE_ENTRY (STORAGE_KEY_USB,		NULL,		4, dir_get_usb_storage),
	STORAGE_ENTRY (STORAGE_KEY_EXTERNAL,	NULL,		3, dir_get_external_storage),
	STORAGE_ENTRY (STORAGE_KEY_PHONE,	NULL,		2, dir_get_phone_storage),
	STORAGE_ENTRY (STORAGE_KEY_INTERNAL,	"/data",	1, NULL),
	STORAGE_ENTRY ("",			NULL,		0, NULL) /* the name field cannot be NULL because we need it to compile config key strings */
};

/*
 * define custom socket commands
 */
enum
{
	LOCAL_CMD_GETPATH = 0,
	LOCAL_CMD_RUN,
	LOCAL_CMD_STOP,
	LOCAL_CMD_ISLOGGING,
	LOCAL_CMD_MOUNT,
	LOCAL_CMD_CANWRITE,
	LOCAL_CMD_CLEARLOG,
	LOCAL_CMD_CLEARLOGDIR,
	LOCAL_CMD_MOVELOG,
	LOCAL_CMD_BUGREPORT,
	LOCAL_CMD_DUMPSYS,
	LOCAL_CMD_GETDEBUG,
	LOCAL_CMD_SETDEBUG,
	LOCAL_CMD_GETPHONESTORAGE,
	LOCAL_CMD_GETEXTSTORAGE,
	LOCAL_CMD_GETUSBSTORAGE,
	LOCAL_CMD_STATFS,
	LOCAL_CMD_GETLOGFILES,
	LOCAL_CMD_GETLASTERROR,
	LOCAL_CMD_SYNCCONF,
	LOCAL_CMD_GENCONF,
	LOCAL_CMD_STORAGEINIT,
	LOCAL_CMD_COUNT
};

static const char *socket_commands [LOCAL_CMD_COUNT] = {
	[LOCAL_CMD_GETPATH]		= ":getpath:",
	[LOCAL_CMD_RUN]			= ":run:",
	[LOCAL_CMD_STOP]		= ":stop:",
	[LOCAL_CMD_ISLOGGING]		= ":islogging:",
	[LOCAL_CMD_MOUNT]		= ":mount:",
	[LOCAL_CMD_CANWRITE]		= ":canwrite:",
	[LOCAL_CMD_CLEARLOG]		= ":clearlog:",
	[LOCAL_CMD_CLEARLOGDIR]		= ":clearlogdir:",
	[LOCAL_CMD_MOVELOG]		= ":movelog:",
	[LOCAL_CMD_BUGREPORT]		= ":bugreport:",
	[LOCAL_CMD_DUMPSYS]		= ":dumpsys:",
	[LOCAL_CMD_GETDEBUG]		= ":getdebug:",
	[LOCAL_CMD_SETDEBUG]		= ":setdebug:",
	[LOCAL_CMD_GETPHONESTORAGE]	= ":getphonestorage:",
	[LOCAL_CMD_GETEXTSTORAGE]	= ":getextstorage:",
	[LOCAL_CMD_GETUSBSTORAGE]	= ":getusbstorage:",
	[LOCAL_CMD_STATFS]		= ":statfs:",
	[LOCAL_CMD_GETLOGFILES]		= ":getlogfiles:",
	[LOCAL_CMD_GETLASTERROR]	= ":getlasterror:",
	[LOCAL_CMD_SYNCCONF]		= ":syncconf:",
	[LOCAL_CMD_GENCONF]		= ":genconf:",
	[LOCAL_CMD_STORAGEINIT]		= ":storageinit:",
};

static void socket_commands_init (void)
{
}

static int socket_commands_index (const char **cmds, int count, const char *cmd)
{
	int i;
	for (i = 0; i < count; i ++)
	{
		if (CMP_CMD (cmd, cmds [i]))
			return i;
	}
	return -1;
}

static pthread_mutex_t lock_storage = PTHREAD_MUTEX_INITIALIZER;

static const char *storage_excluded_text (unsigned long excluded)
{
	static char global_buffer [38];
	int idx = 0;

	if ((idx < (int) sizeof (global_buffer)) && (excluded & STORAGE_EXCLUDED_MASK_USER_SETTING))
	{
		const char *text = "not-sel,";
		snprintf (& global_buffer [idx], sizeof (global_buffer) - idx, "%s", text);
		idx += strlen (text);
	}

	if ((idx < (int) sizeof (global_buffer)) && (excluded & STORAGE_EXCLUDED_MASK_UNMOUNT))
	{
		const char *text = "not-mount,";
		snprintf (& global_buffer [idx], sizeof (global_buffer) - idx, "%s", text);
		idx += strlen (text);
	}

	if ((idx < (int) sizeof (global_buffer)) && (excluded & STORAGE_EXCLUDED_MASK_READONLY))
	{
		const char *text = "read-only,";
		snprintf (& global_buffer [idx], sizeof (global_buffer) - idx, "%s", text);
		idx += strlen (text);
	}

	if ((idx < (int) sizeof (global_buffer)) && (excluded & STORAGE_EXCLUDED_MASK_NO_FREE_SPACE))
	{
		const char *text = "low-free,";
		snprintf (& global_buffer [idx], sizeof (global_buffer) - idx, "%s", text);
		idx += strlen (text);
	}

	if (idx >= (int) sizeof (global_buffer))
	{
		idx = (int) sizeof (global_buffer) - 1;
	}

	global_buffer [idx --] = 0;

	if ((idx >= 0) && (global_buffer [idx] == ','))
		global_buffer [idx] = 0;

	return (const char *) global_buffer;
}

static unsigned long storage_auto_limited_size_mb_nolock (unsigned long total_size_mb)
{
	unsigned long default_value = (unsigned long) atol (LOG_DEFAULT_SIZE_LIMIT);

	if (total_size_mb == 0)
		return 0;

	if (total_size_mb <= default_value)
		return (unsigned long) (total_size_mb / 2);

	return (unsigned long) (total_size_mb / 4);
}

static unsigned long storage_auto_reserved_size_mb_nolock (unsigned long total_size_mb)
{
	unsigned long default_value = (unsigned long) atol (LOG_DEFAULT_SIZE_RESERVED);

	if (total_size_mb == 0)
		return 0;

	if (total_size_mb <= 1024)
		return 100;

	return default_value;
}

static unsigned long storage_get_free_size_mb (const char *path)
{
	struct statfs st;

	if ((! path) || (statfs_nointr (path, & st) < 0))
		return (unsigned long) -1;

	if (st.f_blocks == 0)
		return (unsigned long) -1;

	return (unsigned long) ((unsigned long long) st.f_bsize * (unsigned long long) st.f_bfree / MB);
}

static int storage_update_nolock (STORAGE *ps)
{
	STORAGE_MOUNT_ENTRY me;
	struct statfs st;

	if ((! ps) || (! ps->name) || (! ps->name [0]))
		return -1;

	ps->excluded &= ~(STORAGE_EXCLUDED_MASK_UNMOUNT | STORAGE_EXCLUDED_MASK_READONLY | STORAGE_EXCLUDED_MASK_NO_FREE_SPACE);

	//if (! ps->mountpoint)
	{
		if (ps->getmountpoint)
			ps->mountpoint = ps->getmountpoint ();

		if (! ps->mountpoint)
		{
			DM ("storage_update [%s:unknown]: not-mount: cannot find mountpoint\n", ps->name);
			ps->excluded |= STORAGE_EXCLUDED_MASK_UNMOUNT;
			return -1;
		}
	}

	memset (& me, 0, sizeof (me));

	if (dir_get_mount_entry (ps->mountpoint, & me) == 0)
	{
		DM ("storage_update [%s:%s]: not-mount: no mount entry\n", ps->name, ps->mountpoint);
		ps->excluded |= STORAGE_EXCLUDED_MASK_UNMOUNT;
		return -1;
	}

	if (me.options && (strncmp (me.options, "ro,", 3) == 0))
	{
		DM ("storage_update [%s:%s]: read-only: [%s][%s][%s][%s]\n", ps->name, ps->mountpoint, me.device, me.mountpoint, me.type, me.options);
		ps->excluded |= STORAGE_EXCLUDED_MASK_READONLY;
		return -1;
	}

	memset (& st, 0, sizeof (st));

	if (statfs_nointr (ps->mountpoint, & st) < 0)
	{
		DM ("storage_update [%s:%s]: not-mount: statfs_nointr:%s\n", ps->name, ps->mountpoint, strerror (errno));
		ps->excluded |= STORAGE_EXCLUDED_MASK_UNMOUNT;
		return -1;
	}

	if (st.f_blocks == 0)
	{
		DM ("storage_update [%s:%s]: not-mount: zero blocks\n", ps->name, ps->mountpoint);
		ps->excluded |= STORAGE_EXCLUDED_MASK_UNMOUNT;
		return -1;
	}

	ps->total_size_mb = (unsigned long) ((unsigned long long) st.f_bsize * (unsigned long long) st.f_blocks / MB);
	ps->free_size_mb = (unsigned long) ((unsigned long long) st.f_bsize * (unsigned long long) st.f_bfree / MB);

	if (ps->reserved_size_mb == 0)
	{
		ps->real_reserved_size_mb = storage_auto_reserved_size_mb_nolock (ps->total_size_mb);
	}
	else
	{
		ps->real_reserved_size_mb = ps->reserved_size_mb;
	}

	if (ps->limited_size_mb == 0)
	{
		ps->real_limited_size_mb = storage_auto_limited_size_mb_nolock (ps->total_size_mb);
	}
	else
	{
		ps->real_limited_size_mb = ps->limited_size_mb;
	}

	if (ps->free_size_mb < ps->real_reserved_size_mb)
	{
		DM ("storage_update [%s:%s]: low-free: free=%luMB, reserved=%luMB, total=%luMB, limited=%luMB\n",
			ps->name, ps->mountpoint, ps->free_size_mb, ps->real_reserved_size_mb, ps->total_size_mb, ps->real_limited_size_mb);

		ps->excluded |= STORAGE_EXCLUDED_MASK_NO_FREE_SPACE;
	}
	else
	{
		DM ("storage_update [%s:%s]: valid: priority=%d, free=%luMB, reserved=%luMB, total=%luMB, limited=%luMB\n",
			ps->name, ps->mountpoint, ps->priority, ps->free_size_mb, ps->real_reserved_size_mb, ps->total_size_mb, ps->real_limited_size_mb);
	}
	return 0;
}

static int storage_update (STORAGE *ps)
{
	int ret;
	LOCK (& lock_storage);
	ret = storage_update_nolock (ps);
	UNLOCK (& lock_storage);
	return ret;
}

static void storage_update_all (void)
{
	int i;
	LOCK (& lock_storage);
	for (i = 0; storages [i].name; i ++)
	{
		storage_update_nolock (& storages [i]);
	}
	UNLOCK (& lock_storage);
}

static void storage_reinit (void)
{
	LOCK (& lock_storage);
	dir_clear_storage_path ();
	UNLOCK (& lock_storage);
}

static void storage_init (void)
{
	int i;
	LOCK (& lock_storage);
	for (i = 0; storages [i].name; i ++)
	{
		if (storages [i].name [0] == 0)
		{
			/* this is the last entry, set to NULL directly to prevent other loops go into this */
			storages [i].name = NULL;
			break;
		}

		storages [i].excluded = 0;
		storages [i].real_limited_size_mb = 0;
		storages [i].real_reserved_size_mb = 0;
		storages [i].limited_size_mb = 0;
		storages [i].reserved_size_mb = 0;
		storages [i].free_size_mb = 0;

		storage_update_nolock (& storages [i]);
	}
	UNLOCK (& lock_storage);
}

static void storage_dump_status (void)
{
	int i;
	LOCK (& lock_storage);
	for (i = 0; storages [i].name; i ++)
	{
		file_log (LOG_TAG ": [embedded] storage [%s:%s]: prio=%d, excluded=[%s], reserved=[%lu:%lu]MB, limited=[%lu:%lu]MB, free=%luMB, total=%luMB\n",
			storages [i].name, storages [i].mountpoint,
			storages [i].priority,
			storage_excluded_text (storages [i].excluded),
			storages [i].reserved_size_mb, storages [i].real_reserved_size_mb,
			storages [i].limited_size_mb, storages [i].real_limited_size_mb,
			storages [i].free_size_mb,
			storages [i].total_size_mb);
	}
	UNLOCK (& lock_storage);
}

static int storage_compare_priority (const char *name1, const char *name2)
{
	if (name1 && name2)
	{
		int i, w1 = 0, w2 = 0;
		LOCK (& lock_storage);
		for (i = 0; storages [i].name; i ++)
		{
			if (strcmp (name1, storages [i].name) == 0)
			{
				w1 = storages [i].priority;
			}
			else if (strcmp (name2, storages [i].name) == 0)
			{
				w2 = storages [i].priority;
			}
		}
		UNLOCK (& lock_storage);
		return w1 - w2;
	}
	return 0;
}

static int storage_set_configs (STORAGE *ps, unsigned long limited_size_mb, unsigned long reserved_size_mb, int priority, int user_excluded)
{
	if (ps)
	{
		LOCK (& lock_storage);
		ps->priority = priority;
		if (user_excluded)
		{
			ps->excluded |= STORAGE_EXCLUDED_MASK_USER_SETTING;
		}
		else
		{
			ps->excluded &= ~STORAGE_EXCLUDED_MASK_USER_SETTING;
		}
		ps->limited_size_mb = limited_size_mb;
		ps->reserved_size_mb = reserved_size_mb;
		UNLOCK (& lock_storage);
		return 0;
	}
	return -1;
}

static STORAGE *storage_get_by_name_or_path_nolock (const char *name)
{
	if (name)
	{
		int i;
		for (i = 0; storages [i].name; i ++)
		{
			if ((name [0] != '/') && (strcmp (storages [i].name, name) == 0))
				return & storages [i];

			/* take name as a path */
			if (storages [i].mountpoint && (strncmp (name, storages [i].mountpoint, strlen (storages [i].mountpoint)) == 0))
				return & storages [i];
		}
	}
	return NULL;
}

static const char *storage_get_mountpoint_by_name (const char *name)
{
	if (name)
	{
		STORAGE *ps;

		LOCK (& lock_storage);
		if ((ps = storage_get_by_name_or_path_nolock (name)) != NULL)
		{
			if ((! ps->mountpoint) && ps->getmountpoint)
			{
				ps->mountpoint = ps->getmountpoint ();
			}
		}
		UNLOCK (& lock_storage);

		if (ps)
			return ps->mountpoint;
	}
	return NULL;
}

#define	STORAGE_SELECT_LOGGING_PATH_ERROR_INVALID_BUFFER	(-1)
#define	STORAGE_SELECT_LOGGING_PATH_ERROR_NO_AVAILABLE_STORAGE	(-2)
#define	STORAGE_SELECT_LOGGING_PATH_ERROR_CANNOT_GET_LOG_PATH	(-3)

static unsigned long storage_query_log_path_and_excluded_state (char *buffer, int len)
{
	unsigned long ret = (unsigned long) -1;

	if (buffer && buffer [0] && (len > 0) && (strcmp (buffer, STORAGE_KEY_AUTO) != 0))
	{
		LOCK (& lock_storage);

		STORAGE *ps = storage_get_by_name_or_path_nolock (buffer);

		if (ps)
		{
			storage_update_nolock (ps);

			SAFE_SPRINTF (buffer, len, "%s", ps->name); /* force using storage name */

			if (dir_select_log_path (buffer, len) < 0)
			{
				DM ("failed to query log path of [%s]\n", buffer);
			}
			else
			{
				ret = ps->excluded;
			}
		}

		UNLOCK (& lock_storage);
	}
	return ret;
}

static int storage_select_logging_path (char *buffer, int len, const char *excluded_name)
{
	if (buffer && (len > 0))
	{
		int i;
		int idx = -1;

		if ((strcmp (buffer, STORAGE_KEY_AUTO) == 0) || (buffer [0] == 0)) /* log path is dynamic */
		{
			int priority;

			LOCK (& lock_storage);

			for (i = 0, priority = 0; storages [i].name; i ++)
			{
				storage_update_nolock (& storages [i]);

				if (excluded_name && (strcmp (storages [i].name, excluded_name) == 0))
					continue;

				if (storages [i].excluded)
					continue;

				if (storages [i].priority > priority)
				{
					idx = i;
					priority = storages [i].priority;
				}
			}

			if (idx >= 0)
			{
				SAFE_SPRINTF (buffer, len, "%s", storages [idx].name);
			}

			UNLOCK (& lock_storage);

			if (idx == -1)
			{
				return STORAGE_SELECT_LOGGING_PATH_ERROR_NO_AVAILABLE_STORAGE;
			}
		}
		else /* log path is specified */
		{
			LOCK (& lock_storage);

			STORAGE *ps = storage_get_by_name_or_path_nolock (buffer);

			if (ps)
			{
				storage_update_nolock (ps);

				if (! ((ps->excluded) || (excluded_name && (strcmp (ps->name, excluded_name) == 0))))
				{
					file_log (LOG_TAG ": [embedded] log path[%s] is selected.\n", buffer);

					SAFE_SPRINTF (buffer, len, "%s", ps->name); /* force using storage name */
					idx = 0;
				}
				else
				{
					file_log (LOG_TAG ": [embedded] log path[%s] is excluded.\n", buffer);
				}
			}
			else
			{
				file_log (LOG_TAG ": [embedded] log path[%s] is not found.\n", buffer);
			}

			UNLOCK (& lock_storage);

			if (idx == -1)
			{
				return STORAGE_SELECT_LOGGING_PATH_ERROR_NO_AVAILABLE_STORAGE;
			}
		}

		if (dir_select_log_path (buffer, len) < 0)
		{
			return STORAGE_SELECT_LOGGING_PATH_ERROR_CANNOT_GET_LOG_PATH;
		}

		return 0;
	}
	return STORAGE_SELECT_LOGGING_PATH_ERROR_INVALID_BUFFER;
}

static unsigned long storage_get_limited_size_mb (const STORAGE *ps)
{
	unsigned long size = 0;
	if (ps)
	{
		LOCK (& lock_storage);
		size = ps->real_limited_size_mb;
		UNLOCK (& lock_storage);
	}
	return size;
}

static unsigned long storage_get_reserved_size_mb (const STORAGE *ps)
{
	unsigned long size = 0;
	if (ps)
	{
		LOCK (& lock_storage);
		size = ps->real_reserved_size_mb;
		UNLOCK (& lock_storage);
	}
	return size;
}

static pthread_mutex_t lock_session = PTHREAD_MUTEX_INITIALIZER;

static LOGDATA_HEADER session;
static char session_devinfo [16]; /* device serial number (ro.serialno) */
static char session_prdinfo [16]; /* product name (ro.build.product) */
static char session_rominfo [16]; /* rom version (ro.build.description) */

static void session_data_filename (char *buffer, int len)
{
	if (buffer && (len > 0))
	{
		/*
		 * always keep session info in internal storage
		 */
		snprintf (buffer, len, LOG_DIR ".session." LOG_DATA_EXT);
		buffer [len - 1] = 0;
	}
}

static void session_load_nolock (char *buffer, int len)
{
	int fd, count;

	session_data_filename (buffer, len);

	if ((fd = open_nointr (buffer, O_RDWR | O_CREAT, LOG_FILE_MODE)) < 0)
	{
		DM ("[embedded] open [%s]: %s\n", buffer, strerror (errno));
		goto fixdata;
	}

	memset (& session, 0, sizeof (LOGDATA_HEADER));

	count = read_nointr (fd, & session, sizeof (LOGDATA_HEADER));

	if (count < 0)
	{
		DM ("[embedded] read [%s]: %s\n", buffer, strerror (errno));
		goto fixdata;
	}

	if (count != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] read [%s] wrong size [%d/%d]\n", buffer, count, (int) sizeof (LOGDATA_HEADER));
		goto fixdata;
	}

	if (session.magic != LOGDATA_MAGIC)
	{
		DM ("[embedded] session [%s] wrong magic [%08lX]! corrupted?\n", buffer, session.magic);
		goto fixdata;
	}

	if (session.entry_count > 9999)
	{
		DM ("[embedded] session [%s] invalid sn! corrupted?\n", buffer);
		goto fixdata;
	}

	close (fd);
	return;

fixdata:;
	session.magic = LOGDATA_MAGIC;
	session.entry_count = 0;
	session.header_size = sizeof (session);

	if (fd >= 0)
	{
		write_nointr (fd, & session, sizeof (LOGDATA_HEADER));
		close (fd);
	}
}

static void session_save_nolock (void)
{
	char buffer [PATH_MAX];
	int fd;

	if (session.magic != LOGDATA_MAGIC)
		return;

	if (session.entry_count > 9999)
		return;

	session_data_filename (buffer, sizeof (buffer));

	if ((fd = open_nointr (buffer, O_RDWR | O_CREAT, LOG_FILE_MODE)) < 0)
	{
		DM ("[embedded] open [%s]: %s\n", buffer, strerror (errno));
		return;
	}

	write_nointr (fd, & session, sizeof (LOGDATA_HEADER));
	close (fd);
}

static void session_reset_sn (void)
{
	LOCK (& lock_session);
	session.entry_count = 0;
	UNLOCK (& lock_session);
}

static unsigned short session_get_current_sn (void)
{
	unsigned short ret;

	LOCK (& lock_session);

	if ((session.entry_count == 0) || (session.entry_count > 9999))
	{
		session.entry_count = 1;
		session_save_nolock ();
	}

	ret = (unsigned short) session.entry_count;

	UNLOCK (& lock_session);
	return ret;
}

static unsigned short session_get_next_sn (void)
{
	unsigned short ret;

	LOCK (& lock_session);

	session.entry_count ++;

	if ((session.entry_count == 0) || (session.entry_count > 9999))
		session.entry_count = 1;

	session_save_nolock ();

	ret = (unsigned short) session.entry_count;

	UNLOCK (& lock_session);
	return ret;
}

static void session_init (char *buffer, int len)
{
	char *ptr;

	LOCK (& lock_session);

	session_load_nolock (buffer, len);

	property_get ("ro.serialno", buffer, "unknown"); /* [ro.serialno]: [HT23GW301202] */
	SAFE_SPRINTF (session_devinfo, sizeof (session_devinfo), "%s", buffer);

	property_get ("ro.build.product", buffer, "unknown"); /* [ro.build.product]: [evita] */
	SAFE_SPRINTF (session_prdinfo, sizeof (session_prdinfo), "%s", buffer);

	property_get ("ro.build.description", buffer, "0.1.0.0"); /* [ro.build.description]: [0.1.0.0 (20120613 Evita_ATT_WWE #) test-keys] */
	ptr = strchr (buffer, ' ');
	if (ptr) *ptr = 0;
	SAFE_SPRINTF (session_rominfo, sizeof (session_rominfo), "%s", buffer);

	UNLOCK (& lock_session);
}

static pthread_mutex_t lock_logger = PTHREAD_MUTEX_INITIALIZER;
static const STORAGE *loggers_global_storage = NULL;
static char loggers_global_logpath [PATH_MAX] = "";
static char loggers_error_message [PATH_MAX] = "";
static int loggers_error_counter = 0;

static int loggers_set_configs (LOGGER *pl, unsigned long limited_size_mb, unsigned long rotate_size_mb, int compress, int enabled, float weight, int overload)
{
	if (pl)
	{
		LOCK (& lock_logger);

		pl->limited_size_mb = limited_size_mb;
		pl->rotate_size_mb = rotate_size_mb;

		if (compress)
		{
			pl->state |= LOGGER_STATE_MASK_COMPRESS;
		}
		else
		{
			pl->state &= ~LOGGER_STATE_MASK_COMPRESS;
		}

		pl->enabled = enabled;
		pl->weight = weight;
		pl->overload = overload;

		DM ("loggers_set_configs: %s: enabled=%d, weight=%.3f, overload=%d, limited_size_mb=%lu, rotate_size_mb=%lu, compress=%d\n",
			pl->name, pl->enabled, pl->weight, pl->overload, pl->limited_size_mb, pl->rotate_size_mb, (pl->state & LOGGER_STATE_MASK_COMPRESS) != 0);

		UNLOCK (& lock_logger);
		return 0;
	}
	return -1;
}

static void loggers_init (void)
{
	int i;

	LOCK (& lock_logger);

	loggers_global_storage = NULL;
	loggers_global_logpath [0] = 0;

	for (i = 0; loggers [i].name; i ++)
	{
		if (loggers [i].name [0] == 0)
		{
			/* this is the last entry, set to NULL directly to prevent other loops go into this */
			loggers [i].name = NULL;
			break;
		}

		loggers [i].state = LOGGER_STATE_MASK_STOPPED;
		loggers [i].real_limited_size_mb = 0;

		if (loggers [i].weight > 0)
		{
			loggers [i].limited_size_mb = 0;
		}
		else
		{
			loggers [i].limited_size_mb = (unsigned long) atol (LOG_DEFAULT_SIZE_LIMIT);
		}

		loggers [i].serialno = 0;
		loggers [i].rotate_size_mb = (unsigned long) atol (LOG_DEFAULT_ROTATE_SIZE);

		loggers [i].conf_extras = NULL;
		loggers [i].conf_extras_count = 0;

		if (loggers [i].log_init && (loggers [i].log_init (loggers [i].name, & loggers [i].conf_extras, & loggers [i].conf_extras_count) < 0))
		{
			file_log (LOG_TAG ": [embedded] logger [%s] init failed!\n", loggers [i].name);

			/* ignore this logger */
			loggers [i].name = NULL;
		}
	}
	UNLOCK (& lock_logger);
}

static LOGGER *loggers_get_logger_by_name_nolock (const char *name)
{
	if (name)
	{
		int i;

		for (i = 0; loggers [i].name; i ++)
		{
			if (strcmp (loggers [i].name, name) == 0)
				return & loggers [i];
		}
	}
	return NULL;
}

static int loggers_increase_serialno_by_name_nolock (const char *name)
{
	if (name)
	{
		int i;

		for (i = 0; loggers [i].name; i ++)
		{
			if (strcmp (loggers [i].name, name) == 0)
			{
				loggers [i].serialno ++;

				if (loggers [i].serialno > 9999)
				{
					loggers [i].serialno = 1;
				}

				return loggers [i].serialno;
			}
		}
	}
	return 0;
}

static void loggers_request_remove_old_logs_nolock (void)
{
	int i;

	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		loggers [i].state |= LOGGER_STATE_MASK_REQUEST_REMOVE_OLDLOG;
	}
}

static void loggers_update_state (void)
{
	int i;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (! loggers [i].log_update_state)
			continue;

		loggers [i].state = loggers [i].log_update_state (loggers [i].name, loggers [i].state);
	}
	UNLOCK (& lock_logger);
}

static int loggers_have_error_fatal (void)
{
	int i, ret = 0;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_FATAL)
		{
			ret = 1;
			break;
		}
	}
	UNLOCK (& lock_logger);
	return ret;
}

static int loggers_have_error (void)
{
	int i, ret = 0;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_ALL)
		{
			ret = 1;
			break;
		}
	}
	UNLOCK (& lock_logger);
	return ret;
}

static void loggers_clear_errors (void)
{
	int i;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		loggers [i].state &= ~LOGGER_STATE_MASK_ERROR_ALL;
	}
	UNLOCK (& lock_logger);
}

static int loggers_show_errors (void)
{
	int i, ret = 0;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_OPEN)
		{
			file_log (LOG_TAG ": detected %s open error!\n", loggers [i].name);
		}
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_READ)
		{
			file_log (LOG_TAG ": detected %s read error!\n", loggers [i].name);
		}
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_WRITE)
		{
			file_log (LOG_TAG ": detected %s write error!\n", loggers [i].name);
		}
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_PRSTAT)
		{
			file_log (LOG_TAG ": detected %s pid/tid stat error!\n", loggers [i].name);
		}
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_ZOMBIE)
		{
			file_log (LOG_TAG ": detected %s process zombie!\n", loggers [i].name);
		}
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_DEFUNCT)
		{
			file_log (LOG_TAG ": detected %s process defunct!\n", loggers [i].name);
		}
		if (loggers [i].state & LOGGER_STATE_MASK_ERROR_MISSING)
		{
			file_log (LOG_TAG ": detected %s file disappeared!\n", loggers [i].name);
		}
	}
	UNLOCK (& lock_logger);
	return ret;
}

static int loggers_touch_watchdog (void)
{
	int i, ret = 0;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (loggers [i].log_watchdog && (loggers [i].log_watchdog (loggers [i].name) == 1))
		{
			ret = 1 /* barking */;
			break;
		}
	}
	UNLOCK (& lock_logger);
	return ret;
}

static int loggers_logging_one (void)
{
	int i, ret = 0;
	LOCK (& lock_logger);
	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (! loggers [i].log_update_state)
			continue;

		if ((loggers [i].state & LOGGER_STATE_MASK_STARTED) && (loggers [i].state & LOGGER_STATE_MASK_LOGGING))
		{
			ret = 1;
			break;
		}
	}
	UNLOCK (& lock_logger);
	return ret;
}

static int loggers_logging_all (void)
{
	int i, count, ret = 1;
	LOCK (& lock_logger);
	for (i = 0, count = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (! loggers [i].log_update_state)
			continue;

		count ++;

		if ((loggers [i].state & LOGGER_STATE_MASK_STOPPED) || (! (loggers [i].state & LOGGER_STATE_MASK_LOGGING)))
		{
			ret = 0;
			break;
		}
	}
	if (count == 0) ret = 0;
	UNLOCK (& lock_logger);
	return ret;
}

static unsigned long loggers_get_total_size_mb_nolock (void)
{
	unsigned long long total_size;
	int i;
	for (i = 0, total_size = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].log_get_total_size)
			continue;

		total_size += loggers [i].log_get_total_size (loggers [i].name);
	}
	return (unsigned long) (total_size / MB);
}

static unsigned long loggers_get_total_size_mb (void)
{
	unsigned long ret;
	LOCK (& lock_logger);
	ret = loggers_get_total_size_mb_nolock ();
	UNLOCK (& lock_logger);
	return ret;
}

static void loggers_process_rotation_and_limitation (void)
{
	int i;
	unsigned long reserved_size_mb = 0;

	LOCK (& lock_logger);

	if (loggers_global_storage)
	{
		DM ("logging [%s][%s]: free=%luMB, reserved=%luMB, total=%luMB, limited=%luMB, log=%luMB\n",
				loggers_global_logpath,
				loggers_global_storage->name,
				loggers_global_storage->free_size_mb,
				loggers_global_storage->real_reserved_size_mb,
				loggers_global_storage->total_size_mb,
				loggers_global_storage->real_limited_size_mb,
				loggers_get_total_size_mb_nolock ());

		reserved_size_mb = loggers_global_storage->real_reserved_size_mb;
	}
	else
	{
		DM ("logging [%s]\n", loggers_global_logpath);
	}

	for (i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if ((! loggers [i].log_rotate_and_limit) || (loggers [i].state & LOGGER_STATE_MASK_STOPPED))
			continue;

		loggers [i].log_rotate_and_limit (loggers [i].name, loggers [i].state, loggers [i].rotate_size_mb, loggers [i].real_limited_size_mb, reserved_size_mb);
	}

	UNLOCK (& lock_logger);
}

static void loggers_clear_log_files (void)
{
	char path [PATH_MAX];
	int i, j, logging;

	GLIST_NEW (patterns);
	GLIST_NEW (patterns_internal);

	LOCK (& lock_logger);
	LOCK (& lock_storage);

	/*
	 * clear logs in og_xxx sub-folders
	 */
	glist_add (& patterns, "zfllog_");
	glist_add (& patterns_internal, "zfllog_");

	/*
	 * clear logger history on non-internal storages
	 */
	glist_add (& patterns, LOG_DATA_EXT ".txt");

	/*
	 * support clearing BT log files, requested by S91 BT Cheney Ni.
	 */
	glist_add (& patterns, "btsnoop_");
	glist_add (& patterns_internal, "btsnoop_");

	/*
	 * scan storages
	 */
	for (logging = 0, j = 0; storages [j].name; j ++)
	{
		SAFE_SPRINTF (path, sizeof (path), "%s", storages [j].name);

		dir_select_log_path (path, sizeof (path));

		for (i = 0; loggers [i].name; i ++)
		{
			if (! loggers [i].log_clear_log_files)
				continue;

			if (! (loggers [i].state & LOGGER_STATE_MASK_STOPPED))
			{
				DM ("loggers_clear_log_files: %s was started, ignore!\n", loggers [i].name);
				logging = 1;
				continue;
			}

			file_log (LOG_TAG ": [embedded] %s: clear logs in [%s]\n", loggers [i].name, path);

			loggers [i].log_clear_log_files (path);
		}

		file_log (LOG_TAG ": [embedded] others: clear logs in [%s]\n", path);

		if (strcmp (storages [j].name, STORAGE_KEY_INTERNAL) == 0)
		{
			dir_clear (path, patterns_internal);
		}
		else
		{
			dir_clear (path, patterns);
		}
	}

	glist_clear (& patterns, NULL);
	glist_clear (& patterns_internal, NULL);

	if (! logging)
	{
		session_data_filename (path, sizeof (path));

		errno = 0;
		unlink (path);
		DM ("unlink [%s][%s]\n", path, strerror (errno));

		session_reset_sn ();
	}

	UNLOCK (& lock_storage);
	UNLOCK (& lock_logger);
}

static void loggers_get_log_files (char *buffer, int len)
{
	char *ptr;
	int i;

	if ((! buffer) || (len <= 0))
		return;

	LOCK (& lock_logger);

	buffer [0] = 0;

	for (i = 0, ptr = buffer; loggers [i].name; i ++)
	{
		if (! loggers [i].enabled)
			continue;

		if (! loggers [i].log_get_log_filepath)
			continue;

		loggers [i].log_get_log_filepath (loggers [i].name, ptr, len - (ptr - buffer));

		if (*ptr == 0) /* get nothing */
			continue;

		ptr += strlen (ptr);

		if ((int) (ptr - buffer) >= (int) (len - 2))
		{
			DM ("not enough buffer!\n");
			break;
		}

		*ptr ++ = '\n';
		*ptr = 0;
	}

	if ((ptr > buffer) && (*(ptr - 1) == '\n'))
		ptr --;

	*ptr = 0;

	UNLOCK (& lock_logger);
}

static int loggers_check_file_type (const char *filepath)
{
	int ret, i;

	LOCK (& lock_logger);

	for (ret = LOGGER_FILETYPE_UNKNOWN, i = 0; loggers [i].name; i ++)
	{
		if (! loggers [i].log_check_file_type)
			continue;

		if ((ret = loggers [i].log_check_file_type (loggers [i].name, filepath)) != LOGGER_FILETYPE_UNKNOWN)
			break;
	}

	UNLOCK (& lock_logger);

	return ret;
}

static void loggers_stop (void)
{
	int i;

	LOCK (& lock_logger);

	/*
	 * stop loggers in reversed order, find the last index
	 */
	for (i = 0; loggers [i].name; i ++);

	for (i -= 1; i >= 0; i --)
	{
		if (! loggers [i].enabled)
			continue;

		loggers [i].state &= ~(LOGGER_STATE_MASK_STARTED | LOGGER_STATE_MASK_LOGGING);
		loggers [i].state |= LOGGER_STATE_MASK_STOPPED;

		if (loggers [i].log_stop)
		{
			file_log (LOG_TAG ": [embedded] loggers_stop() name[%s] +++!\n", loggers [i].name);
			loggers [i].log_stop (loggers [i].name);
			file_log (LOG_TAG ": [embedded] loggers_stop() name[%s] ---!\n", loggers [i].name);
		}
	}

	loggers_global_storage = NULL;
	loggers_global_logpath [0] = 0;

	UNLOCK (& lock_logger);
}

static int loggers_start (const char *storage_name)
{
	const char *properties_runtime [] = {
		PROPERTY_BOOT_COMPLETED_TOOL,	/* indicate tool java layer received boot completed intent or not */
		PROPERTY_BOOT_COMPLETED_COS,	/* indicate COS java runtime is ready or not */
		"persist.radio.lockui.stt",	/* SSD Test Tool UI lock */
		"persist.sys.stt.emmc_prealloc",/* eMMC prealloc flag for tool internal use */
		NULL
	};

	const char *attributes_runtime [] = {
		"/proc/driver/hdf",		/* HTCLOG driver mask */
		"/system/etc/hldm.bin",		/* HTCLOG default mask */
		"/sys/power/wake_lock",		/* wake lock list */
		"/sys/power/wake_unlock",	/* wake unlock list */
		NULL
	};

	char *ptr;
	int i, ret = 0;

	LOCK (& lock_logger);

	/*
	 * the storage_name would be "auto", need to parse it in storage_select_logging_path() first.
	 */
	SAFE_SPRINTF (loggers_global_logpath, sizeof (loggers_global_logpath), "%s", storage_name ? storage_name : db_get ("systemloggers.LogPath", pair_LogPath.value));

	i = storage_select_logging_path (loggers_global_logpath, sizeof (loggers_global_logpath), NULL);

	if (i < 0)
	{
		switch (i)
		{
		case STORAGE_SELECT_LOGGING_PATH_ERROR_NO_AVAILABLE_STORAGE:
			SAFE_SPRINTF (loggers_error_message, sizeof (loggers_error_message), "%s",
					"No valid storage is available! Please make sure at least one storage has enough space for logging.");

			file_log (LOG_TAG ": [embedded] start loggers [%s]: no valid storage available! abort!\n", storage_name);
			break;

		case STORAGE_SELECT_LOGGING_PATH_ERROR_CANNOT_GET_LOG_PATH:
			loggers_global_storage = storage_get_by_name_or_path_nolock (loggers_global_logpath);

			SAFE_SPRINTF (loggers_error_message, sizeof (loggers_error_message),
					"Cannot access path [%s] on storage [%s]! Please try to change the Preferred Logging Storage setting.",
					loggers_global_logpath,
					loggers_global_storage ? loggers_global_storage->name : "unknown");

			file_log (LOG_TAG ": [embedded] start loggers [%s]: cannot access path [%s]! abort!\n", storage_name, loggers_global_logpath);
			break;

		case STORAGE_SELECT_LOGGING_PATH_ERROR_INVALID_BUFFER:
		default:
			SAFE_SPRINTF (loggers_error_message, sizeof (loggers_error_message), "%s",
					"An unknown error was occurred! This would be caused by memory corruption! Please try rebooting device to recover.");

			file_log (LOG_TAG ": [embedded] start loggers [%s]: unknown error! abort!\n", storage_name);
			break;
		}

		storage_dump_status ();

		ret = -1;
	}
	else
	{
		char buffer [PATH_MAX];
		unsigned long storage_limited_size_mb;

		loggers_error_counter = 0;

		loggers_global_storage = storage_get_by_name_or_path_nolock (loggers_global_logpath);

		if (! loggers_global_storage)
		{
			file_log (LOG_TAG ": [embedded] cannot find storage [%s] by path [%s]! force selecting internal!\n", storage_name, loggers_global_logpath);

			SAFE_SPRINTF (loggers_global_logpath, sizeof (loggers_global_logpath), "%s", "internal");

			storage_select_logging_path (loggers_global_logpath, sizeof (loggers_global_logpath), NULL);

			loggers_global_storage = storage_get_by_name_or_path_nolock (loggers_global_logpath);
		}

		dir_create_recursive (loggers_global_logpath);

		dir_no_media (loggers_global_logpath);

		/*
		 * change session info
		 */
		session_get_next_sn ();

		storage_limited_size_mb = storage_get_limited_size_mb (loggers_global_storage);

		file_log (LOG_TAG ": [embedded] start loggers [%s][%s][%s][reserved:%lu][limit:%lu]\n",
				storage_name,
				loggers_global_storage->name,
				loggers_global_logpath,
				storage_get_reserved_size_mb (loggers_global_storage),
				storage_limited_size_mb);

		storage_dump_status ();

		dump_properties_and_attributes (properties_runtime, attributes_runtime);

		for (i = 0; loggers [i].name; i ++)
		{
			loggers [i].serialno = 0;

			if (! loggers [i].enabled)
				continue;

			loggers [i].state &= ~(LOGGER_STATE_MASK_STOPPED | LOGGER_STATE_MASK_LOGGING);
			loggers [i].state |= LOGGER_STATE_MASK_STARTED;

			if ((loggers [i].weight > 0) && (loggers [i].limited_size_mb == 0))
			{
				loggers [i].real_limited_size_mb = (unsigned long) (loggers [i].weight * storage_limited_size_mb);
			}
			else
			{
				loggers [i].real_limited_size_mb = (loggers [i].limited_size_mb == 0) ? storage_limited_size_mb : loggers [i].limited_size_mb;
			}

			if (loggers [i].log_start)
			{
				file_log (LOG_TAG ": [embedded] %s: [weight:%.3f][overload:%d][rotate:%lu][limit:%lu][compress:%d]\n",
						loggers [i].name,
						loggers [i].weight,
						loggers [i].overload,
						loggers [i].rotate_size_mb,
						loggers [i].real_limited_size_mb,
						(loggers [i].state & LOGGER_STATE_MASK_COMPRESS) != 0);

				if (loggers [i].log_start (loggers [i].name) < 0)
				{
					SAFE_SPRINTF (loggers_error_message, sizeof (loggers_error_message), "Failed to start logger [%s]!", loggers [i].name);
				}
			}
		}
	}

	UNLOCK (& lock_logger);
	return ret;
}

static void loggers_clear_last_error (void)
{
	LOCK (& lock_logger);
	loggers_error_message [0] = 0;
	UNLOCK (& lock_logger);
}

static void loggers_get_last_error (char *buffer, int len)
{
	LOCK (& lock_logger);
	SAFE_SPRINTF (buffer, len, "%s", loggers_error_message);
	UNLOCK (& lock_logger);
}

#define	LOGGERS_GLOBAL_STATE_CHANGING	(-1)
#define	LOGGERS_GLOBAL_STATE_STOPPED	(0)
#define	LOGGERS_GLOBAL_STATE_STARTED	(1)
#define	LOGGERS_GLOBAL_STATE_RESTARTING	(2)

static int loggers_global_state = LOGGERS_GLOBAL_STATE_STOPPED;

static int loggers_get_global_state (void)
{
	int ret;
	LOCK (& lock_logger);
	ret = loggers_global_state;
	UNLOCK (& lock_logger);
	return ret;
}

static void loggers_set_global_state (int state)
{
	LOCK (& lock_logger);
	loggers_global_state = state;
	UNLOCK (& lock_logger);
}

static int loggers_get_global_logpath_nolock (char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s", loggers_global_logpath);
	return 0;
}

static int loggers_get_global_logpath (char *buffer, int len)
{
	int ret;
	LOCK (& lock_logger);
	ret = loggers_get_global_logpath_nolock (buffer, len);
	UNLOCK (& lock_logger);
	return ret;
}

static int loggers_get_global_storage (STORAGE *ps)
{
	int ret = -1;
	if (ps)
	{
		LOCK (& lock_logger);
		if (loggers_global_storage)
		{
			memcpy (ps, loggers_global_storage, sizeof (STORAGE));
			ret = 0;
		}
		UNLOCK (& lock_logger);
	}
	return ret;
}

static void loggers_global_stop (const char *reason, ...)
{
	char msg_reason [PATH_MAX];
	char msg [PATH_MAX];
	va_list ap;

	loggers_set_global_state (LOGGERS_GLOBAL_STATE_CHANGING);

	loggers_update_state ();

	va_start (ap, reason);
	vsnprintf (msg_reason, sizeof (msg_reason), reason, ap);
	msg_reason [sizeof (msg_reason) - 1] = 0;
	va_end (ap);

	if (! loggers_logging_one ())
	{
		SAFE_SPRINTF (msg, sizeof (msg), LOG_TAG ": [embedded] stop loggers: %s (while no logger is logging now!)\n", msg_reason);
	}
	else
	{
		SAFE_SPRINTF (msg, sizeof (msg), LOG_TAG ": [embedded] stop loggers: %s\n", msg_reason);
	}

	file_log (msg);

	loggers_stop ();
	loggers_set_global_state (LOGGERS_GLOBAL_STATE_STOPPED);
}

static void loggers_global_start (const char *logpath_or_storage_name)
{
	const char *ptr;
	int res = 0;

	loggers_set_global_state (LOGGERS_GLOBAL_STATE_CHANGING);

	loggers_clear_errors ();

	loggers_update_state ();

	if (loggers_logging_all ())
	{
		file_log (LOG_TAG ": [embedded] start loggers (while all loggers are logging!)\n");
	}
	else
	{
		if (loggers_logging_one ())
		{
			DM ("some loggers are still logging, stop them before start!");
			loggers_stop ();
		}

		ptr = logpath_or_storage_name ? logpath_or_storage_name : db_get ("systemloggers.LogPath", pair_LogPath.value);

		res = loggers_start (ptr);
	}

	loggers_set_global_state ((res < 0) ? LOGGERS_GLOBAL_STATE_STOPPED : LOGGERS_GLOBAL_STATE_STARTED);
}

static void config_load (char *buffer, int len, int update_to_file, char *overwrite_configs)
{
	const char *ptr;
	char tmp [16];
	unsigned long value1, value2;
	int priority, compress, enabled, i;
	float weight;
	int overload;

	CONF *conf = NULL;

	if ((! buffer) || (len <= 0))
		return;

	if ((access (LOGGER_CONFIG_FILENAME, R_OK) != 0) || ((conf = conf_load_from_file (LOGGER_CONFIG_FILENAME)) == NULL))
	{
		conf = conf_new (LOGGER_CONFIG_FILENAME);

		if (! conf)
		{
			DM ("failed to create a new config!\n");
			return;
		}
	}

	/*
	 * apply overwrite configs
	 */
	if (overwrite_configs)
	{
		char *pn, *pv, *pe;

		for (pn = overwrite_configs; *pn; pn = pe)
		{
			if ((pe = strchr (pn, ':')) == NULL)
			{
				pe = & pn [strlen (pn)];
			}
			else
			{
				*pe ++ = 0;
			}

			if ((pv = strchr (pn, '=')) == NULL)
			{
				DM ("config_load: overwrite configs: [%s] is not a name=value pair!\n", pn);
				continue;
			}

			*pv ++ = 0;

			DM ("config_load: overwrite configs: [%s]=[%s]\n", pn, pv);

			conf_set (conf, pn, pv);

			SAFE_SPRINTF (buffer, len, "systemloggers.%s", pn);
			db_set (buffer, pv);
		}
	}

	ptr = conf_get (conf, pair_Version.name, "N/A");

	if (strcmp (CONF_VERSION, ptr) != 0)
	{
		DM ("different version of [%s] is detected, current [%s], required [" CONF_VERSION "]!\n", LOGGER_CONFIG_FILENAME, ptr);
	}

	/*
	 * setup global values
	 */
	for (i = 0; i < KEYWORD_COUNT; i ++)
	{
		if ((ptr = conf_get (conf, keyword_pairs [i]->name, NULL)) == NULL)
		{
			/* missing in config, use default value */
			ptr = keyword_pairs [i]->value;
			conf_set (conf, keyword_pairs [i]->name, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", keyword_pairs [i]->name);
		db_set (buffer, ptr);
	}

	/*
	 * setup storage values
	 */
	for (i = 0; storages [i].name; i ++)
	{
		/*
		 * limited size
		 */
		if ((ptr = conf_get (conf, storages [i].conf_key_limited_size_mb, NULL)) == NULL)
		{
		#if 0
			/* missing in config, use global value */
			ptr = conf_get (conf, pair_LimitedTotalLogSizeMB.name, pair_LimitedTotalLogSizeMB.value);
			conf_set (conf, storages [i].conf_key_limited_size_mb, ptr);
		#else
			/* missing in config, default auto size */
			conf_set (conf, storages [i].conf_key_limited_size_mb, STORAGE_KEY_AUTO);
			ptr = conf_get (conf, storages [i].conf_key_limited_size_mb, pair_LimitedTotalLogSizeMB.value);
		#endif
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", storages [i].conf_key_limited_size_mb);
		db_set (buffer, ptr);

		if (strcmp (ptr, STORAGE_KEY_AUTO) == 0)
		{
			value1 = 0; /* real limited size will be calculated according to the total size of logging storage */
		}
		else
		{
			value1 = (unsigned long) atol (ptr);
		}

		/*
		 * reserved size
		 */
		if ((ptr = conf_get (conf, storages [i].conf_key_reserved_size_mb, NULL)) == NULL)
		{
		#if 0
			/* missing in config, use global value */
			ptr = conf_get (conf, pair_ReservedStorageSizeMB.name, pair_ReservedStorageSizeMB.value);
			conf_set (conf, storages [i].conf_key_reserved_size_mb, ptr);
		#else
			/* missing in config, default auto size */
			conf_set (conf, storages [i].conf_key_reserved_size_mb, STORAGE_KEY_AUTO);
			ptr = conf_get (conf, storages [i].conf_key_reserved_size_mb, pair_ReservedStorageSizeMB.value);
		#endif
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", storages [i].conf_key_reserved_size_mb);
		db_set (buffer, ptr);

		if (strcmp (ptr, STORAGE_KEY_AUTO) == 0)
		{
			value2 = 0; /* real reserved size will be calculated according to the total size of logging storage */
		}
		else
		{
			value2 = (unsigned long) atol (ptr);
		}

		/*
		 * priority
		 */
		if ((ptr = conf_get (conf, storages [i].conf_key_priority, NULL)) == NULL)
		{
			/* missing in config, use default value */
			SAFE_SPRINTF (tmp, sizeof (tmp), "%d", storages [i].priority);
			ptr = tmp;
			conf_set (conf, storages [i].conf_key_priority, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", storages [i].conf_key_priority);
		db_set (buffer, ptr);

		priority = atoi (ptr);

		/*
		 * log path for excluded checking
		 */
		ptr = db_get ("systemloggers.LogPath", pair_LogPath.value);

		/*
		 * update configs
		 */
		storage_set_configs (& storages [i], value1, value2, priority, (strcmp (ptr, STORAGE_KEY_AUTO) == 0) ? 0 : (strcmp (ptr, storages [i].name) != 0));
	}

	for (i = 0; loggers [i].name; i ++)
	{
		/*
		 * limited size
		 */
		if ((ptr = conf_get (conf, loggers [i].conf_key_limited_size_mb, NULL)) == NULL)
		{
			/* missing in config, default auto size */
			conf_set (conf, loggers [i].conf_key_limited_size_mb, STORAGE_KEY_AUTO);
			ptr = conf_get (conf, loggers [i].conf_key_limited_size_mb, pair_LimitedTotalLogSizeMB.value);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_key_limited_size_mb);
		db_set (buffer, ptr);

		if (strcmp (ptr, STORAGE_KEY_AUTO) == 0)
		{
			value1 = 0; /* real limited size will be calculated each time starting logger */
		}
		else
		{
			value1 = (unsigned long) atol (ptr);
		}

		/*
		 * rotate size
		 */
		if ((ptr = conf_get (conf, loggers [i].conf_key_rotate_size_mb, NULL)) == NULL)
		{
			/* missing in config, use global value */
			ptr = conf_get (conf, pair_RotateSizeMB.name, pair_RotateSizeMB.value);
			conf_set (conf, loggers [i].conf_key_rotate_size_mb, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_key_rotate_size_mb);
		db_set (buffer, ptr);

		if (strcmp (ptr, STORAGE_KEY_AUTO) == 0)
		{
			value2 = (unsigned long) atol (LOG_DEFAULT_ROTATE_SIZE);

			DM ("config_load: auto size: [%s][rotate:%lu]\n", loggers [i].conf_key_rotate_size_mb, value2);
		}
		else
		{
			value2 = (unsigned long) atol (ptr);
		}

		/*
		 * compress
		 */
		if ((ptr = conf_get (conf, loggers [i].conf_key_compress, NULL)) == NULL)
		{
			/* missing in config, use global value */
			ptr = conf_get (conf, pair_Compress.name, pair_Compress.value);
			conf_set (conf, loggers [i].conf_key_compress, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_key_compress);
		db_set (buffer, ptr);

		compress = IS_VALUE_TRUE (ptr);

		/*
		 * enable
		 */
		if ((ptr = conf_get (conf, loggers [i].conf_key_enable, NULL)) == NULL)
		{
			/* missing in config, use default value */
			SAFE_SPRINTF (tmp, sizeof (tmp), "%s", loggers [i].enabled ? "true" : "false");
			ptr = tmp;
			conf_set (conf, loggers [i].conf_key_enable, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_key_enable);
		db_set (buffer, ptr);

		enabled = IS_VALUE_TRUE (ptr);

		/*
		 * weight
		 */
		if ((ptr = conf_get (conf, loggers [i].conf_key_weight, NULL)) == NULL)
		{
			/* missing in config, use default value */
			SAFE_SPRINTF (tmp, sizeof (tmp), "%.3f", loggers [i].weight);
			ptr = tmp;
			conf_set (conf, loggers [i].conf_key_weight, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_key_weight);
		db_set (buffer, ptr);

		weight = (float) atof (ptr);

		if (weight > 1.0)
		{
			weight = 1.0;
		}

		/*
		 * overload
		 */
		if ((ptr = conf_get (conf, loggers [i].conf_key_overload, NULL)) == NULL)
		{
			/* missing in config, use default value */
			SAFE_SPRINTF (tmp, sizeof (tmp), "%d", loggers [i].overload);
			ptr = tmp;
			conf_set (conf, loggers [i].conf_key_overload, ptr);
		}
		SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_key_overload);
		db_set (buffer, ptr);

		overload = (int) atoi (ptr);

		if (overload < 0)
		{
			overload = 0;
		}

		/*
		 * update configs
		 */
		loggers_set_configs (& loggers [i], value1, value2, compress, enabled, weight, overload);

		/*
		 * process extra configs
		 */
		if (loggers [i].conf_extras)
		{
			int idx;

			for (idx = 0; idx < loggers [i].conf_extras_count; idx ++)
			{
				if (! loggers [i].conf_extras [idx].conf_key)
					continue;

				if ((ptr = conf_get (conf, loggers [i].conf_extras [idx].conf_key, NULL)) == NULL)
				{
					/* missing in config, use default value */
					SAFE_SPRINTF (tmp, sizeof (tmp), "%s", loggers [i].conf_extras [idx].default_value);
					ptr = tmp;
					conf_set (conf, loggers [i].conf_extras [idx].conf_key, ptr);
				}

				SAFE_SPRINTF (buffer, len, "systemloggers.%s", loggers [i].conf_extras [idx].conf_key);
				db_set (buffer, ptr);

				/* update to value */
				if (loggers [i].conf_extras [idx].value && (loggers [i].conf_extras [idx].value_len > 0))
				{
					SAFE_SPRINTF (loggers [i].conf_extras [idx].value, loggers [i].conf_extras [idx].value_len, "%s", ptr);
				}

				/* keep prompt */
				SAFE_SPRINTF (buffer, len, "%s." KEY_EXTRA_CONFIG_PROMPT, loggers [i].conf_extras [idx].conf_key);
				conf_set (conf, buffer, loggers [i].conf_extras [idx].prompt);

				/* keep data type */
				SAFE_SPRINTF (buffer, len, "%s." KEY_EXTRA_CONFIG_DATA_TYPE, loggers [i].conf_extras [idx].conf_key);
				conf_set (conf, buffer, loggers [i].conf_extras [idx].data_type);

				/* keep default value */
				SAFE_SPRINTF (buffer, len, "%s." KEY_EXTRA_CONFIG_DEFAULT_VALUE, loggers [i].conf_extras [idx].conf_key);
				conf_set (conf, buffer, loggers [i].conf_extras [idx].default_value);
			}
		}
	}

	conf_set (conf, pair_Version.name, CONF_VERSION); /* update to the latest version */
	conf_dump (conf);

	if (update_to_file)
	{
		conf_save_to_file (conf);
	}

	conf_destroy (conf);

	db_set ("systemloggers.loaded", "1");

	db_dump ();
}

static void send_notification (const int enable, const char *warn_msg)
{
	const char *action = "com.htc.android.ssdtest.SYSTEM_LOGGERS_STATUS";
	const char *component = "com.htc.android.ssdtest/.NotificationReceiver";
	char path [PATH_MAX];
	char buffer [512];
	int pid = -1;

	property_get ("persist.radio.notify.stt", path, "1");

	if (path [0] != '1')
		return;

	/*
	 * for COS
	 */
	property_get ("ro.build.cos.version.release", path, "");

	if (path [0])
	{
		property_get (PROPERTY_BOOT_COMPLETED_COS, path, "0");

		if (path [0] != '1')
		{
			DM ("send_notification: COS JRT is not yet ready!\n");
			return;
		}
	}

	if (enable)
	{
		loggers_get_global_logpath (path, sizeof (path));

		DM ("send_notification: enabled,%s\n", path);
		snprintf (buffer, sizeof (buffer), "am broadcast -a %s -n %s -e status enabled -e path %s", action, component, path);
	}
	else
	{
		DM ("send_notification: disabled\n");
		snprintf (buffer, sizeof (buffer), "am broadcast -a %s -n %s -e status disabled", action, component);
	}

	if (warn_msg)
	{
		size_t len = strlen (buffer);

		if (sizeof (buffer) > len) snprintf (& buffer [len], sizeof (buffer) - len, " -e wmsg \"%s\"", warn_msg);

		DM ("send_notification: with message [%s]\n", warn_msg);
	}

	buffer [sizeof (buffer) - 1] = 0;

	/*
	 * always ignore notifcation when adb debugging is off because am command may block system()
	 */
	property_get ("persist.service.adb.enable", path, "");

	if (path [0] != '1')
	{
		property_get ("persist.sys.usb.config", path, "");

		if (strstr (path, "adb"))
		{
			path [0] = '1';
		}
	}

	if (path [0] == '1')
	{
		/*
		 * always ignore notifcation before boot completed (which means java runtime not yet ready)
		 */
		property_get (PROPERTY_BOOT_COMPLETED, path, "");

		if (path [0] == '1')
		{
			/*
			 * we don't care the return value of this command and no synchronizing concern, run it in a thread and return immediately.
			 */
			DM ("send_notification: LD_LIBRARY_PATH=[%s], BOOTCLASSPATH=[%s]\n", getenv ("LD_LIBRARY_PATH"), getenv ("BOOTCLASSPATH"));
			system_in_thread (buffer);
		}
		else
		{
			DM ("send_notification: do not send notification before boot completed.\n");
		}
	}
	else
	{
		DM ("send_notification: do not send notification when adb debugging is off.\n");
	}
}

static int done = 0;
static sem_t lock_killer;

static void *thread_killer_main (void *UNUSED_VAR (null))
{
	const char *paths [] = {
		"/data/zfllog",
		"/sdcard/zfllog",
		"/sdcard2/zfllog",
		"/mnt/usb/zfllog",
		NULL
	};

	char path [PATH_MAX];
	struct stat st;
	struct dirent *entry;
	DIR *dir;
	struct tm tm;
	time_t t, ot;
	int i, unlinked;

	prctl (PR_SET_NAME, (unsigned long) "logctl:killer", 0, 0, 0);

	if (nice (19) < 0)
	{
		DM ("killer nice failed: %s\n", strerror (errno));
	}

	/*
	 * old time, 2000/01/01
	 */
	memset (& tm, 0, sizeof (tm));
	tm.tm_year = 2000 - 1900;
	tm.tm_mon = 0;
	tm.tm_mday = 1;
	ot = mktime (& tm);

	while (! done)
	{
		if ((timed_wait (& lock_killer, 24 * 60 * 60 * 1000) < 0) && (errno != ETIMEDOUT))
		{
			DM ("killer sem_timedwait: %s\n", strerror (errno));
			continue;
		}

		file_log (LOG_TAG ": hello killer\n");

		t = time (NULL);

		for (i = 0; paths [i] != NULL; i ++)
		{
			if ((dir = opendir (paths [i])) == NULL)
				continue;

			unlinked = 0;

			while ((entry = readdir (dir)) != NULL)
			{
				if ((entry->d_type == DT_DIR) || (entry->d_name [0] == '.'))
					continue;

				if (strcmp (entry->d_name, LOG_DATA_EXT ".txt") == 0)
					continue;

				SAFE_SPRINTF (path, sizeof (path), "%s/%s", paths [i], entry->d_name);

				if (stat (path, & st) < 0)
				{
					DM ("stat [%s]: %s\n", path, strerror (errno));
					continue;
				}

				if ((t > ot) && (((time_t) st.st_mtime) < ot))
				{
					/*
					 * the modification time is too old, maybe the file was created right after booted.
					 * just touch it and keep monitoring in the following days.
					 */
					if (utime (path, NULL) < 0)
					{
						DM ("utime [%s]: %s\n", path, strerror (errno));
					}
					else
					{
						DM ("utime [%s]\n", path);
					}
					continue;
				}

				/*
				 * remove files older than 14 days
				 */
				if ((t > (time_t) st.st_mtime) && ((t - (time_t) st.st_mtime) > (time_t) (14 * 24 * 60 * 60)))
				{
					localtime_r ((time_t *) & st.st_mtime, & tm);

					errno = 0;

					if (unlink (path) < 0)
					{
						DM ("[embedded] unlink 14d [%04d%02d%02d][%s]: %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, path, strerror (errno));
					}
					else
					{
						DM ("[embedded] unlink 14d [%04d%02d%02d][%s] ok\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, path);

						unlinked ++;
					}
				}
			}

			closedir (dir);

			if (unlinked)
			{
				file_log (LOG_TAG ": unlink %d old files in [%s].\n", unlinked, paths [i]);
			}
		}
	}

end:;

	DM ("stop killer thread ...\n");
	return NULL;
}

static sem_t lock_monitor;
static int stop_by_shutdown = 0;

static void *thread_monitor_main (void *UNUSED_VAR (null))
{
	const char *properties_runtime [] = {
		PROPERTY_BOOT_COMPLETED_TOOL,	/* indicate tool java layer received boot completed intent or not */
		PROPERTY_BOOT_COMPLETED_COS,	/* indicate COS java runtime is ready or not */
		NULL
	};

	const char *attributes_runtime [] = {
		"/sys/power/wake_lock",		/* wake lock list */
		"/sys/power/wake_unlock",	/* wake unlock list */
		NULL
	};

	STORAGE storage;
	char logpath [PATH_MAX];
	struct statfs st;
	char *ptr;
	int first_time;
	int restart_counter = 0;
	int watchdog_counter = 0;
	int hello_counter = 0;
	int lowfreespace_counter = 0;
	int exp_counter = 10000;
	int local_state;
	int i;

	prctl (PR_SET_NAME, (unsigned long) "logctl:monitor", 0, 0, 0);

	first_time = 1;

	while (! done)
	{
		ptr = (char *) db_get ("systemloggers.loaded", NULL);

		if ((! ptr) || (*ptr != '1'))
		{
			config_load (logpath, sizeof (logpath), 0, NULL);
		}

		if (first_time)
		{
			timed_wait (& lock_monitor, 1000 /* 1 sec, wait for loggers ready when auto started */);
			first_time = 0;
		}
		else
		{
			timed_wait (& lock_monitor, 10000 /* 10 secs */);
		}

		local_state = loggers_get_global_state ();

		if (local_state == LOGGERS_GLOBAL_STATE_CHANGING)
		{
			DM ("logger status is changing, skip to next cycle ...");
			continue;
		}

		loggers_update_state ();

		if (++ hello_counter > 360 /* around 1 hour */)
		{
			file_log (LOG_TAG ": hello, logging=[one=%d][all=%d], local_state=%d, log=%luMB\n", loggers_logging_one (), loggers_logging_all (), local_state, loggers_get_total_size_mb ());
			hello_counter = 0;

			/*
			 * monitor these runtime values
			 */
			dump_properties_and_attributes (properties_runtime, attributes_runtime);
		}

		if (++ exp_counter > 9000 /* not exactly one day */)
		{
			if (is_expired ())
			{
				DM ("[embedded] expired!");

				if (loggers_logging_one ())
				{
					loggers_global_stop ("expired!");
					send_notification (0, "Tool was expired!");
				}

				/*
				 * remove all sockets
				 */
				local_destroy_all_sockets ();

				/*
				 * force exiting whole process
				 */
				exit (1);
			}
			exp_counter = 0;
		}

		if (local_state == LOGGERS_GLOBAL_STATE_STOPPED)
		{
			if (restart_counter)
			{
				DM ("[embedded] restart loggers, count %d!\n", restart_counter);

				loggers_global_start (NULL);

				if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
				{
					restart_counter ++;

					if (restart_counter >= 3)
					{
						file_log (LOG_TAG ": [embedded] restart loggers failed!\n");
						restart_counter = 0;
						send_notification (0, "Restart failed!");
					}
				}
				else
				{
					restart_counter = 0;
					send_notification (1, NULL);
				}
			}
			else if (loggers_logging_one ())
			{
				loggers_global_stop ("loggers should be stopped but someone is still logging, try stop again");
				send_notification (0, NULL);
			}
			continue;
		}

		restart_counter = 0;

		if (loggers_have_error_fatal () /* fatal error, restart immediately */)
		{
			loggers_error_counter = 0;
			loggers_show_errors ();
			loggers_global_stop ("need to restart loggers due to fatal error detected");
			loggers_global_start (NULL);

			if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
			{
				restart_counter ++;
				send_notification (0, "Restarting due to fatal error!");
			}
			else
			{
				restart_counter = 0;
				send_notification (1, NULL);
			}
			continue;
		}

		if (loggers_have_error () /* any other errors */)
		{
			loggers_error_counter ++;

			DM ("detected logger error! ... %d\n", loggers_error_counter);

			if (loggers_error_counter >= LOGGER_ERROR_TOLERANT_COUNT)
			{
				loggers_error_counter = 0;
				loggers_show_errors ();
				loggers_global_stop ("need to restart loggers due to error detected");
				loggers_global_start (NULL);

				if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
				{
					restart_counter ++;
					send_notification (0, "Restarting due to error detected!");
				}
				else
				{
					restart_counter = 0;
					send_notification (1, NULL);
				}
				continue;
			}
		}
		else
		{
			loggers_error_counter = 0;
		}

		if (++ watchdog_counter > 120 /* touch watch dog around 20 minutes */)
		{
			watchdog_counter = 0;

			if (loggers_touch_watchdog () != 0)
			{
				loggers_global_stop ("need to restart loggers due to watchdog barking");
				loggers_global_start (NULL);

				if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
				{
					restart_counter ++;
					send_notification (0, "Restarting due to watchdog barking!");
				}
				else
				{
					restart_counter = 0;
					send_notification (1, NULL);
				}
				continue;
			}
		}

		if (! loggers_logging_one () /* no loggers are logging */)
		{
			/*
			 * rechecking state
			 */
			local_state = loggers_get_global_state ();

			if (local_state == LOGGERS_GLOBAL_STATE_STARTED)
			{
				file_log_command_output ("mount");
				file_log_command_output ("ps -t");

				loggers_global_stop ("need to restart loggers due to loggers stopped abnormally");
				loggers_global_start (NULL);

				if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
				{
					restart_counter ++;
					send_notification (0, "Restarting due to loggers stopped abnormally!");
				}
				else
				{
					restart_counter = 0;
					send_notification (1, NULL);
				}
			}
			else if (local_state == LOGGERS_GLOBAL_STATE_CHANGING)
			{
				DM ("logger state is changing ...\n");
			}
			continue;
		}

		storage_update_all ();

		if ((loggers_get_global_logpath (logpath, sizeof (logpath)) < 0) || (logpath [0] == 0))
		{
			file_log (LOG_TAG ": no valid path found while logging!\n");
			continue;
		}

		memset (& storage, 0, sizeof (STORAGE));

		if ((loggers_get_global_storage (& storage) < 0) || (! storage.mountpoint))
		{
			file_log (LOG_TAG ": no valid storage [%s][%s] found while logging!\n", storage.name, storage.mountpoint);
			continue;
		}

		if ((storage.excluded & STORAGE_EXCLUDED_MASK_NO_FREE_SPACE) == 0)
		{
			lowfreespace_counter = 0;
		}

		if (storage.excluded == STORAGE_EXCLUDED_MASK_NO_FREE_SPACE /* have only this exclude reason */)
		{
			lowfreespace_counter ++;

			if (lowfreespace_counter < 3)
			{
				DM ("[embedded] detected low freespace [%s][%s], count %d!\n", storage.name, storage.mountpoint, lowfreespace_counter);
			}
			else
			{
				loggers_global_stop ("need to restart loggers due to storage [%s][%s] has not enough freespace!", storage.name, storage.mountpoint);
				lowfreespace_counter = 0;
				loggers_global_start (NULL);

				if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
				{
					restart_counter ++;
					send_notification (0, "Restarting due to low freespace!");
				}
				else
				{
					restart_counter = 0;
					send_notification (1, NULL);
				}
				continue;
			}
		}
		else if (storage.excluded /* have other exclude reason */)
		{
			loggers_global_stop ("need to restart loggers due to storage [%s][%s] was excluded! (%s)",
					storage.name, storage.mountpoint, storage_excluded_text (storage.excluded));

		#if 0
			if (storage.excluded & (STORAGE_EXCLUDED_MASK_UNMOUNT | STORAGE_EXCLUDED_MASK_READONLY))
			{
				file_log_command_output ("df");
				file_log_command_output ("ls -l -a %s", storage.mountpoint);
			}
		#endif

			lowfreespace_counter = 0;
			loggers_global_start (NULL);

			if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
			{
				restart_counter ++;
				send_notification (0, "Restarting due to storage was excluded!");
			}
			else
			{
				restart_counter = 0;
				send_notification (1, NULL);
			}
			continue;
		}

		loggers_process_rotation_and_limitation ();
	}

end:;
	DM ("stop monitoring thread ...\n");
	return NULL;
}

static int do_dumpsys (char *iobuf, int iobuflen)
{
	char buf [PATH_MAX];
	int ret;

	strncpy (buf, iobuf, sizeof (buf) - 2);
	buf [sizeof (buf) - 2] = 0;

	if (storage_select_logging_path (buf, sizeof (buf), NULL) < 0)
	{
		SAFE_SPRINTF (buf, sizeof (buf), "%s", LOG_DIR);
	}

	snprintf (iobuf, iobuflen - 1, "%sdumpsys_" TAG_DATETIME ".txt", buf);
	iobuf [iobuflen - 1] = 0;
	str_replace_tags (iobuf);

	snprintf (buf, sizeof (buf) - 1, "/system/bin/dumpsys > %s", iobuf);
	buf [sizeof (buf) - 1] = 0;

	DM ("run [%s]\n", buf);

	ret = (system (buf) == 0) ? 0 : -1;

	DM ("end of [%s]\n", buf);
	return ret;
}

static void handle_local_command_getpath (char *buffer, int len)
{
	loggers_update_state ();

	if (loggers_logging_one ())
	{
		DM ("read from logging path\n");
		loggers_get_global_logpath (buffer, len);
	}
	else
	{
		DM ("read from config setting\n");
		SAFE_SPRINTF (buffer, len, "%s", db_get ("systemloggers.LogPath", pair_LogPath.value));
		storage_select_logging_path (buffer, len, NULL);
	}
}

static void handle_local_command_getlogfiles (char *buffer, int len)
{
	loggers_get_log_files (buffer, len);

	if (buffer [0] == 0)
	{
		SAFE_SPRINTF (buffer, len, "%s", "Cannot find log files while there is no error occurred! Please make sure at least one logger is enabled.");
	}
}

static void handle_local_command_stop (char *buffer, int len)
{
	loggers_global_stop ("got stop command: %s", buffer [0] ? buffer : "");

	if (strcmp (buffer, "shutdown") == 0)
		stop_by_shutdown = 1;

	send_notification (0, NULL);

	SAFE_SPRINTF (buffer, len, "%d", 0);
}

static void handle_local_command_run (char *buffer, int len)
{
	int ret = 0;

	stop_by_shutdown = 0;

	loggers_clear_last_error ();

	loggers_global_start (buffer [0] ? buffer : NULL);

	if (loggers_get_global_state () == LOGGERS_GLOBAL_STATE_STOPPED)
	{
		loggers_get_last_error (buffer, len);

		send_notification (0, (buffer [0] == 0) ? "Start failed!" : buffer);
	}
	else
	{
		handle_local_command_getlogfiles (buffer, len);

		if (buffer [0] == '/')
		{
			send_notification (1, NULL);
		}
		else
		{
			/* take it as a failure case, stop loggers */
			if (buffer [0] == 0)
			{
				SAFE_SPRINTF (buffer, len, "%s", "No log file can be found!");
			}
			loggers_global_stop (buffer);
		}
	}

	if (buffer [0] == 0)
	{
		SAFE_SPRINTF (buffer, len, "%s", "N/A");
	}
}

static void handle_local_command_mount (char *buffer, int len)
{
	char tmp [PATH_MAX + 16];
	int mounted;
	int autorun;
	int autoswitch;

	storage_update_all ();

	STORAGE logging_storage, *pmount_storage;

	datatok (buffer, tmp);
	mounted = (tmp [0] == '0') ? 0 : 1;

	datatok (buffer, tmp);
	pmount_storage = storage_get_by_name_or_path_nolock (tmp);

	if (! pmount_storage)
	{
		DM ("MOUNT mounted=%d [%s], unknown storage, ignored!\n", mounted, tmp);
		SAFE_SPRINTF (buffer, len, "%d", 0);
		return;
	}

	DM ("MOUNT .1. mounted=%d [%s], storage [%s][%s]\n", mounted, tmp, pmount_storage->name, pmount_storage->mountpoint);

	config_load (buffer, len, 0, NULL);

	SAFE_SPRINTF (buffer, len, "%s", db_get ("systemloggers.LogPath", pair_LogPath.value));
	autoswitch = (strcmp (buffer, STORAGE_KEY_AUTO) == 0);

	autorun = IS_VALUE_TRUE (db_get ("systemloggers.AutoStart", pair_AutoStart.value));

	loggers_update_state ();

	memset (& logging_storage, 0, sizeof (logging_storage));

	if (loggers_logging_one ())
	{
		if (loggers_get_global_storage (& logging_storage) < 0)
			logging_storage.name = NULL;
	}

	DM ("MOUNT .2. autorun=%d, autoswitch=%d, islogging=%d [%s][%s], stopped by shutdown=%d\n",
		autorun, autoswitch, loggers_logging_one (), logging_storage.name, logging_storage.mountpoint, stop_by_shutdown);

	file_log (LOG_TAG ": [embedded] mounted=%d [%s], storage [%s][%s], autorun=%d, autoswitch=%d, islogging=%d [%s][%s], stopped by shutdown=%d\n",
		mounted, tmp, pmount_storage->name, pmount_storage->mountpoint,
		autorun, autoswitch, loggers_logging_one (), logging_storage.name, logging_storage.mountpoint, stop_by_shutdown);

	if (stop_by_shutdown)
	{
		DM ("MOUNT .3. ignore this event due to loggers was stopped by shutdown\n");
	}
	else if (! autoswitch)
	{
		/* do not need to auto switch path */
		if (mounted)
		{
			if (autorun && (! loggers_logging_one ()))
			{
				if (strcmp (buffer, pmount_storage->name) == 0)
				{
					DM ("MOUNT .3. run, logpath=[%s]\n", buffer);
					handle_local_command_run (buffer, len);
				}
			}
			else
			{
				DM ("MOUNT .3. ignore this event due to %s\n", autorun ? "not autoswitch and logging" : "not autorun");
			}
		}
		else if (loggers_logging_one ())
		{
			/* only when unmounting the logging storage we need to handle the unmount status */
			if (logging_storage.name && (strcmp (pmount_storage->name, logging_storage.name) == 0))
			{
				DM ("MOUNT .3. stop\n");
				loggers_global_stop ("media unmounted");
				send_notification (0, NULL);
			}
			else
			{
				DM ("MOUNT .3. ignore this event due to not logging to [%s]\n", logging_storage.name);
			}
		}
	}
	else
	{
		/* auto switch path */
		int change = autorun;

		if (loggers_logging_one ())
		{
			if (mounted)
			{
				change = (storage_compare_priority (pmount_storage->name, logging_storage.name) > 0);
			}
			else
			{
				change = logging_storage.name ? (strcmp (pmount_storage->name, logging_storage.name) == 0) : 1;
			}
		}

		DM ("MOUNT .3. need to change=%d, [logging:%s][mount:%d:%s]\n", change, logging_storage.name, mounted, pmount_storage->name);

		file_log (LOG_TAG ": need to change=%d, [logging:%s][mount:%d:%s]\n", change, logging_storage.name, mounted, pmount_storage->name);

		if (change)
		{
			if (loggers_logging_one ())
			{
				DM ("MOUNT .3.1. stop\n");
				loggers_global_stop ("change log path");
				//send_notification (0, NULL);
			}

			if (mounted)
			{
				storage_select_logging_path (buffer, len, NULL);
			}
			else
			{
				/* storage may be not yet un-mounted when we receive Intent.ACTION_MEDIA_EJECT, exclude it */
				storage_select_logging_path (buffer, len, pmount_storage->name);
			}

			DM ("MOUNT .3.2. run, logpath=[%s]\n", buffer);
			handle_local_command_run (buffer, len);
		}
	}
	DM ("MOUNT .4. end\n");

	SAFE_SPRINTF (buffer, len, "%d", 0);
}

static void handle_local_command_clearlogdir (char *buffer, int len)
{
	/*
	 * this is a generic function, do not check the system loggers status here
	 */
	char tmp [PATH_MAX + 16];

	if (buffer [0] && (buffer [0] != ':'))
	{
		GLIST_NEW (patterns);
		char *ph, *pe;

		datatok (buffer, tmp);

		if (storage_select_logging_path (tmp, sizeof (tmp), NULL) < 0)
			tmp [0] = 0;

		if (tmp [0])
		{
			file_log (LOG_TAG ": [embedded][clearlogdir][%s][%s]\n", tmp, buffer);

			for (ph = pe = buffer; *ph; pe ++)
			{
				if ((*pe == ':') || (*pe == 0))
				{
					char c = *pe;

					*pe = 0;

					if (ph != pe)
					{
						//DM ("pattern [%s]\n", ph);
						glist_add (& patterns, ph);
					}

					if (c == 0)
						break;

					ph = pe + 1;
				}
			}

			dir_clear (tmp, patterns);

			glist_clear (& patterns, NULL);
		}
	}
	SAFE_SPRINTF (buffer, len, "%d", 0);
}

static void handle_local_command_movelog (char *buffer, int len)
{
	file_log (LOG_TAG ": [embedded] handle_local_command_movelog, buffer[%s].\n", buffer);

	char srcpath [PATH_MAX];
	char dstpath [PATH_MAX];
	struct dirent *entry;
	DIR *dir;
	int err;
	unsigned long excluded;

	datatok (buffer, srcpath);
	datatok (buffer, dstpath);

	SAFE_SPRINTF (buffer, len, "zfllog_%s/", srcpath);

	if ((excluded = storage_query_log_path_and_excluded_state (srcpath, sizeof (srcpath))) == (unsigned long) -1)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is not found!", srcpath);
		return;
	}

	if (excluded & STORAGE_EXCLUDED_MASK_UNMOUNT)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is not mounted!", srcpath);
		return;
	}

	if (excluded & STORAGE_EXCLUDED_MASK_READONLY)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is ready only!", srcpath);
		return;
	}

	if ((excluded = storage_query_log_path_and_excluded_state (dstpath, sizeof (dstpath))) == (unsigned long) -1)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is not found!", dstpath);
		return;
	}

	if (excluded & STORAGE_EXCLUDED_MASK_UNMOUNT)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is not mounted!", dstpath);
		return;
	}

	if (excluded & STORAGE_EXCLUDED_MASK_READONLY)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is ready only!", dstpath);
		return;
	}

	if (excluded & STORAGE_EXCLUDED_MASK_NO_FREE_SPACE)
	{
		SAFE_SPRINTF (buffer, len, "Storage [%s] is no free space!", dstpath);
		return;
	}

	strncat (dstpath, buffer, sizeof (dstpath) - strlen (dstpath) - 1);
	dstpath [sizeof (dstpath) - 1] = 0;

	mkdir (dstpath, DEFAULT_DIR_MODE);

	if (! dir_exists (dstpath))
	{
		SAFE_SPRINTF (buffer, len, "Cannot access [%s]!", dstpath);
		return;
	}

	if ((dir = opendir (srcpath)) == NULL)
	{
		SAFE_SPRINTF (buffer, len, "Failed on [%s]: %s", srcpath, strerror (errno));
		return;
	}

	err = FILE_COPY_SUCCEEDED;

	while ((entry = readdir (dir)) != NULL)
	{
		if ((strcmp (entry->d_name, ".") == 0) || (strcmp (entry->d_name, "..") == 0))
			continue;

		SAFE_SPRINTF (buffer, len, "%s%s", srcpath, entry->d_name);

		switch (loggers_check_file_type (buffer))
		{
		case LOGGER_FILETYPE_FILE:
		case LOGGER_FILETYPE_DATA:
			if ((err = file_copy (srcpath, dstpath, entry->d_name)) >= 0)
			{
				DM ("unlink copied [%s] ...\n", buffer);
				unlink (buffer);
			}
			break;

		case LOGGER_FILETYPE_LOGGING_FILE:
		case LOGGER_FILETYPE_LOGGING_DATA:
			err = file_copy (srcpath, dstpath, entry->d_name);
			break;

		default:
			/* copy other files */
			if (strncmp (entry->d_name, LOG_DATA_EXT ".txt", strlen (LOG_DATA_EXT ".txt")) == 0)
			{
				err = file_copy (srcpath, dstpath, entry->d_name);
			}
		}

		if (err < 0)
		{
			SAFE_SPRINTF (buffer, len, "Failed on [%s]: %s", (err == FILE_COPY_ERROR_SOURCE) ? srcpath : dstpath, strerror (errno));
			closedir (dir);
			return;
		}
	}

	closedir (dir);

	SAFE_SPRINTF (buffer, len, "%d", 0);
}

static void handle_local_command_statfs (char *buffer, int len)
{
	struct statfs st;

	if (buffer [0] != '/')
	{
		dir_select_log_path (buffer, len);
	}

	if (statfs_nointr (buffer, & st) < 0)
	{
		DM ("statfs_nointr [%s]: %s\n", buffer, strerror (errno));
	}
	else
	{
	#if 0
		//bionic/libc/include/sys/vfs.h
		struct statfs {
			uint32_t        f_type;     /* type of file system (see below) */
			uint32_t        f_bsize;    /* optimal transfer block size */
			uint64_t        f_blocks;   /* total data blocks in file system */
			uint64_t        f_bfree;    /* free blocks in fs */
			uint64_t        f_bavail;   /* free blocks avail to non-superuser */
			uint64_t        f_files;    /* total file nodes in file system */
			uint64_t        f_ffree;    /* free file nodes in fs */
			__kernel_fsid_t f_fsid;     /* file system id */
			uint32_t        f_namelen;  /* maximum length of filenames */
			uint32_t        f_frsize;
			uint32_t        f_spare[5];
		};
		free_size = (unsigned long long) (((unsigned long long) st.f_bfree * (unsigned long long) st.f_bsize));
	#endif
		snprintf (buffer, len,
			"f_type=%u:"
			"f_bsize=%u:"
			"f_blocks=%llu:"
			"f_bfree=%llu:"
			"f_bavail=%llu:"
			"f_files=%llu:"
			"f_ffree=%llu:"
			"f_namelen=%u:"
			, (unsigned int) st.f_type
			, (unsigned int) st.f_bsize
			, (unsigned long long int) st.f_blocks
			, (unsigned long long int) st.f_bfree
			, (unsigned long long int) st.f_bavail
			, (unsigned long long int) st.f_files
			, (unsigned long long int) st.f_ffree
			, (unsigned int) st.f_namelen
			);
		buffer [len - 1] = 0;
	}

	if (buffer [0] == 0)
	{
		SAFE_SPRINTF (buffer, len, "%d", 0);
	}
}

static void handle_local_command_syncconf (char *buffer, int len)
{
	loggers_update_state ();

	if (loggers_logging_one ())
	{
		DM ("cannot syncconf due to loggers are running!\n");
		SAFE_SPRINTF (buffer, len, "%d", -1);
		return;
	}

	config_load (buffer, len, 0, NULL);

	SAFE_SPRINTF (buffer, len, "%d", 0);
}

static void handle_local_command_genconf (char *buffer, int len)
{
	char *ptr = NULL;

#if 0
	loggers_update_state ();

	if (loggers_logging_one ())
	{
		DM ("cannot genconf due to loggers are running!\n");
		SAFE_SPRINTF (buffer, len, "%d", -1);
		return;
	}
#endif

	if (buffer [0])
	{
		ptr = strdup (buffer);
	}

	config_load (buffer, len, 1, ptr);

	if (ptr)
	{
		free (ptr);
	}

	SAFE_SPRINTF (buffer, len, "%d", 0);
}

static void sig_dummy (int sig)
{
	DM ("got dummy signal %d\n", sig);
}

static void sig_exit (int sig)
{
	loggers_global_stop ("got signal %d", sig);
	send_notification (0, NULL);
	exit (0);
}

static int invalid_fd (int fd)
{
	if (fcntl (fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		DM ("fcntl fd=%d: %s\n", fd, strerror (errno));
		return 1;
	}
	return 0;
}

int logctl_main (int server_socket)
{
	pthread_t thread_monitor = (pthread_t) -1;
	pthread_t thread_killer = (pthread_t) -1;

	char buffer [PATH_MAX + 16];
	int ret = 0;
	int commfd = -1;
	int i;

	DM ("logctl " VERSION "\n");

	if (signal (SIGTERM, sig_exit) == SIG_ERR) { DM ("cannot handle SIGTERM signal!\n"); return -1; }
	if (signal (SIGHUP,  sig_exit) == SIG_ERR) { DM ("cannot handle SIGHUP signal!\n"); return -1; }
	if (signal (SIGALRM, sig_dummy) == SIG_ERR) { DM ("cannot handle SIGALRM signal!\n"); }
	if (signal (SIGUSR1, sig_dummy) == SIG_ERR) { DM ("cannot handle SIGUSR1 signal!\n"); }
	if (signal (SIGUSR2, sig_dummy) == SIG_ERR) { DM ("cannot handle SIGUSR2 signal!\n"); }

	session_init (buffer, sizeof (buffer));
	loggers_init ();
	storage_init ();
	socket_commands_init ();
	config_load (buffer, sizeof (buffer), 0, NULL);

	if (partition_is_autostart_bit_set ())
	{
		file_log (LOG_TAG ": force auto start!\n");
		db_set ("systemloggers.AutoStart", "true");
	}

	if (IS_VALUE_TRUE (db_get ("systemloggers.AutoStart", pair_AutoStart.value)))
	{
		file_log (LOG_TAG ": auto start\n");
		buffer [0] = 0;
		handle_local_command_run (buffer, sizeof (buffer));
	}
	else
	{
		send_notification (0, NULL);
	}

	sem_init (& lock_monitor, 0, 0);

	if (pthread_create (& thread_monitor, NULL, thread_monitor_main, NULL) < 0)
	{
		DM ("[embedded] pthread_create monitor main: %s\n", strerror (errno));
		thread_monitor = (pthread_t) -1;
	}

	sem_init (& lock_killer, 0, 0);

	if (pthread_create (& thread_killer, NULL, thread_killer_main, NULL) < 0)
	{
		DM ("[embedded] pthread_create killer main: %s\n", strerror (errno));
		thread_killer = (pthread_t) -1;
	}

	while (! done)
	{
		DM ("waiting connection ...\n");

	#if USE_LOCAL_SOCKET
		commfd = local_wait_for_connection (server_socket);
	#else
		commfd = wait_for_connection (server_socket);
	#endif

		if (commfd < 0)
		{
			DM ("accept client connection failed!\n");
			continue;
		}

		DM ("connection established.\n");

		for (;;)
		{
			memset (buffer, 0, sizeof (buffer));

			ret = read_nointr (commfd, buffer, sizeof (buffer) - 1);

			if (ret <= 0)
			{
				DM ("read_nointr command error (%d)! close connection!\n", ret);
				break;
			}

			DM ("read_nointr command [%s].\n", buffer);

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				done = 1;
				sem_post (& lock_monitor);
				break;
			}

			if (CMP_CMD (buffer, CMD_HELP))
			{
				int i, len;
				for (i = 0; i < LOCAL_CMD_COUNT; i ++)
				{
					strncpy (buffer, socket_commands [i], sizeof (buffer) - 2);
					strcat (buffer, "\n");
					buffer [sizeof (buffer) - 1] = 0;

					len = strlen (buffer);

					if (write_nointr (commfd, buffer, len) != (ssize_t) len)
					{
						DM ("send response [%s] to client failed: %s\n", socket_commands [i], strerror (errno));
					}
				}
				continue;
			}

			if (CMP_CMD (buffer, CMD_GETVER))
			{
				SAFE_SPRINTF (buffer, sizeof (buffer), "%s", VERSION);

				DM ("send version [%s].\n", buffer);

				if (write_nointr (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
				{
					DM ("send response [%s] to client failed: %s\n", buffer, strerror (errno));
				}
				continue;
			}

			/*
			 * buffer is taken as both command and response buffer
			 */
			ret = socket_commands_index (socket_commands, LOCAL_CMD_COUNT, buffer);

			/* remove command body */
			if (ret >= 0) MAKE_DATA (buffer, socket_commands [ret]);

			switch (ret)
			{
			case LOCAL_CMD_GETPATH:
				handle_local_command_getpath (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_RUN:
				handle_local_command_run (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_STOP:
				handle_local_command_stop (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_ISLOGGING:
				loggers_update_state ();
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", loggers_logging_one ());
				break;
			case LOCAL_CMD_GETLOGFILES:
				handle_local_command_getlogfiles (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_GETLASTERROR:
				loggers_get_last_error (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_MOUNT:
				handle_local_command_mount (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_CANWRITE:
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", (dir_write_test (buffer) < 0) ? 0 : 1);
				break;
			case LOCAL_CMD_CLEARLOGDIR:
				handle_local_command_clearlogdir (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_CLEARLOG:
				loggers_clear_log_files ();
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", 0);
				break;
			case LOCAL_CMD_MOVELOG:
				handle_local_command_movelog (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_BUGREPORT:
				if (buffer [0] == 0)
				{
					SAFE_SPRINTF (buffer, sizeof (buffer), "%s", LOG_DIR);
				}

				do_bugreport (buffer, sizeof (buffer));

				if (buffer [0] == 0)
				{
					SAFE_SPRINTF (buffer, sizeof (buffer), "%d", -1);
				}
				break;
			case LOCAL_CMD_DUMPSYS:
				do_dumpsys (buffer, sizeof (buffer));
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", 0);
				break;
			case LOCAL_CMD_GETDEBUG:
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", debug_more);
				break;
			case LOCAL_CMD_SETDEBUG:
				debug_more = (buffer [0] == '1');
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", 0);
				break;
			case LOCAL_CMD_GETPHONESTORAGE:
				SAFE_SPRINTF (buffer, sizeof (buffer), "%s", storage_get_mountpoint_by_name (STORAGE_KEY_PHONE));
				break;
			case LOCAL_CMD_GETEXTSTORAGE:
				SAFE_SPRINTF (buffer, sizeof (buffer), "%s", storage_get_mountpoint_by_name (STORAGE_KEY_EXTERNAL));
				break;
			case LOCAL_CMD_GETUSBSTORAGE:
				SAFE_SPRINTF (buffer, sizeof (buffer), "%s", storage_get_mountpoint_by_name (STORAGE_KEY_USB));
				break;
			case LOCAL_CMD_STATFS:
				handle_local_command_statfs (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_SYNCCONF:
				handle_local_command_syncconf (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_GENCONF:
				handle_local_command_genconf (buffer, sizeof (buffer));
				break;
			case LOCAL_CMD_STORAGEINIT:
				storage_reinit ();
				break;
			default:
				DM ("unknown command [%s]!\n", buffer);
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", -1);
			}

			/* command response */
			if (buffer [0] == 0)
			{
				SAFE_SPRINTF (buffer, sizeof (buffer), "%d", -1);
			}

			DM ("send response [%s].\n", buffer);

			if (write_nointr (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		shutdown (commfd, SHUT_RDWR);
		close_nointr (commfd);
		commfd = -1;

		ret = 0;
	}

	if (thread_killer != (pthread_t) -1)
	{
		/* quit killer thread */
		sem_post (& lock_killer);
		pthread_join (thread_killer, NULL);
		thread_killer = (pthread_t) -1;
	}

	if (thread_monitor != (pthread_t) -1)
	{
		/* quit monitor thread */
		sem_post (& lock_monitor);
		pthread_join (thread_monitor, NULL);
		thread_monitor = (pthread_t) -1;
	}

	done = 0;
	return ret;
}

static void debug_dump (const char *UNUSED_VAR (msg), const char *logfile)
{
#if 0
	/*
	 * dump every time
	 */
	char buffer [1024];

	SAFE_SPRINTF (buffer, sizeof (buffer), "%s.dd.device", logfile);

	if (access (buffer, F_OK) == 0)
	{
		/* dumped before, abort */
		DM ("dumped file found, skip debug dump\n");
		return;
	}

	SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/logcat -v threadtime -b main -d -f %s.dd.device", logfile);
	DM ("%s\n", buffer);
	system (buffer);

	SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/dmesg > %s.dd.kernel", logfile);
	DM ("%s\n", buffer);
	system (buffer);

	SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/ps -t > %s.dd.ps", logfile);
	DM ("%s\n", buffer);
	system (buffer);
#else
	/*
	 * dump once
	 */
	char buffer [PATH_MAX];
	char tag_with_path [PATH_MAX];
	char *ptr;

	SAFE_SPRINTF (tag_with_path, sizeof (tag_with_path), "%s", logfile);

	if (! tag_with_path [0])
		return;

	ptr = strchr (tag_with_path, '_');

	if (ptr)
		*ptr = 0;

	SAFE_SPRINTF (buffer, sizeof (buffer), "%s.dd.device", tag_with_path);

	if (access (buffer, F_OK) == 0)
	{
		/* dumped before, abort */
		DM ("dumped file [%s] found, skip debug dump\n", buffer);
		return;
	}

	SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/logcat -v threadtime -b main -d -f %s.dd.device", tag_with_path);
	DM ("%s\n", buffer);
	system (buffer);

	SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/dmesg > %s.dd.kernel", tag_with_path);
	DM ("%s\n", buffer);
	system (buffer);

	SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/ps -t > %s.dd.ps", tag_with_path);
	DM ("%s\n", buffer);
	system (buffer);
#endif
}

int logger_common_update_state (const char *name, int state, pid_t pid, pthread_t tid, const char *log_filename, int check_pipe)
{
	char s = 'S';

	state &= ~(LOGGER_STATE_MASK_LOGGING | LOGGER_STATE_MASK_ERROR_PRSTAT | LOGGER_STATE_MASK_ERROR_MISSING);

	/*
	 * update logging state
	 */
	if (pid > 0)
	{
		s = get_pid_stat (pid);

		if (IS_PROCESS_STATE_UNKNOWN (s))
		{
			file_log (LOG_TAG ": [embedded] detected %s cannot stat pid [%d]!\n", name, pid);

			state |= LOGGER_STATE_MASK_ERROR_PRSTAT;
		}
	}
	if (tid != (pthread_t) -1)
	{
		s = get_pid_stat (tid);

		if (! is_thread_alive (tid))
		{
			file_log (LOG_TAG ": [embedded] detected %s cannot stat tid [0x%X]!\n", name, tid);

			state |= LOGGER_STATE_MASK_ERROR_PRSTAT;
		}
	}
	if ((state & LOGGER_STATE_MASK_ERROR_PRSTAT) == 0)
	{
		state |= LOGGER_STATE_MASK_LOGGING;
	}

	/*
	 * update process state
	 */
	if (IS_PROCESS_STATE_ZOMBIE (s))
	{
		if (! (state & LOGGER_STATE_MASK_ERROR_ZOMBIE))
		{
			file_log (LOG_TAG ": [embedded] detected %s became zombie!\n", name);

			debug_dump ("zombie is found", log_filename);
		}
		state |= LOGGER_STATE_MASK_ERROR_ZOMBIE;
	}
	else if (state & LOGGER_STATE_MASK_ERROR_ZOMBIE)
	{
		file_log (LOG_TAG ": [embedded] detected %s resumed from zombie!\n", name);

		state &= ~LOGGER_STATE_MASK_ERROR_ZOMBIE;
	}

	if (IS_PROCESS_STATE_DEFUNCT (s))
	{
		if (! (state & LOGGER_STATE_MASK_ERROR_DEFUNCT))
		{
			file_log (LOG_TAG ": [embedded] detected %s became defunct!\n", name);

			/* trigger /proc/sysrq-trigger */
		}
		state |= LOGGER_STATE_MASK_ERROR_DEFUNCT;
	}
	else if (state & LOGGER_STATE_MASK_ERROR_DEFUNCT)
	{
		file_log (LOG_TAG ": [embedded] detected %s resumed from defunct!\n", name);

		state &= ~LOGGER_STATE_MASK_ERROR_DEFUNCT;
	}

	/*
	 * check missing file
	 */
	if (log_filename && log_filename [0] && (access (log_filename, R_OK | W_OK) != 0))
	{
		DM ("[embedded] access [%s]: %s\n", log_filename, strerror (errno));

		if (access (log_filename, F_OK) == 0)
		{
			/*
			 * file existing but cannot r/w, try to show the mode
			 */
			struct stat st;

			if (stat (log_filename, & st) < 0)
			{
				DM ("[embedded] stat [%s]: %s\n", log_filename, strerror (errno));
			}
			else
			{
				DM ("[embedded] stat [%s]: mode %03o\n", log_filename, st.st_mode);
			}
		}
		state |= LOGGER_STATE_MASK_ERROR_MISSING;
	}
	else if (check_pipe)
	{
		char pipe [PATH_MAX];

		logger_common_pipe_filename (name, pipe, sizeof (pipe));

		if (logger_common_pipe_validate (name, pipe, 1) < 0)
		{
			DM ("[embedded] pipe [%s] is missing!\n", pipe);

			state |= LOGGER_STATE_MASK_ERROR_MISSING;
		}
	}

	return state;
}

int logger_common_generate_new_file (const char *name, const char *file_prefix, char *buffer, int len)
{
	char path_with_slash [PATH_MAX];
	char timestamp [TAG_DATETIME_LEN + 1];
	char c;
	int fd;

	if ((! file_prefix) || (! buffer))
		return EINVAL;

	if (strlen (file_prefix) >= LOGDATA_PREFIX_BUFLEN)
	{
		DM ("%s prefix [%s] is too long! (> %d)\n", name, file_prefix, LOGDATA_PREFIX_BUFLEN);
		return EOVERFLOW;
	}

	loggers_get_global_logpath_nolock (path_with_slash, sizeof (path_with_slash));

	c = dir_get_storage_code (path_with_slash);

	SAFE_SPRINTF (timestamp, sizeof (timestamp), "%s", TAG_DATETIME);

	str_replace_tags (timestamp);

#if 0
	/* do not include serialno */
	snprintf (buffer, len, "%s%s%c%04u_%s_%s_%s_%s.txt",
			path_with_slash,
			file_prefix,
			c,
			(unsigned int) session_get_current_sn (),
			timestamp,
			session_devinfo,
			session_prdinfo,
			session_rominfo);
#else
	/* include serialno */
	snprintf (buffer, len, "%s%s%c%04u_%04u_%s_%s_%s_%s.txt",
			path_with_slash,
			file_prefix,
			c,
			(unsigned int) session_get_current_sn (),
			(unsigned int) loggers_increase_serialno_by_name_nolock (name),
			timestamp,
			session_devinfo,
			session_prdinfo,
			session_rominfo);
#endif
	buffer [len - 1] = 0;

	DM ("[embedded] %s new file [%s]\n", name, buffer);

	if ((fd = open_nointr (buffer, O_RDWR | O_CREAT, LOG_FILE_MODE)) < 0)
	{
		int e = errno;
		file_log (LOG_TAG ": [embedded] %s open new file [%s] failed: %s\n", name, buffer, strerror (e));
		return e;
	}

	close_nointr (fd);

	if ((fd = logger_common_logdata_open (name, path_with_slash, file_prefix)) < 0)
	{
		file_log (LOG_TAG ": %s open logdata failed! try going on ...\n", name);
	}
	else
	{
		logger_common_logdata_add_file (fd, name, path_with_slash, buffer);
		close_nointr (fd);
	}
	return 0;
}

int logger_common_need_rotate (const char *name, const char *log_filename, unsigned long rotate_size_mb)
{
	unsigned long long size = (unsigned long long) file_size (log_filename);
	unsigned long long rsize = (unsigned long long) rotate_size_mb * MB;

	if (size >= rsize)
	{
		DM ("[embedded] %s [%s] needs rotate (%llu > %llu)\n", name, log_filename, size, rsize);
		return 1;
	}
	return 0;
}

void logger_common_pipe_filename (const char *name, char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, DAT_DIR ".%s.pipe", name);
}

int logger_common_pipe_validate (const char *UNUSED_VAR (name), const char *pipefile, int show_log)
{
	struct stat st;

	if (! pipefile)
		return -1;

	if (lstat (pipefile, & st) < 0)
	{
		if (show_log)
		{
			DM ("lstat [%s]: %s\n", pipefile, strerror (errno));
		}
		return -1;
	}

	if ((! S_ISFIFO (st.st_mode)) || ((st.st_mode & 0777) != 0600))
	{
		if (show_log)
		{
			DM ("wrong pipe [%s]: mode=%o\n", pipefile, st.st_mode);
		}
		return -1;
	}

	return 0;
}

int logger_common_pipe_create (const char *name, const char *pipefile)
{
	if (logger_common_pipe_validate (name, pipefile, 1) < 0)
	{
		DM ("create pipe [%s]\n", pipefile);

		unlink (pipefile);

		if (mkfifo (pipefile, 0600) < 0)
		{
			file_log (LOG_TAG ": mkfifo [%s]: %s\n", pipefile, strerror (errno));
			return -1;
		}
	}
	return 0;
}

int logger_common_pipe_open (const char *name, const char *pipefile)
{
	int fd;
	int create = 0;

	if (logger_common_pipe_create (name, pipefile) < 0)
		return -1;

	fd = open_nointr (pipefile, O_RDONLY, 0600);

	if (fd < 0)
	{
		file_log (LOG_TAG ": open [%s]: %s\n", pipefile, strerror (errno));
	}
	else
	{
		fcntl (fd, F_SETFD, FD_CLOEXEC);
	}
	return fd;
}

int logger_common_pipe_close (const char *UNUSED_VAR (name), int fd)
{
	if (fd < 0)
		return -1;

	return close_nointr (fd);
}

int logger_common_pipe_have_data (const char *UNUSED_VAR (name), int fd)
{
	if (fd < 0)
		return 0;

	return poll_check_data (fd); /* return 1 or 0 */
}

int logger_common_pipe_read (const char *UNUSED_VAR (name), int fd, char *buffer, size_t count)
{
	if (fd < 0)
		return -1;

	return (int) read_nointr (fd, buffer, count);
}

void *logger_common_pipe_router_thread (void *pipeinfo)
{
	PIPEINFO *pi = (PIPEINFO *) pipeinfo;

	char buffer [2048];
	int fd, count, i;

	time_t pre_time;
	time_t cur_time;
	double dif_time = 0;
	int buf_count = 0;
	char state [256];

	time(&cur_time);
	pre_time = cur_time;

	if ((! pi) || (! pi->name) || (! pi->log_filename))
	{
		DM ("router thread got invalid argument! (%p)\n", pipeinfo);
		return NULL;
	}

	DM ("%s router thread begin ...\n", pi->name);

	SAFE_SPRINTF (buffer, sizeof (buffer), "%s:router", pi->name);

	prctl (PR_SET_NAME, (unsigned long) buffer, 0, 0, 0);

	for (fd = -1; pi->pipefd != -1;)
	{
		if ((fd == -1) || (pi->rotated))
		{
			if (fd != -1)
			{
				close_nointr (fd);
			}

			pi->rotated = 0;
			pi->error = PIPE_ROUTER_ERROR_NONE;

			if ((fd = open_nointr (pi->log_filename, O_WRONLY | O_CREAT, LOG_FILE_MODE)) < 0)
			{
				file_log (LOG_TAG ": router open [%s]: %s\n", pi->log_filename, strerror (errno));
				fd = -1;
				pi->error = PIPE_ROUTER_ERROR_OPEN;
				break;
			}
		}

		count = logger_common_pipe_read (pi->name, pi->pipefd, buffer, sizeof (buffer));

		if (count < 0)
		{
			file_log (LOG_TAG ": router read pipe: %s\n", strerror (errno));
			pi->error = PIPE_ROUTER_ERROR_READ;
			break;
		}

		if (count == 0)
		{
			file_log (LOG_TAG ": router read pipe: 0 byte!\n");
			pi->error = PIPE_ROUTER_ERROR_READ;
			break;
		}

		if (write_nointr (fd, buffer, count) < 0)
		{
			file_log (LOG_TAG ": router write_nointr [%s]: %s\n", pi->log_filename, strerror (errno));
			pi->error = PIPE_ROUTER_ERROR_WRITE;
			break;
		}

		for (i = 0; loggers [i].name; i ++)
		{
			if (strstr(pi->name, loggers [i].name) == NULL)
			{
				continue;
			}
			else
			{
				if (loggers [i].overload == 0)
				{
					continue;
				}

				memset (state, 0, sizeof (state));

				// check if log overloaded or not.
				time(&cur_time);

				dif_time = difftime (cur_time, pre_time);

				buf_count = buf_count + count;

				if (dif_time >= 3.0)	// calculate overload every 3 seconds
				{
					if ( ( dif_time > 0 ) && ( ( buf_count / dif_time ) > (loggers [i].overload * 1024) ) )
					{
						sprintf (state, "***LOG_BUFFER_OVERLOADED***, buffer[%.3f](B/s), criteria[%d](KB/s).\n", ( buf_count / dif_time ), loggers [i].overload);
						state[sizeof(state)-1] = 0;

						if (write_nointr (fd, state, strlen(state)) < 0)
						{
							file_log (LOG_TAG ": router write_nointr [%s]: %s\n", pi->log_filename, strerror (errno));
						}
					}

					buf_count = 0;
					pre_time = cur_time;
				}
				break;
			}
		}
	}

	if (fd != -1)
	{
		close_nointr (fd);
	}

	DM ("%s router thread end ...\n", pi->name);
	return NULL;
}

void logger_common_logdata_filename (const char *path_with_slash, const char *file_prefix, char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s.%sdata." LOG_DATA_EXT, path_with_slash, file_prefix);
}

int logger_common_logdata_open (const char *name, const char *path_with_slash, const char *file_prefix)
{
	char datafile [PATH_MAX];
	char logfile [PATH_MAX];
	struct dirent *entry;
	DIR *dir;
	int fd;
	unsigned long i;

	LOGDATA_HEADER hdr;
	LOGDATA data;

	logger_common_logdata_filename (path_with_slash, file_prefix, datafile, sizeof (datafile));

	if ((fd = open_nointr (datafile, O_RDWR | O_CREAT, LOG_FILE_MODE)) < 0)
	{
		DM ("[embedded] %s logdata open [%s]: %s\n", name, datafile, strerror (errno));
		return -1;
	}

	memset (& hdr, 0, sizeof (LOGDATA_HEADER));

	if (read_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] %s logdata read header [%s]: %s\n", name, datafile, strerror (errno));
		goto fixdata;
	}

	if (hdr.magic != LOGDATA_MAGIC)
	{
		DM ("[embedded] %s logdata wrong magic [%s][%08lX] (not %08lX)! corrupted?\n", name, datafile, hdr.magic, (unsigned long) LOGDATA_MAGIC);
		goto fixdata;
	}

	if (hdr.entry_count != LOGDATA_COUNT)
	{
		DM ("[embedded] %s logdata invalid entry count [%s][%lu] (not %lu)! corrupted?\n", name, datafile, hdr.entry_count, (unsigned long) LOGDATA_COUNT);
		goto fixdata;
	}

	if (hdr.entry_size != sizeof (LOGDATA))
	{
		DM ("[embedded] %s logdata invalid entry size [%s][%lu] (not %lu)! corrupted?\n", name, datafile, hdr.entry_size, (unsigned long) sizeof (LOGDATA));
		goto fixdata;
	}

	if (hdr.header_size != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] %s logdata invalid header size [%s][%lu] (not %lu)! corrupted?\n", name, datafile, hdr.header_size, (unsigned long) sizeof (LOGDATA_HEADER));
		goto fixdata;
	}

	if (hdr.index_head >= hdr.entry_count)
	{
		DM ("[embedded] %s logdata invalid index_head [%s][%lu] (> count=%lu)! corrupted?\n", name, datafile, hdr.index_head, hdr.entry_count);
		goto fixdata;
	}

	hdr.magic = (hdr.entry_count * hdr.entry_size) + hdr.header_size; /* expected file size */
	hdr.entry_size = (unsigned long) lseek (fd, 0, SEEK_END); /* real file size */
	hdr.header_size = sizeof (LOGDATA_HEADER);

	if (hdr.magic != hdr.entry_size)
	{
		DM ("[embedded] %s logdata invalid file size [%s][%lu] (not %lu)! corrupted?\n", name, datafile, hdr.entry_size, hdr.magic);
		goto fixdata;
	}

	goto end;

fixdata:;
	hdr.magic = LOGDATA_MAGIC;
	hdr.entry_count = LOGDATA_COUNT;
	hdr.entry_size = sizeof (LOGDATA);
	hdr.header_size = sizeof (LOGDATA_HEADER);
	hdr.total_size = 0;
	hdr.index_head = 0;
	hdr.index_tail = 0;

	DM ("[embedded] %s logdata create [%s], entry count [%lu], entry size [%lu], header size [%lu], expected file size [%lu]\n",
		name, datafile, hdr.entry_count, hdr.entry_size, hdr.header_size, (hdr.entry_count * hdr.entry_size) + hdr.header_size);

	lseek (fd, 0, SEEK_SET);

	if (ftruncate (fd, 0) < 0)
	{
		DM ("[embedded] %s logdata ftruncate [%s]: %s\n", name, datafile, strerror (errno));
		/*
		 * going on even failed to truncate file...
		 */
	}

	if (write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] %s logdata write header [%s]: %s\n", name, datafile, strerror (errno));
		close (fd);
		return -1;
	}

	if (! file_prefix)
	{
		i = 0;
	}
	else
	{
		int len = strlen (file_prefix);

		if ((dir = opendir (path_with_slash)) == NULL)
		{
			DM ("[embedded] %s logdata opendir [%s]: %s\n", name, path_with_slash, strerror (errno));
			close (fd);
			return -1;
		}

		while ((entry = readdir (dir)) != NULL)
		{
			if ((strcmp (entry->d_name, ".") == 0) || (strcmp (entry->d_name, "..") == 0))
				continue;

			/* if file_prefix is given, only the files start with file_prefix will be included */
			if ((strncmp (file_prefix, entry->d_name, len) != 0) || (! IS_STORAGE_CODE (entry->d_name [len])))
				continue;

			SAFE_SPRINTF (logfile, sizeof (logfile), "%s%s", path_with_slash, entry->d_name);

			if (hdr.index_head >= hdr.entry_count) /* reach maximum count, ignore all rest files */
			{
				errno = 0;
				unlink (logfile);
				DM ("[embedded] %s logdata unlink scanned file [%s][%s]\n", name, entry->d_name, strerror (errno));
				continue;
			}

			SAFE_SPRINTF (data.file, sizeof (data.file), "%s", entry->d_name);
			data.size = (unsigned long long) file_size (logfile);

			DM ("[embedded] %s logdata scanned [%s][%llu]\n", name, data.file, data.size);

			if (write_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
			{
				DM ("[embedded] %s logdata write data [%s]: %s\n", name, datafile, strerror (errno));
				close (fd);
				return -1;
			}

			hdr.total_size += data.size;
			hdr.index_head ++;
		}

		closedir (dir);

		i = hdr.index_head;
	}

	memset (& data, 0, sizeof (LOGDATA));

	for (; i < hdr.entry_count; i ++)
	{
		if (write_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
		{
			DM ("[embedded] %s logdata write empty data [%s]: %s\n", name, datafile, strerror (errno));
			close (fd);
			return -1;
		}
	}

	DM ("[embedded] %s logdata last offset = %lu\n", name, (unsigned long) lseek (fd, 0, SEEK_CUR));

	if (hdr.index_head >= hdr.entry_count)
		hdr.index_head = hdr.entry_count - 1;

	lseek (fd, 0, SEEK_SET);

	if (write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] %s logdata write header [%s]: %s\n", name, datafile, strerror (errno));
		close (fd);
		return -1;
	}

end:;
	lseek (fd, 0, SEEK_SET);
	return fd;
}

static int logger_common_logdata_remove_file (int fd, const char *name, const char *path_with_slash, const char *reason, LOGDATA_HEADER *phdr, LOGDATA *pdata, char *logfile, int logfilelen)
{
	unsigned long offset;

	if (phdr->index_head == phdr->index_tail)
		return 0;

	offset = phdr->index_tail * sizeof (LOGDATA) + sizeof (LOGDATA_HEADER);

	if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
	{
		DM ("[embedded] %s logdata lseek offset=%lu: %s\n", name, offset, strerror (errno));
		return -1;
	}

	if (read_nointr (fd, pdata, sizeof (LOGDATA)) != sizeof (LOGDATA))
	{
		DM ("[embedded] %s logdata read data: %s\n", name, strerror (errno));
		return -1;
	}

	if (phdr->total_size > pdata->size)
		phdr->total_size -= pdata->size;
	else
		phdr->total_size = 0;

	SAFE_SPRINTF (logfile, logfilelen, "%s%s", path_with_slash, pdata->file);

	if (access (logfile, F_OK) == 0)
	{
		errno = 0;
		unlink (logfile);
		file_log (LOG_TAG ": [embedded] %s logdata unlink [%s] due to %s! [%s]\n", name, logfile, reason, strerror (errno));
	}

	pdata->file [0] = 0;
	pdata->size = 0;

	lseek (fd, (off_t) offset, SEEK_SET);
	write_nointr (fd, pdata, sizeof (LOGDATA));

	phdr->index_tail ++;

	if (phdr->index_tail >= phdr->entry_count)
		phdr->index_tail = 0;

	return 1;
}

int logger_common_logdata_limit_size (int fd, const char *name, const char *path_with_slash, unsigned long limited_size_mb, unsigned long reserved_size_mb, unsigned long long *total_size)
{
	LOGDATA_HEADER hdr;
	LOGDATA data;
	LOGGER *pl;
	char logfile [PATH_MAX];
	unsigned long offset, cur_size_mb;
	int ret = -1;

	lseek (fd, 0, SEEK_SET);

	if (read_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] %s logdata read header: %s\n", name, strerror (errno));
		goto end;
	}

	if (hdr.magic != LOGDATA_MAGIC)
	{
		DM ("[embedded] %s logdata invalid magic [0x%08lX]!\n", name, hdr.magic);
		goto end;
	}

	offset = hdr.index_head * sizeof (LOGDATA) + sizeof (LOGDATA_HEADER);

	if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
	{
		DM ("[embedded] %s logdata lseek offset=%lu: %s\n", name, offset, strerror (errno));
		goto end;
	}

	if (read_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
	{
		DM ("[embedded] %s logdata read data: %s\n", name, strerror (errno));
		goto end;
	}

	if (data.file [0])
	{
		/*
		 * update logging file size
		 */
		SAFE_SPRINTF (logfile, sizeof (logfile), "%s%s", path_with_slash, data.file);

		if (hdr.total_size > data.size)
			hdr.total_size -= data.size;
		else
			hdr.total_size = 0;

		data.size = (unsigned long long) file_size (logfile);

		hdr.total_size += data.size;

		lseek (fd, (off_t) offset, SEEK_SET);
		write_nointr (fd, & data, sizeof (LOGDATA));
	}

	cur_size_mb = (unsigned long) (hdr.total_size / MB);

	while (cur_size_mb >= limited_size_mb)
	{
		if (logger_common_logdata_remove_file (fd, name, path_with_slash, "size limitation", & hdr, & data, logfile, sizeof (logfile)) <= 0)
			break;

		cur_size_mb = (unsigned long) (hdr.total_size / MB);
	}

	cur_size_mb = storage_get_free_size_mb (path_with_slash);

	pl = loggers_get_logger_by_name_nolock (name);

	if ((cur_size_mb < reserved_size_mb) || (pl->state & LOGGER_STATE_MASK_REQUEST_REMOVE_OLDLOG))
	{
		int count;

		if (cur_size_mb < reserved_size_mb)
			loggers_request_remove_old_logs_nolock ();

		pl->state &= ~LOGGER_STATE_MASK_REQUEST_REMOVE_OLDLOG;

		/*
		 * calculate remove count by logger weight
		 */
		if ((count = (int) ((float) 10.0 * pl->weight)) <= 0)
			count = 1;

		DM ("[embedded] %s was requested to remove %d file(s)!\n", name, count);

		for (; count > 0; count --)
		{
			if (logger_common_logdata_remove_file (fd, name, path_with_slash, "low free space", & hdr, & data, logfile, sizeof (logfile)) <= 0)
				break;
		}
	}

	if (total_size)
	{
		*total_size = hdr.total_size;
	}

	lseek (fd, 0, SEEK_SET);
	write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER));

	ret = 0;
end:;
	return ret;
}

int logger_common_logdata_add_file (int fd, const char *name, const char *path_with_slash, const char *log_filename)
{
	LOGDATA_HEADER hdr;
	LOGDATA data;
	char logfile [PATH_MAX], *ptr;
	unsigned long offset;
	int ret = -1;

	if ((! log_filename) || (! log_filename [0]))
		goto end;

	ptr = strrchr (log_filename, '/');

	if (! ptr)
		goto end;

	ptr ++;

	lseek (fd, 0, SEEK_SET);

	if (read_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		DM ("[embedded] %s logdata read header: %s\n", name, strerror (errno));
		goto end;
	}

	if (hdr.magic != LOGDATA_MAGIC)
	{
		DM ("[embedded] %s logdata invalid magic [0x%08lX]!\n", name, hdr.magic);
		goto end;
	}

	offset = hdr.index_head * sizeof (LOGDATA) + sizeof (LOGDATA_HEADER);

	if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
	{
		DM ("[embedded] %s logdata lseek offset=%lu: %s\n", name, offset, strerror (errno));
		goto end;
	}

	if (read_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
	{
		DM ("[embedded] %s logdata read data: %s\n", name, strerror (errno));
		goto end;
	}

	if (data.file [0])
	{
		hdr.index_head ++;

		if (hdr.index_head >= hdr.entry_count)
			hdr.index_head = 0;

		if (hdr.index_head == hdr.index_tail)
			hdr.index_tail ++;

		if (hdr.index_tail >= hdr.entry_count)
			hdr.index_tail = 0;

		offset = hdr.index_head * sizeof (LOGDATA) + sizeof (LOGDATA_HEADER);

		if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
		{
			DM ("[embedded] %s logdata lseek offset=%lu: %s\n", name, offset, strerror (errno));
			goto end;
		}

		if (read_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
		{
			DM ("[embedded] %s logdata read data: %s\n", name, strerror (errno));
			goto end;
		}

		if (data.file [0])
		{
			if (hdr.total_size > data.size)
				hdr.total_size -= data.size;
			else
				hdr.total_size = 0;

			SAFE_SPRINTF (logfile, sizeof (logfile), "%s%s", path_with_slash, data.file);

			if (access (logfile, F_OK) == 0)
			{
				errno = 0;
				unlink (logfile);
				file_log (LOG_TAG ": [embedded] %s logdata unlink [%s] due to logdata full (head=%lu)! [%s]\n", name, logfile, hdr.index_head, strerror (errno));
			}
		}
	}

	data.size = (unsigned long long) file_size (log_filename);
	SAFE_SPRINTF (data.file, sizeof (data.file), "%s", ptr);

	lseek (fd, (off_t) offset, SEEK_SET);
	write_nointr (fd, & data, sizeof (LOGDATA));

	hdr.total_size += data.size;

	lseek (fd, 0, SEEK_SET);
	write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER));

	ret = 0;
end:;
	return ret;
}

int logger_common_logdata_compress_file (int UNUSED_VAR (fd), const char *UNUSED_VAR (name), const char *UNUSED_VAR (log_filename))
{
	return 0;
}

int logger_common_check_file_type (const char *UNUSED_VAR (name), const char *filepath, const char *file_prefix, const char *log_filename)
{
	char buffer [32];
	const char *filename;

	logger_common_logdata_filename ("", file_prefix, buffer, sizeof (buffer));

	if ((filename = strrchr (filepath, '/')) == NULL) filename = filepath; else filename ++;

	if (log_filename [0] /* logging */)
	{
		const char *ptr;

		if (strcmp (filename, buffer) == 0)
			return LOGGER_FILETYPE_LOGGING_DATA;

		if ((ptr = strrchr (log_filename, '/')) == NULL) ptr = log_filename; else ptr ++;

		if (strcmp (filename, ptr) == 0)
			return LOGGER_FILETYPE_LOGGING_FILE;
	}
	else
	{
		if (strcmp (filename, buffer) == 0)
			return LOGGER_FILETYPE_DATA;
	}

	if (strncmp (filename, file_prefix, strlen (file_prefix)) == 0)
		return LOGGER_FILETYPE_FILE;

	return LOGGER_FILETYPE_UNKNOWN;
}

int logger_common_get_overload_size (const char *name)
{
	int i;

	for (i = 0; loggers [i].name; i ++)
	{
		if (strstr(name, loggers [i].name) == NULL)
		{
			continue;
		}
		else
		{
			return loggers [i].overload;
		}
	}

	return 0;
}
