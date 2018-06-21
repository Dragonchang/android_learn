#ifndef	_SERVICE_COMMON_H_
#define	_SERVICE_COMMON_H_

  #include <cutils/log.h>
  #include <cutils/properties.h>
  #define APP_DIR "/tmp/"
  #define BIN_DIR APP_DIR "bin/"
  #define LIB_DIR APP_DIR "lib/"
  #define DAT_DIR APP_DIR "data/"
  #define TMP_DIR APP_DIR "tmp/"

  #define LOG_FOLDER_NAME  "zfllog/"
  #define LOG_DIR  DAT_DIR LOG_FOLDER_NAME

  #define GHOST_FOLDER_NAME  "ghost/"
  #define GHOST_DIR  DAT_DIR GHOST_FOLDER_NAME

  #define LOG_DATA_EXT "stt"
  #define LOG_FILE_TAG ""


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>

#ifndef	PATH_MAX
  #define PATH_MAX		(256 + 2)
#endif

#include "headers/str.h"
#include "headers/fio.h"
#include "headers/glist.h"
#include "headers/conf.h"
#include "headers/dir.h"
#include "libcommon.h"
extern int debug_more;
#define BUILD_AND

#ifdef BUILD_AND
  //#ifdef FORCE_STDOUT
    //#define DM(...) printf("["LOG_TAG"]: " __VA_ARGS__)
  //#else
    #define DM(...) ALOGD(__VA_ARGS__)
  //#endif
#else
  #define DM(...) ALOGD(__VA_ARGS__)
#endif

#ifndef	SOCKET_PORT_SERVICE
  #define SOCKET_PORT_SERVICE	61500
#endif

#ifndef	SOCKET_PORT_LAST
  #define SOCKET_PORT_LAST	65500
#endif

#define MAX_NAME_LEN	32
#define MAX_ARGUMENT_LEN	64

typedef struct {
	const char name [MAX_NAME_LEN];
	const int no_socket;
	int sockfd;
	int port;
	int (*proc) (int sockfd);
} SERVICE;

extern void send_command_to_service (int force_start, const char *service_name, const char *command, char *rbuffer, int rlen);

#define	SERVICE_DECLARE(name)		extern int name##_main (int)
#define	SERVICE_ENTRY(name)		{#name,0,-1,-1,name##_main}
#define	SERVICE_ENTRY_NOSOCKET(name)	{#name,1,-1,-1,name##_main}
#define	SERVICE_ENTRY_END		{"",1,-1,-1,NULL}

#define	IS_CMD(s)		(s[0]==':')
#define	CMP_CMD(s,cmd)		(!strncmp(s,cmd,strlen(cmd)))
#define	MAKE_DATA(s,cmd)\
{\
	char *_p = & s [strlen (cmd)];\
	int _len = strlen (_p);\
	memmove (s, _p, _len);\
	s [_len] = 0;\
}
#define	SAFE_SPRINTF(b,l,f,...)\
{\
	void *__pb = b;\
	if (__pb && (l > 0))\
	{\
		snprintf (b, l, f, __VA_ARGS__);\
		b [l - 1] = 0;\
	}\
}

#define	IS_VALUE_TRUE(s)	((s != NULL) && ((s [0] == '1') || (strcasecmp (s, "true") == 0)))

extern void local_uevent_initial (void);
extern void local_uevent_destroy (void);
extern int local_uevent_register (const char *key); /* return fd */
extern void local_uevent_unregister (const char *key);
extern void local_uevent_dispatch (const char *uevent);
extern void local_uevent_try_to_end (void);
extern int local_uevent_is_ended (const char *uevent); /* non-zero to end */

extern int datatok (char *from, char *to);
extern int db_init (const char *filepath);
extern void db_destroy (void);
extern const char *db_get (const char *name, const char *default_value);
extern int db_set (const char *name, const char *value);
extern int db_remove (const char *name);
extern void db_dump (void);

extern int do_bugreport (char *iobuf, int iobuflen);
extern int do_emergency_dump (const char *prefix, char *iobuf, int iobuflen);

extern void dump_properties_and_attributes (const char **properties, const char **attributes);

