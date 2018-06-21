#define	LOG_TAG		"STT:lognet"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "cutils/sched_policy.h"

#include "common.h"
#include "server.h"

#include "headers/sem.h"
#include "headers/fio.h"
#include "headers/process.h"

/*
 * include the static expiring checking function
 */
#include "src/expire.c"

#define	VERSION	"1.11"
/*
 * 1.11	: fix potential for loop error index.
 * 1.10	: not to save pid to enable/disable ndc process.
 * 1.9	: add enable/disable route information logging feature.
 * 1.8	: add log to check if any error return value for calling tcpdump.
 * 1.7	: check if tcpdump process cannot be terminated, no need to start net logger to avoid forked process from htcserviced leakage.
 * 1.6	: retry 10 times waitpid, put process into delete_queue for auto_clear thread to clear.
 * 1.5	: extend timeout value to clear tcpdump process.
 * 1.4	: support new logger control service.
 * 1.3	: fix the filename when rotate number > 10.
 * 1.2	: check log file missing state.
 * 1.1	: ensure running tcpdump as root user.
 * 1.0	: run tcpdump.
 */

#define	MB			(1024 * 1024)

#define	SIZE_CLEAR_BEFORE_STOP	5
#define	LOG_NET_EXT		"net"
#define	LOG_NAME		"tcpdump"

#define	SESSION_COUNT		100
#define	SESSION_ARRAY_LENGTH	(SESSION_COUNT * sizeof (LOG_SESSION))
#define	MAX_SESSION		(SESSION_COUNT - 5)

/* custom commands */
#define	LOG_GETPATH	":getpath:"
#define	LOG_SETPATH	":setpath:"
#define	LOG_GETPARAM	":getparam:"
#define	LOG_SETPARAM	":setparam:"	/* :setparam:rotate_size:rotate_count:size_limit */
#define	LOG_RUN		":run:"
#define	LOG_STOP	":stop:"
#define	LOG_ISLOGGING	":islogging:"

#define	MAX_RETRY_COUNT		100
#define	PROCESS_QUEUE_COUNT	100
#define	PROCESS_QUEUE_ARRAY_LENGTH	(PROCESS_QUEUE_COUNT * sizeof (PROCESS_DELETE_QUEUE))

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static sem_t autoclear_lock;
static sem_t monitor_lock;

static int done = 0;
static int pid = 0;
static unsigned int mask = 0;
static int process_queue_count = 0;

static char log_command [PATH_MAX] = "/system/xbin/tcpdump";
static char log_command_info [128] = "";
static char log_path [PATH_MAX] = "internal";
static char log_filename [PATH_MAX] = "";
static char log_size [12] = "20";
static char log_rotate [12] = "8";
static char log_limit [12] = "500";

static pthread_mutex_t sn_lock = PTHREAD_MUTEX_INITIALIZER;
static int session_count;
static unsigned short sn = 0;
static LOG_SESSION sessions [SESSION_COUNT];

static PROCESS_DELETE_QUEUE process_delete_queue [PROCESS_QUEUE_COUNT];

static unsigned short lognet_get_log_sn (void);

static void clear_process_nolock (int z)
{
	int status = 0, ret = 0, i = 0, index = 0;
	char *s;

	for (i = 1; i <= 10; i ++)
	{
		DM ("waitpid: waiting for %s child (%d:%c) finish ... %d\n", z ? "zombie" : "stopping", pid, get_pid_stat (pid), i);

		ret = waitpid (pid, & status, WNOHANG);

		if (ret > 0)
			break;

		usleep (100000);

		if (i == 10)	// retry 10 times waitpid, put process into delete_queue for auto_clear thread to clear.
		{
			for (index = 0; index <= PROCESS_QUEUE_COUNT; index ++)
			{
				if (index == PROCESS_QUEUE_COUNT)
				{
					DM ("delete_queue is full to store child (%d).", pid);
					break;
				}

				if (process_delete_queue [index].pid == 0)	// add process into delete queue that failed to be cleared
				{
					DM ("add %s child (%d:%c) into delete_queue, index(%d).\n", z ? "zombie" : "stopping", pid, get_pid_stat (pid), index);
					process_queue_count ++;
					process_delete_queue [index].pid = pid;
					process_delete_queue [index].retry = 0;
					break;
				}
			}
		}
	}

	s = alloc_waitpid_status_text (status);

	snprintf (log_command_info, sizeof (log_command_info), "ret=%d, %s", ret, s);

	log_command_info [sizeof (log_command_info) - 1] = 0;

	if (s) free (s);

	DM ("waitpid: %s finished (%s).\n", z ? "zombie" : "stopping", log_command_info);
}

