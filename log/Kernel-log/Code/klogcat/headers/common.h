

#ifndef	_HTC_SERVICE_COMMON_H_
#define	_HTC_SERVICE_COMMON_H_

#include <cutils/log.h>
#include <cutils/properties.h>
#include "libcommon.h"

#include <stdio.h>
#include <pthread.h>

#ifndef	PATH_MAX
  #define PATH_MAX		(256 + 2)
#endif

#include "headers/str.h"
#include "headers/fio.h"
#include "headers/glist.h"
#include "headers/dir.h"

extern int debug_more;

#ifdef BUILD_AND
  #ifdef FORCE_STDOUT
    #define DM(...) printf("["LOG_TAG"]: " __VA_ARGS__)
  #else
    #define DM(...) LOGD(__VA_ARGS__)
  #endif
#else
  #define DM(...) printf("["LOG_TAG"]: " __VA_ARGS__)
#endif

#ifndef	SOCKET_PORT_SERVICE
  #define SOCKET_PORT_SERVICE	61500
#endif

#ifndef	SOCKET_PORT_LAST
  #define SOCKET_PORT_LAST	65500
#endif

#define MAX_NAME_LEN	32

typedef struct {
	const char name [MAX_NAME_LEN];
	const int no_socket;
	int sockfd;
	int port;
	int (*proc) (int sockfd);
} SERVICE;

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
#define	LOG_FILE_MODE			0600

#define KERNEL_LOG_SOURCE 	"/proc/kmsg"
#define LAST_KMSG_SOURCE 	"/proc/last_kmsg"
#define LAST_KMSG_SOURCE_2	"/sys/fs/pstore/console-ramoops"

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

extern void logger_common_logdata_filename (const char *path_with_slash, const char *file_prefix, char *buffer, int len);
extern int logger_common_logdata_open (const char *name, const char *path_with_slash, const char *file_prefix);
extern int logger_common_logdata_limit_size (int fd, const char *name, const char *path_with_slash, unsigned long limited_size_mb, unsigned long reserved_size_mb, unsigned long long *total_size);
extern int logger_common_logdata_add_file (int fd, const char *name, const char *path_with_slash, const char *log_filename);
extern int logger_common_logdata_compress_file (int fd, const char *name, const char *log_filename);

extern int logger_common_update_state (const char *name, int state, pid_t pid, pthread_t tid, const char *log_filename, int check_pipe);
extern int logger_common_need_rotate (const char *name, const char *log_filename, unsigned long rotate_size_mb);
extern int logger_common_generate_new_file (const char *name, const char *file_prefix, char *buffer, int len);

extern int logkmsg_start (const char *);
extern int logkmsg_stop (const char *);
extern int logkmsg_rotate_and_limit (const char *, int, unsigned long, unsigned long, unsigned long);

extern int  m_nEnableDebug;
extern char m_szPath [PATH_MAX];
extern int  m_nFileToSave;
extern int  m_nKBytes;
extern int  m_nCount;
extern int  m_nDumpAndExit;
extern int  m_nDumpType;

extern int m_nDone;
extern pthread_t working;

#endif