#define	LOG_DEFAULT_ROTATE_SIZE		"5"
#define	LOG_DEFAULT_ROTATE_COUNT	"39"
#define	LOG_DEFAULT_SESSION		"10"
#define	LOG_DEFAULT_SIZE_LIMIT		"2048"
#define	LOG_DEFAULT_SIZE_RESERVED	"300"
#define	LOG_DEFAULT_AUTOCLEAR		"true"
#define	LOG_DEFAULT_COMPRESS		"false"

#define	LOG_FILE_MODE			0666

#define	LOGGER_FILETYPE_UNKNOWN		(0)
#define	LOGGER_FILETYPE_FILE		(1)
#define	LOGGER_FILETYPE_DATA		(2)
#define	LOGGER_FILETYPE_LOGGING_FILE	(3)
#define	LOGGER_FILETYPE_LOGGING_DATA	(4)

/* logger state bits */
#define	LOGGER_STATE_MASK_STARTED		(0x00000001)
#define	LOGGER_STATE_MASK_STOPPED		(0x00000002)
#define	LOGGER_STATE_MASK_LOGGING		(0x00000004)
#define	LOGGER_STATE_MASK_REQUEST_REMOVE_OLDLOG	(0x00000010)
/* error bits */
#define	LOGGER_STATE_MASK_ERROR_ALL		(0x0000FF00)
#define	LOGGER_STATE_MASK_ERROR_FATAL		(0x00000F00)
#define	LOGGER_STATE_MASK_ERROR_OPEN		(0x00000100)
#define	LOGGER_STATE_MASK_ERROR_READ		(0x00000200)
#define	LOGGER_STATE_MASK_ERROR_WRITE		(0x00000400)
#define	LOGGER_STATE_MASK_ERROR_PRSTAT		(0x00001000)
#define	LOGGER_STATE_MASK_ERROR_ZOMBIE		(0x00002000)
#define	LOGGER_STATE_MASK_ERROR_DEFUNCT		(0x00004000)
#define	LOGGER_STATE_MASK_ERROR_MISSING		(0x00008000)
/* function bits */
#define	LOGGER_STATE_MASK_COMPRESS		(0x01000000)

#define	LOGGER_ERROR_TOLERANT_COUNT	(3)

#define SESSION_DATA_FILE_VERSION_ID	"v1"

/* for loggers */
typedef struct {
	char timestamp [TAG_DATETIME_LEN + 1];
	unsigned long long size;
	unsigned short sn;
	char reserved [6];
	char devinfo [16]; /* device serial number (ro.serialno) */
	char prdinfo [16]; /* product name (ro.build.product) */
	char rominfo [16]; /* rom version (ro.build.description) */
} LOG_SESSION;

typedef struct {
	int pid;
	int retry;
} PROCESS_DELETE_QUEUE;

#define	KEY_STORAGE_PREFIX	"Storage"
#define	KEY_LOGGER_PREFIX	"Logger"

#define	KEY_EXTRA_CONFIG_PROMPT		"Prompt"
#define	KEY_EXTRA_CONFIG_DATA_TYPE	"Type"
#define	KEY_EXTRA_CONFIG_DEFAULT_VALUE	"Default"

#define	STORAGE_CONFIG_KEY(___name,___key)	(KEY_STORAGE_PREFIX "." ___name "." ___key)
#define	LOGGER_CONFIG_KEY(___name,___key)	(KEY_LOGGER_PREFIX "." ___name "." ___key)

#define	LOGGER_EXTRA_CONFIG_DATA_TYPE_STRING	"string"
#define	LOGGER_EXTRA_CONFIG_DATA_TYPE_BOOLEAN	"boolean"
#define	LOGGER_EXTRA_CONFIG_DATA_TYPE_INTEGER	"integer"
#define	LOGGER_EXTRA_CONFIG_DATA_TYPE_FLOAT	"float"
#define	LOGGER_EXTRA_CONFIG_DATA_TYPE_LIST	"list" // when it's type list, the "value" and "default_value" fields should be an integer index.

typedef struct {
	const char *prompt;
	const char *data_type;
	const char *conf_key;
	const char *default_value;
	char *value;
	unsigned long value_len;
} LOGGER_EXTRA_CONFIG;