static unsigned long state = LOGGER_STATE_MASK_STOPPED;

static int lognet_islogging_nolock (void)
{
	if (pid > 0)
	{
		char name [PATH_MAX];
		int len;

		int zc = strlen (log_rotate);

		double rc = log10(atoi(log_rotate));

		if((rc == (int)rc) && (rc >= 1))
		{
			zc--;
		}

		snprintf (name, sizeof (name), "%s", log_filename);
		name [sizeof (name) - 1] = 0;

		len = strlen (name);

		if (zc >= (int) (sizeof (name) - len))
		{
			zc = sizeof (name) - len - 1;
		}

		if (zc > 0)
		{
			memset (& name [len], '0', zc);
			name [len + zc] = 0;
		}

		state = logger_common_update_state ("lognet", state, pid, (pthread_t) -1, name, 0);
	}
	return state;
}

static int lognet_islogging (void)
{
	int ret;
	/*
	 * in order to protect var: pid
	 */
	pthread_mutex_lock (& data_lock);
	ret = lognet_islogging_nolock ();
	pthread_mutex_unlock (& data_lock);
	return ret;
}

static int lognet_stop (const char *reason, ...)
{
	char msg_reason [PATH_MAX];
	char msg [PATH_MAX];
	va_list ap;
	int ret = -1;

	pthread_mutex_lock (& data_lock);

	state &= ~(LOGGER_STATE_MASK_STARTED | LOGGER_STATE_MASK_LOGGING);
	state |= LOGGER_STATE_MASK_STOPPED;

	va_start (ap, reason);
	vsnprintf (msg_reason, sizeof (msg_reason), reason, ap);
	msg_reason [sizeof (msg_reason) - 1] = 0;
	va_end (ap);

	snprintf (msg, sizeof (msg), LOG_TAG ": stop net logger: %s\n", msg_reason);
	msg [sizeof (msg) - 1] = 0;

	file_log (msg);

	if (is_process_alive (pid))
	{
		int c = get_pid_stat (pid);

		kill (pid, SIGTERM);

		clear_process_nolock (0);

		file_log (LOG_TAG ": %d (%c) stopped\n", pid, c);

		log_filename [0] = 0;

		ret = 0;
	}

	pid = 0;

	pthread_mutex_unlock (& data_lock);
	return ret;
}

static int ndc_start (void)
{
	DM ("ndc_start +++. \n");

	char ndc_command [PATH_MAX] = "/system/bin/ndc";

	if (access (ndc_command, X_OK) != 0)
	{
		DM ("unable to find [%s] command. \n", ndc_command);
		return -1;
	}

	property_set ("persist.sys.htcnetd.debug", "1");

	int ndc_pid = fork ();

	if (ndc_pid == 0)	/* child */
	{
		char *argv [5];
		argv [0] = ndc_command;
		argv [1] = "htcnetd";
		argv [2] = "config";
		argv [3] = "reconfig";
		argv [4] = NULL;

		if (execv (argv [0], argv) == -1)
		{
			DM ("fork: child after execv() error: %s\n", strerror (errno));
			_exit (EXIT_FAILURE);
		}
		DM ("fork: done call [%s] command. \n", ndc_command);
	}

	sleep(1);

	ndc_pid = fork ();

	if (ndc_pid == 0)	/* child */
	{
		char *argv [6];
		argv [0] = ndc_command;
		argv [1] = "htcnetd";
		argv [2] = "log";
		argv [3] = "route";
		argv [4] = "start";
		argv [5] = NULL;

		if (execv (argv [0], argv) == -1)
		{
			DM ("fork: child after execv() error: %s\n", strerror (errno));
			_exit (EXIT_FAILURE);
		}
		DM ("fork: done call [%s] command. \n", ndc_command);
	}

	DM ("ndc_start ---. \n");
	return 0;
}

