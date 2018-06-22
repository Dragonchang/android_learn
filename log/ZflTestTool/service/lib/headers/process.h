#ifndef _PARTNER_HTC_SSDTEST_PROCESS_H_
#define _PARTNER_HTC_SSDTEST_PROCESS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>

#include "headers/glist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROCESS_STATE_UNKNOWN	'?'
#define PROCESS_STATE_ZOMBIE	'Z'
#define PROCESS_STATE_DEFUNCT	'D'

#define IS_PROCESS_STATE_UNKNOWN(s)	(s == PROCESS_STATE_UNKNOWN)
#define IS_PROCESS_STATE_ZOMBIE(s)	(s == PROCESS_STATE_ZOMBIE)
#define IS_PROCESS_STATE_DEFUNCT(s)	(s == PROCESS_STATE_DEFUNCT)

typedef struct {
	pid_t pid;
	pid_t ppid;
	char bin [64];
} PID_INFO;

typedef struct {
	int fd;
	struct stat st;
	char link [64];
} FD_INFO;

extern int find_all_pids_of_bin (const char *bin_name, pid_t *pid_array, int array_count);
extern GLIST *find_all_pids (void);
extern GLIST *find_all_fds (void);
extern void close_all_fds (GLIST *fds);
extern int is_process_alive (int pid);
extern int is_process_zombi (int pid);
extern int is_thread_alive (pthread_t tid);
extern char get_pid_stat (pid_t pid);
extern int get_pid_name (pid_t pid, char *buffer, int buflen);
extern int get_pid_cmdline (pid_t pid, char *buffer, int buflen);
extern void get_ppid (const pid_t pid, pid_t * ppid);
extern pid_t getpppid (void);
extern int kill_signal (pid_t pid, const char *name, const char *cmdline, int sig);

extern void system_in_thread (const char *command);

extern char *alloc_waitpid_status_text (int status);

extern void dump_environ (void);
extern void dump_process (pid_t pid);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* _PARTNER_HTC_SSDTEST_PROCESS_H_ */