#define	LOGDATA_MAGIC		0x89124138
#define	LOGDATA_COUNT		(5000)
#define	LOGDATA_FILE_BUFLEN	(96) /* 104 - sizeof (unsigned long long) */
#define	LOGDATA_PREFIX_BUFLEN	(LOGDATA_FILE_BUFLEN - 78) /* session(5)+serialno(5)+timestamp(16)+devinfo(16)+prdinfo(16)+rominfo(16).txt(4)=78 */

typedef struct
{
	unsigned long magic;
	unsigned long entry_count;
	unsigned long entry_size;
	unsigned long index_head;
	unsigned long index_tail;
	unsigned long long total_size __attribute__ ((aligned (8)));
	unsigned long header_size;
	char reserved [4];
} LOGDATA_HEADER;

typedef struct
{
	char file [LOGDATA_FILE_BUFLEN];
	unsigned long long size __attribute__ ((aligned (8)));
} LOGDATA;

typedef struct {
	/*
	 * this structure will be used in a thread, please make sure the given address
	 * of name and log_filename can be accessed (do not free) during the thread lifecycle.
	 */
	char *name;
	char *log_filename;
	int rotated;
	int pipefd;
	int error; /* PIPE_ROUTER_ERROR_xxx */
} PIPEINFO;

#define	PIPE_ROUTER_ERROR_NONE	(0)
#define	PIPE_ROUTER_ERROR_OPEN	(1)
#define	PIPE_ROUTER_ERROR_READ	(2)
#define	PIPE_ROUTER_ERROR_WRITE	(3)

extern void logger_common_pipe_filename (const char *name, char *buffer, int len);
extern int logger_common_pipe_validate (const char *name, const char *pipefile, int show_log);
extern int logger_common_pipe_create (const char *name, const char *pipefile);
extern int logger_common_pipe_open (const char *name, const char *pipefile);
extern int logger_common_pipe_close (const char *name, int fd);
extern int logger_common_pipe_have_data (const char *name, int fd);
extern int logger_common_pipe_read (const char *name, int fd, char *buffer, size_t count);
extern void *logger_common_pipe_router_thread (void *pipeinfo /* PIPEINFO & */);

extern void logger_common_logdata_filename (const char *path_with_slash, const char *file_prefix, char *buffer, int len);
extern int logger_common_logdata_open (const char *name, const char *path_with_slash, const char *file_prefix);
extern int logger_common_logdata_limit_size (int fd, const char *name, const char *path_with_slash, unsigned long limited_size_mb, unsigned long reserved_size_mb, unsigned long long *total_size);
extern int logger_common_logdata_add_file (int fd, const char *name, const char *path_with_slash, const char *log_filename);
extern int logger_common_logdata_compress_file (int fd, const char *name, const char *log_filename);

extern int logger_common_update_state (const char *name, int state, pid_t pid, pthread_t tid, const char *log_filename, int check_pipe);
extern int logger_common_need_rotate (const char *name, const char *log_filename, unsigned long rotate_size_mb);
extern int logger_common_generate_new_file (const char *name, const char *file_prefix, char *buffer, int len);

extern int logger_common_check_file_type (const char *name, const char *filepath, const char *file_prefix, const char *log_filename);
extern int logger_common_get_overload_size (const char *name);

/* commands */
#define	CMD_ENDSERVER			":endserver:"
#define	CMD_GETVER			":getver:"
#define	CMD_GETPORT			":getport:"
#define	CMD_LISTSERVICES		":listservices:"
#define	CMD_HELP			":help:"

#define	PROPERTY_BOOT_COMPLETED		"sys.boot_completed"		/* android boot completed */
#define	PROPERTY_BOOT_COMPLETED_TOOL	"sys.boot_completed.stt"	/* sync with Device.PROPERTY_BOOT_COMPLETED, indicate tool java layer received boot completed intent or not */
#define	PROPERTY_BOOT_COMPLETED_COS	"sys.boot_completed_android"	/* indicate COS java runtime is ready or not */
#define	PROPERTY_FACTORY_MODE	"ro.bootmode"	/* check factory boot mode property */

#define PROPERTY_CRYPTO_STATE	"ro.crypto.state"
#define PROPERTY_VOLD_DECRYPT	"vold.decrypt"
#define PROPERTY_CRYPTO_TYPE	"ro.crypto.type"

#endif