static int ndc_stop (void)
{
	DM ("ndc_stop +++. \n");

	char ndc_command [PATH_MAX] = "/system/bin/ndc";

	if (access (ndc_command, X_OK) != 0)
	{
		DM ("unable to find [%s] command. \n", ndc_command);
		return -1;
	}

	int ndc_pid = fork ();

	if (ndc_pid == 0)	/* child */
	{
		char *argv [6];
		argv [0] = ndc_command;
		argv [1] = "htcnetd";
		argv [2] = "log";
		argv [3] = "route";
		argv [4] = "stop";
		argv [5] = NULL;

		if (execv (argv [0], argv) == -1)
		{
			DM ("fork: child after execv() error: %s\n", strerror (errno));
			_exit (EXIT_FAILURE);
		}

		DM ("fork: done call [%s] command. \n", ndc_command);
	}

	DM ("ndc_stop ---. \n");
	return 0;
}

static int lognet_start (void)
{
	char buf [PATH_MAX], *ptr;
	int ret;

	LOG_SESSION ls;

	GLIST_NEW (fds);

	bzero (& ls, sizeof (ls));
	bzero (process_delete_queue, PROCESS_QUEUE_ARRAY_LENGTH);

	if (access (log_command, X_OK) != 0)
	{
		lognet_stop ("unable to find [%s] command", log_command);
		return -1;
	}

	/* [ro.serialno]: [HT23GW301202] */
	property_get ("ro.serialno", buf, "unknown");
	strncpy (ls.devinfo, buf, sizeof (ls.devinfo) - 1);

	/* [ro.build.product]: [evita] */
	property_get ("ro.build.product", buf, "unknown");
	strncpy (ls.prdinfo, buf, sizeof (ls.prdinfo) - 1);

	/* [ro.build.description]: [0.1.0.0 (20120613 Evita_ATT_WWE #) test-keys] */
	property_get ("ro.build.description", buf, "0.1.0.0");
	ptr = strchr (buf, ' ');
	if (ptr) *ptr = 0;
	strncpy (ls.rominfo, buf, sizeof (ls.rominfo) - 1);

	pthread_mutex_lock (& data_lock);

	ls.sn = lognet_get_log_sn ();

	strncpy (ls.timestamp, TAG_DATETIME, sizeof (ls.timestamp));
	str_replace_tags (ls.timestamp);

#if 0 // keep old naming first
	snprintf (log_filename, sizeof (log_filename), "%s" LOG_NAME "_" LOG_FILE_TAG "%c%04u_%s_%s_%s_%s.pkt",
			log_path,
			dir_get_storage_code (log_path),
			(unsigned int) ls.sn,
			ls.timestamp,
			ls.devinfo,
			ls.prdinfo,
			ls.rominfo);
#else
	snprintf (log_filename, sizeof (log_filename), "%s" LOG_NAME "_%s.pkt",
			log_path,
			ls.timestamp);
#endif
	log_filename [sizeof (log_filename) - 1] = 0;

	log_command_info [0] = 0;

	/*
	 * release zombi child
	 */
	if (is_process_zombi (pid) == 1)
	{
		clear_process_nolock (1);
	}

	state &= ~(LOGGER_STATE_MASK_STOPPED | LOGGER_STATE_MASK_LOGGING);
	state |= LOGGER_STATE_MASK_STARTED;

	DM ("%s -i any -C %s -W %s -s 0 -Z root -w %s\n", log_command, log_size, log_rotate, log_filename);

	/*
	 * Do not use functions would call malloc() or free() in child process.
	 * A no-longer-running thread may be holding on to the heap lock, and
	 * an attempt to malloc() or free() would result in deadlock.
	 */
	fds = find_all_fds ();

	pid = fork ();

	if (pid == 0)	/* child */
	{
		/* tcpdump -i any -C log_rotate_size -W log_rotate_count -s 0 -Z root -w /data/tcpdump_xxx.pkt  */

		/*
		 * run tcpdump in background group
		 */
		set_sched_policy (getpid (), SP_BACKGROUND);

		//DM ("fork: child forked (%d).\n", getpid ());
		char *argv [14];
		argv [0] = log_command;
		argv [1] = "-i";
		argv [2] = "any";
		argv [3] = "-C";
		argv [4] = (char *) log_size;
		argv [5] = "-W";
		argv [6] = (char *) log_rotate;
		argv [7] = "-s";
		argv [8] = "0";
		argv [9] = "-Z";
		argv [10] = "root";
		argv [11] = "-w";
		argv [12] = log_filename [0] ? log_filename : "/dev/null";
		argv [13] = NULL;
		close_all_fds (fds);

		//DM ("fork: child before execv().\n");
		if (execv (argv [0], argv) == -1)
		{
			DM ("fork: child after execv() error: %s\n", strerror (errno));
		}

		exit (127);
	}

	glist_clear (& fds, free);

	if (pid < 0)
	{
		DM ("fork: %s\n", strerror (errno));
		pid = 0;
	}
	else
	{
		DM ("fork: parent got child pid (%d).\n", pid);
	}

	if (pid)
	{
		sleep (1);

		if (lognet_islogging_nolock () & LOGGER_STATE_MASK_LOGGING)
		{
			DM ("record.\n");

			file_log (LOG_TAG ": start net logger [%s][%s][%s], file=%s, " LOG_NAME "=%d\n", log_size, log_rotate, log_limit, log_filename, pid);

			snprintf (buf, sizeof (buf), "%s." LOG_NET_EXT "_" SESSION_DATA_FILE_VERSION_ID ".%c", log_path, dir_get_storage_code (log_path));
			buf [sizeof (buf) - 1] = 0;
			file_mutex_write (buf, (const char *) & ls, sizeof (ls), O_CREAT | O_RDWR | O_APPEND);
		}
		else
		{
			DM ("drop.\n");

			snprintf (log_filename, sizeof (log_filename), "Retrying %s ... %s", log_command, log_command_info);
			log_filename [sizeof (log_filename) - 1] = 0;
		}
	}

	ret = pid ? 0 : -1;

	pthread_mutex_unlock (& data_lock);
	return ret;
}

static void read_timestampes_nolock (void)
{
	char buffer [PATH_MAX];
	int count;

	snprintf (buffer, sizeof (buffer), "%s." LOG_NET_EXT "_" SESSION_DATA_FILE_VERSION_ID ".%c", log_path, dir_get_storage_code (log_path));
	buffer [sizeof (buffer) - 1] = 0;

	bzero (sessions, SESSION_ARRAY_LENGTH);

	count = file_mutex_read (buffer, (char *) sessions, SESSION_ARRAY_LENGTH);

	if (count < (int) sizeof (LOG_SESSION))
	{
		session_count = 0;
	}
	else
	{
		LOG_SESSION *pls;
		int i;

		session_count = count / sizeof (LOG_SESSION);

		if ((count % sizeof (LOG_SESSION)) != 0)
		{
			DM ("[%s] may be corrupted: size is not matched!\n", buffer);
			session_count = 0;
		}

		for (i = 0, pls = sessions; i < session_count; i ++, pls ++)
		{
			if (pls->timestamp [TAG_DATETIME_LEN] != 0)
			{
				DM ("[%s] may be corrupted: wrong timestamp!\n", buffer);
				session_count = 0;
				break;
			}
		}
	}
}

static void write_timestampes_nolock (void)
{
	char buffer [PATH_MAX];

	snprintf (buffer, sizeof (buffer), "%s." LOG_NET_EXT "_" SESSION_DATA_FILE_VERSION_ID ".%c", log_path, dir_get_storage_code (log_path));
	buffer [sizeof (buffer) - 1] = 0;

	file_mutex_write (buffer, (const char *) sessions, session_count * sizeof (LOG_SESSION), O_CREAT | O_TRUNC | O_WRONLY);

	//DM ("[%s]: end of write, session count = %d\n", buffer, session_count);
}

static void lognet_reset_sn (void)
{
	pthread_mutex_lock (& sn_lock);
	sn = 0;
	pthread_mutex_unlock (& sn_lock);
}

static unsigned short lognet_get_log_sn (void)
{
	unsigned short ret;
	int i;

	pthread_mutex_lock (& sn_lock);

	if (sn == 0)
	{
		read_timestampes_nolock ();

		sn = 1;

		for (i = 0; i < session_count; i ++)
		{
			if (sessions [i].sn >= sn)
				sn = sessions [i].sn + 1;
		}
	}

	if ((sn == 0) || (sn > 9999))
		sn = 1;

	ret = (unsigned short) sn;

	pthread_mutex_unlock (& sn_lock);
	return ret;
}

static char *get_log_filename_with_rotate_number_nolock (LOG_SESSION *psession, const char UNUSED_VAR (sc), char *buffer, int buflen, int rn)
{
	if ((! psession) || (! buffer))
		return NULL;

#if 0 // keep old naming first
	snprintf (buffer, buflen, LOG_NAME "_" LOG_FILE_TAG "%c%04u_%s_%s_%s_%s.pkt%d",
			sc,
			(unsigned int) psession->sn,
			psession->timestamp,
			psession->devinfo,
			psession->prdinfo,
			psession->rominfo,
			rn);
#else

	// check digits for netlog rotate count
	char *rotate_zero;

	int zc = strlen (log_rotate);
	double rc = log10(atoi(log_rotate));

	if((rc == (int)rc) && (rc >= 1))
		zc--;

	rotate_zero = malloc (sizeof(char) * (zc + 1));

	int zero_count = 0;

	if(rn > 0)
		zero_count = zc - 1 - floor(log10(rn));
	else
		zero_count = zc - 1;


	if (zero_count > 0)
	{
		memset (rotate_zero, '0', zero_count);
	}

	rotate_zero [zero_count] = 0;
	snprintf (buffer, buflen, LOG_NAME "_%s.pkt%s%d",
			psession->timestamp,
			rotate_zero,
			rn);

	free(rotate_zero);

#endif

	buffer [buflen - 1] = 0;
	return buffer;
}

static unsigned long long count_session_size_nolock (LOG_SESSION *psession)
{
	unsigned long long size;
	char logpath [PATH_MAX];
	char *pfile, sc;
	int i, len;

	if (! psession)
		return 0;

	/*
	 * do not modify log_path, make a copy
	 */
	strncpy (logpath, log_path, sizeof (logpath));
	logpath [sizeof (logpath) - 1] = 0;

	sc = dir_get_storage_code (logpath);

	len = strlen (logpath);

	if (len >= (int) sizeof (logpath)) /* no enough buffer to add file name */
		return 0;

	pfile = & logpath [len];

	len = sizeof (logpath) - len - 1;

	for (size = 0, i = 0;; i ++)
	{
		get_log_filename_with_rotate_number_nolock (psession, sc, pfile, len, i);

		pfile [len - 1] = 0;

		if (access (logpath, F_OK) != 0)
			break;

		size += (unsigned long long) file_size (logpath);
	}

	*pfile = 0;

	return size;
}

static unsigned long long count_total_size_nolock (void)
{
	LOG_SESSION *pls;
	unsigned long long size;
	int i;

	for (size = 0, i = 0, pls = sessions; i < session_count; i ++, pls ++)
	{
		if ((pls->size == 0) || (i == (session_count - 1)))
		{
			pls->size = count_session_size_nolock (pls);
		}

		if (pls->size > ((unsigned long long) 4096 * MB))
		{
			DM ("abnormal size [%lld] of [%u][%s] detected!\n", pls->size, pls->sn, pls->timestamp);
		}

		size += pls->size;
	}

	return size;
}

static int unlink_log_files_by_session_nolock (LOG_SESSION *psession, const char *reason, const char sc)
{
	char logpath [PATH_MAX];
	char *pfile;
	int i;
	int len;
	int processed = 0;

	if (! psession)
		goto end;

	/*
	 * do not modify log_path, make a copy
	 */
	strncpy (logpath, log_path, sizeof (logpath));
	logpath [sizeof (logpath) - 1] = 0;

	len = strlen (logpath);

	if (len >= (int) sizeof (logpath)) /* no enough buffer to add file name */
		goto end;

	pfile = & logpath [len];

	len = sizeof (logpath) - len - 1;

	for (i = 0;; i ++)
	{
		get_log_filename_with_rotate_number_nolock (psession, sc, pfile, len, i);

		pfile [len - 1] = 0;

		if (access (logpath, F_OK) != 0)
			break;

		DM ("[%s] unlink [%s]\n", reason, logpath);

		unlink (logpath);

		processed ++;
	}

	*pfile = 0;

	file_log (LOG_TAG ": [%s] unlink [%s][%04u][%s]\n", reason, logpath, (unsigned int) psession->sn, psession->timestamp);

end:;
	return processed;
}

static void clear_process_delete_queue (void)
{
	DM ("clear process delete_queue, process_queue_count(%d). \n", process_queue_count);

	int status = 0, ret = 0, index = 0, k = 0;

	while(index < PROCESS_QUEUE_COUNT)
	{
		if (process_queue_count == 0)
			break;

		if (process_delete_queue [index].pid == 0)
		{
			index ++;
			continue;
		}

		if (is_process_zombi (process_delete_queue [index].pid) == -1)	// no such process
		{
			DM ("no process, remove it on delete_queue (%d:%c), total retry count(%d), index(%d).\n", process_delete_queue [index].pid, get_pid_stat (process_delete_queue [index].pid), process_delete_queue [index].retry, index);
			process_queue_count --;
			process_delete_queue [index].pid = 0;
			process_delete_queue [index].retry = 0;
			index ++;
			continue;
		}

		if (process_delete_queue [index].retry >= MAX_RETRY_COUNT)
		{
			DM ("remove process on delete_queue (%d:%c) since retry count exceed max times. \n", process_delete_queue [index].pid, get_pid_stat (process_delete_queue [index].pid));
			process_queue_count --;
			process_delete_queue [index].pid = 0;
			process_delete_queue [index].retry = 0;
			index ++;
			continue;
		}

		for (k = 0; k < 10; k ++)
		{
			process_delete_queue [index].retry ++;

			if (process_delete_queue [index].retry % 5 == 0)	// call kill(SIGKILL) for the process every 5 retry times.
			{
				DM ("kill process on delete_queue (%d:%c) ... index(%d).\n", process_delete_queue [index].pid, get_pid_stat (process_delete_queue [index].pid), index);
				kill (process_delete_queue [index].pid, SIGKILL);
			}

			DM ("waitpid: waiting process on delete_queue (%d:%c) finish ... retry(%d), index(%d).\n", process_delete_queue [index].pid, get_pid_stat (process_delete_queue [index].pid), process_delete_queue [index].retry, index);

			ret = waitpid (process_delete_queue [index].pid, & status, WNOHANG);

			if (ret > 0)
			{
				DM ("process (%d) is cleared on delete_queue.\n", process_delete_queue [index].pid);
				process_queue_count --;
				process_delete_queue [index].pid = 0;
				process_delete_queue [index].retry = 0;
				break;
			}
			usleep (10000);
		}
		index ++;
	}
}

static void *thread_autoclear (void *UNUSED_VAR (null))
{
	LOG_SESSION *pls;

	unsigned long long total_size = 0;
	unsigned long long free_size;
	int processed, i, j;
	int session;
	char sc;

	prctl (PR_SET_NAME, (unsigned long) "lognet:autoclear", 0, 0, 0);

	pthread_detach (pthread_self ());

	i = -1;

	sem_getvalue (& autoclear_lock, & i);

	if (i <= 0)
	{
		DM ("another auto clear thread is running!\n");
		return NULL;
	}

	sem_wait (& autoclear_lock);

	pthread_mutex_lock (& data_lock);
	pthread_mutex_lock (& sn_lock);
	DM ("in autoclear thread. \n");

	sc = dir_get_storage_code (log_path);

	read_timestampes_nolock ();

	/*
	 * clear by size limit
	 */
	total_size = count_total_size_nolock ();

	processed = atoi (log_limit);

	if (processed <= 0)
	{
		processed = 500;
	}

	free_size = (unsigned long long) processed * MB /* MB to bytes */;

	if (total_size > free_size)
	{
		DM ("reach size limitation, log size is %llu bytes, limitation is %llu bytes.\n", total_size, free_size);

		for (processed = 0, pls = sessions; (total_size > free_size) && (session_count > 1); pls ++, session_count --)
		{
			unlink_log_files_by_session_nolock (pls, "sizelimit", sc);

			total_size -= (total_size > pls->size) ? pls->size : total_size;

			processed ++;
		}

		if (processed)
		{
			DM ("reduce log size to %llu bytes.\n", total_size);

			memmove (sessions, pls, session_count * sizeof (LOG_SESSION));
		}
		else /* nothing was done, maybe we have only one session */
		{
			DM ("[sizelimit] nothing to clear.\n");
		}
	}

	DM ("total size = %lld bytes", total_size);

	/*
	 * clear process delete queue
	 */
	clear_process_delete_queue ();

	write_timestampes_nolock ();

	pthread_mutex_unlock (& sn_lock);
	pthread_mutex_unlock (& data_lock);

	sem_post (& autoclear_lock);

	return NULL;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	pthread_t thread = (pthread_t) -1;

	int exp_counter = 10000;
	int file_missing_counter = 0;
	int z;

	prctl (PR_SET_NAME, (unsigned long) "lognet:monitor", 0, 0, 0);

	mask = 0;

	while (! done)
	{
		if (++ exp_counter > 9000 /* not exactly one day */)
		{
			if (is_expired ())
			{
				DM ("expired!");

				/*
				 * force exiting whole process
				 */
				exit (1);
			}
			exp_counter = 0;
		}

		z = lognet_islogging ();

		if (z & LOGGER_STATE_MASK_ERROR_MISSING /* log file was disappeared */)
		{
			if ((mask & LOGGER_STATE_MASK_ERROR_MISSING) == 0)
			{
				mask |= LOGGER_STATE_MASK_ERROR_MISSING;

				DM (LOG_TAG ": [embedded] detected lognet file disappeared!\n");
			}
			else
			{
				DM ("detected log file disappeared! ... %d\n", ++ file_missing_counter);

				if (file_missing_counter >= LOGGER_ERROR_TOLERANT_COUNT)
				{
					file_missing_counter = 0;

					if (process_queue_count == 0)
					{
						lognet_stop ("need to restart logger due to log file disappeared");
						lognet_start ();
					}
					else
					{
						DM ("forked process cannot be terminated, wait to restart logger, ... %d\n", process_queue_count);
					}
				}
			}
		}
		else if (z & LOGGER_STATE_MASK_ERROR_DEFUNCT /* defunct */)
		{
			if ((mask & LOGGER_STATE_MASK_ERROR_DEFUNCT) == 0)
			{
				mask |= LOGGER_STATE_MASK_ERROR_DEFUNCT;

				file_log (LOG_TAG ": [embedded] detected lognet defunct!\n");
			}
		}
		else if (z & LOGGER_STATE_MASK_ERROR_ZOMBIE /* zombi */)
		{
			lognet_reset_sn ();
			lognet_start ();
			mask = 0;
		}
		else if (z & LOGGER_STATE_MASK_STOPPED /* not logging */)
		{
			mask = 0;
		}
		else
		{
			if (mask & LOGGER_STATE_MASK_ERROR_MISSING)
			{
				/* resume from zombie */
				mask &= ~LOGGER_STATE_MASK_ERROR_MISSING;

				file_missing_counter = 0;

				DM (LOG_TAG ": [embedded] detected lognet file appeared!\n");
			}
			if (mask & LOGGER_STATE_MASK_ERROR_DEFUNCT)
			{
				mask &= ~LOGGER_STATE_MASK_ERROR_DEFUNCT;

				file_log (LOG_TAG ": [embedded] defunct lognet resumed!\n");
			}

			if (pthread_create (& thread, NULL, thread_autoclear, NULL) < 0)
			{
				DM ("pthread_create: %s\n", strerror (errno));
			}
		}

		while ((timed_wait (& monitor_lock, 10000) < 0) && (errno == EINTR /* interrupted by a signal */));
	}

end:;
	DM ("stop monitoring thread ...\n");
	return NULL;
}

int lognet_main (int server_socket)
{
	pthread_t thread_monitor = (pthread_t) -1;

	char buffer [PATH_MAX + 16];
	char *ptr;

	int ret = 0;
	int commfd = -1;
	int z;

	if (access ("/system/xbin/tcpdump", X_OK) == 0)
	{
		strncpy (log_command, "/system/xbin/tcpdump", sizeof (log_command) - 1);
	}
	else if (access ("/system/bin/tcpdump", X_OK) == 0)
	{
		strncpy (log_command, "/system/bin/tcpdump", sizeof (log_command) - 1);
	}
	else if (access ("/data/bin/tcpdump", X_OK) == 0)
	{
		strncpy (log_command, "/data/bin/tcpdump", sizeof (log_command) - 1);
	}
	else if (access ("/data/bin/htctcpdump", X_OK) == 0)
	{
		strncpy (log_command, "/data/bin/htctcpdump", sizeof (log_command) - 1);
	}
	log_command [sizeof (log_command) - 1] = 0;

	if (dir_select_log_path (log_path, sizeof (log_path)) < 0)
		return -1;

	sem_init (& monitor_lock, 0, 0);
	sem_init (& autoclear_lock, 0, 1);

	if (pthread_create (& thread_monitor, NULL, thread_main, NULL) < 0)
	{
		DM ("pthread_create: %s\n", strerror (errno));
		thread_monitor = (pthread_t) -1;
	}

	while (! done)
	{
		DM ("waiting connection ...\n");

		commfd = wait_for_connection (server_socket);

		if (commfd < 0)
		{
			DM ("accept client connection failed!\n");
			continue;
		}

		DM ("connection established.\n");

		for (;;)
		{
			memset (buffer, 0, sizeof (buffer));

			ret = read (commfd, buffer, sizeof (buffer));

			if (ret <= 0)
			{
				DM ("read command error (%d)! close connection!\n", ret);
				break;
			}

			buffer [sizeof (buffer) - 1] = 0;

			ret = 0;

			DM ("read command [%s].\n", buffer);

			if (! is_thread_alive (thread_monitor))
			{
				thread_monitor = (pthread_t) -1;
			}

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				done = 1;
				break;
			}

			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				/* islogging even is zombie */
				sprintf (buffer, "%d", (lognet_islogging () & LOGGER_STATE_MASK_STOPPED) ? 0 : 1);
			}
			else if (CMP_CMD (buffer, LOG_GETPATH))
			{
				pthread_mutex_lock (& data_lock);
				if (log_filename [0])
				{
					strcpy (buffer, log_filename);
				}
				else
				{
					strcpy (buffer, log_path);
				}
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_SETPATH))
			{
				if (lognet_islogging () & LOGGER_STATE_MASK_STOPPED)
				{
					MAKE_DATA (buffer, LOG_SETPATH);

					if (dir_select_log_path (buffer, sizeof (buffer)) < 0)
					{
						ret = -1;
					}
					else
					{
						pthread_mutex_lock (& data_lock);
						strncpy (log_path, buffer, sizeof (log_path));
						log_path [sizeof (log_path) - 1] = 0;
						pthread_mutex_unlock (& data_lock);
					}
				}
				else
				{
					DM ("do not allow to change path while running!\n");
					ret = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_GETPARAM))
			{
				pthread_mutex_lock (& data_lock);
				sprintf (buffer, "%s:%s:%s", log_size, log_rotate, log_limit);
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_SETPARAM))
			{
				if (lognet_islogging () & LOGGER_STATE_MASK_STOPPED)
				{
					MAKE_DATA (buffer, LOG_SETPARAM);
					pthread_mutex_lock (& data_lock);
					datatok (buffer, log_size);
					datatok (buffer, log_rotate);
					datatok (buffer, log_limit);
					pthread_mutex_unlock (& data_lock);
				}
				else
				{
					DM ("do not allow to change parameters while running!\n");
					ret = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				if (lognet_islogging () & LOGGER_STATE_MASK_STOPPED)
				{
					ndc_start ();
					ret = lognet_start ();
				}
				else
				{
					ret = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_STOP))
			{
				if (lognet_islogging () & LOGGER_STATE_MASK_STOPPED)
				{
					ret = -1;
				}
				else
				{
					lognet_stop ("got stop command");
					lognet_reset_sn ();
					ndc_stop ();
				}
				buffer [0] = 0;
			}
			else
			{
				DM ("unknown command [%s]!\n", buffer);
				ret = -1;
				buffer [0] = 0;
			}

			/* command response */
			if (buffer [0] == 0)
				sprintf (buffer, "%d", ret);

			DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close_nointr (commfd);
		commfd = -1;

		ret = 0;
	}

	z = lognet_islogging ();

	if (z & LOGGER_STATE_MASK_ERROR_ZOMBIE)
	{
		pthread_mutex_lock (& data_lock);
		clear_process_nolock (1);
		file_log (LOG_TAG ": %d stopped\n", pid);
		pthread_mutex_unlock (& data_lock);
	}
	else if (! (z & LOGGER_STATE_MASK_STOPPED))
	{
		lognet_stop ("lognet service stopped");
	}

	lognet_reset_sn ();

	if (thread_monitor != (pthread_t) -1)
	{
		/* quit thread */
		sem_post (& monitor_lock);
		pthread_join (thread_monitor, NULL);
		thread_monitor = (pthread_t) -1;
	}

	/* reset done flag */
	done = 0;

	return ret;
}
