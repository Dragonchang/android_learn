#define	LOG_TAG		"STT:logkmsg"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/klog.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include <cutils/sockets.h>

#include "headers/sem.h"
#include "headers/fio.h"
#include "headers/dir.h"
#include "headers/poll.h"
#include "headers/process.h"

#include "common.h"
#include "server.h"

#define	VERSION	"4.3"
/*
 * 4.3	: Do not set thread id to -1 inside thread to prevent timing issue.
 * 4.2	: add "PROP_VALUE_MAX" property check to avoid BB on K444 ROM build.
 * 4.1	: add more logs controlled by "persist.debug.logkmsg" property in order to check why time_lock semaphore is always locked.
 * 4.0	: reset log overload criteria while restarting loggers.
 * 3.9	: check if kernel log buffer size overloaded or not. If overloaded, print warning message on kernel log for server side parsing.
 * 3.8	: replace null string with blank space on the buffer to avoid failing to find last string on the buffer.
 * 3.7	: support new logger control service.
 * 3.6	: improve rotate file speed.
 * 3.5	: check also missing logging file.
 * 3.4	: add a watchdog to monitor logger status.
 * 3.3	: service dumpdmesg was renamed to dk.
 * 3.2	: support getting kernel logs from  dumpdmesg service.
 * 3.1	: share log session with other loggers.
 * 3.0	: 1. remove socket interface. 2. sync with Smith.
 * 2.9	: handle SIGTERM and SIGHUP signals.
 * 2.8	: add storage code to identify the log path.
 * 2.7	: also show system clock in nanoseconds.
 * 2.6	: support sn.
 * 2.5	: 1. do not call fsync() when debugtool.fsync=0. 2. call emergency dump when write failed.
 * 2.4	: keep local logs.
 * 2.3	: support auto select log path.
 * 2.2	: use session structure.
 * 2.1	: create timestamp list.
 * 2.0	: try to read all logs from ring buffer at first time.
 * 1.9	: use signal 0 to check alive status.
 * 1.8	: 1. check thread alive status by sending SIGUSR2. 2. stop/start ebdlogd service when log started/stopped.
 * 1.7	: add update timestamp in kernel log.
 * 1.6	: 1. set default path to LOG_DIR, 2. set file mode to 0666
 * 1.5	: add external apis for logctl
 * 1.4	: stop logging if write failed
 * 1.3	: change default value by SQA's request
 * 1.2	: complete the rotation function
 * 1.1	: also supprts :getparam: and :setparam: commands
 */

#define	FILE_PREFIX	"kernel_" LOG_FILE_TAG

#define	DEBUG_LOGKMSG_PROPERTY		"persist.debug.logkmsg"

#define TIMEGEN (1)
#define SHOW_DUMPDMESG_SIZE (0)

/* CONFIG_LOG_BUF_SHIFT from kernel, sync with system/core/toolbox/dmesg.c */
#define KLOG_BUF_SHIFT	17
#define KLOG_BUF_LEN	(1 << KLOG_BUF_SHIFT)

#define KLOG_BUF_OVERLOAD_SIZE	65536

#ifndef BUILD_AND
#define KLOG_CLOSE      0
#define KLOG_OPEN       1
#define KLOG_READ       2
#define KLOG_READ_ALL   3
#define KLOG_READ_CLEAR 4
#define KLOG_CLEAR      5
#define KLOG_DISABLE    6
#define KLOG_ENABLE     7
#define KLOG_SETLEVEL   8
#define KLOG_UNREADSIZE 9
#define KLOG_WRITE      10
#endif

#ifndef PROP_VALUE_MAX
#define PROP_VALUE_MAX  92
#endif

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t working = (pthread_t) -1;
static sem_t timed_lock;

static int done = 0;
static char log_filename [PATH_MAX] = "";
static unsigned long long total_size = 0;

static int write_error_flag = 0;
static time_t watchdog_lastchecked = 0;
static unsigned long watchdog_counter = 0;
static int overload = 0;

static int verbose = 0;

static void set_debug_logkmsg_level (void)
{
	char tmp [PROP_VALUE_MAX];
	int ret = property_get (DEBUG_LOGKMSG_PROPERTY, tmp, "0");

	// Set defualt verbose to debug.
	//verbose = 3;

	if (ret > 0)
	{
		verbose = atoi (tmp);

		if (verbose < 0) verbose = 0;
		if (verbose > 10) verbose = 10;
	}
}

int logkmsg_watchdog (const char *UNUSED_VAR (name))
{
	int ret = 0;

	if (is_thread_alive (working))
	{
		time_t now = time (NULL);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_watchdog, pthread_mutex_lock() +++\n");

		pthread_mutex_lock (& data_lock);

		if ((now > watchdog_lastchecked) && (watchdog_counter == 0))
		{
			ret = 1;
		}

		DM ("logkmsg_watchdog: [embedded] counter=%lu in %ld seconds\n", watchdog_counter, ((long) now - (long) watchdog_lastchecked));

		if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_watchdog, counter[%lu] in %ld seconds ---\n", watchdog_counter, ((long) now - (long) watchdog_lastchecked));

		watchdog_lastchecked = now;
		watchdog_counter = 0;

		pthread_mutex_unlock (& data_lock);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_watchdog, pthread_mutex_lock() ---\n");
	}
	return ret;
}

int logkmsg_get_overload_size (const char *name)
{
	return logger_common_get_overload_size (name);
}

static void get_cur_time (char *buffer, int len)
{
	struct timespec ts;
	struct tm *ptm;
	time_t t;

	if (verbose > 1) file_log (LOG_TAG ": [embedded] get_cur_time() buffer[%s], len[%d] +++\n", buffer, len);

	if (clock_gettime (CLOCK_MONOTONIC, & ts) < 0)
	{
		ts.tv_sec = ts.tv_nsec = 0;
	}

	if (verbose > 1) file_log (LOG_TAG ": [embedded] get_cur_time()\n");

	t = time (NULL);

	ptm = localtime (& t);

	buffer [0] = 0;

	snprintf (buffer, len, "%04d/%02d/%02d %02d:%02d:%02d (%lu.%lu)",
			ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
			ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
			(unsigned long) ts.tv_sec, (unsigned long) ts.tv_nsec);

	buffer [len - 1] = 0;

	if (verbose > 1) file_log (LOG_TAG ": [embedded] get_cur_time() buffer[%s] ---\n", buffer);
}

#if TIMEGEN
static pthread_t timegen = (pthread_t) -1;
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

static char curtime [64] = "";
static char prevtime [64] = "";

static void *thread_timegen (void *UNUSED_VAR (null))
{
	prctl (PR_SET_NAME, (unsigned long) "logkmsg:timegen", 0, 0, 0);

	for (; ! done;)
	{
		if (verbose > 1) file_log (LOG_TAG ": [embedded] thread_timegen pthread_mutex_lock(time_lock) curtime[%s], prevtime[%s] +++\n", curtime, prevtime);

		pthread_mutex_lock (& time_lock);

		get_cur_time (curtime, sizeof (curtime));

		pthread_mutex_unlock (& time_lock);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] thread_timegen pthread_mutex_lock(time_lock) curtime[%s], prevtime[%s] ---\n", curtime, prevtime);

		sleep (1);
	}

	DM ("end of timegen thread ...\n");

	if (verbose > 1) file_log (LOG_TAG ": [embedded] thread_timegen end, curtime[%s], prevtime[%s].\n", curtime, prevtime);

	return NULL;
}
#endif

static int use_htc_dk_service = 0;

static int read_from_htc_dk_service (char *buffer, int len)
{
	int socket_fd, count, rlen;

	for (count = 1; count <= 10; count ++)
	{
		if ((socket_fd = socket_local_client ("htc_dk", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM)) >= 0)
			break;

		DM ("failed to connect to htc_dk service (%d)! %s\n", count, strerror (errno));

		sleep (1);
	}

	count = -1;

	if (socket_fd >= 0)
	{
		count = 0;

		/*
		 * it may need multiple times to read data from socket
		 */
		//while (poll_check_data (socket_fd))
		while (1)
		{
			if ((rlen = read_nointr (socket_fd, & buffer [count], len - count)) < 0)
			{
				DM ("failed to read data from htc_dk service! %s\n", strerror (errno));
				count = -1;
				break;
			}

			if (rlen == 0) /* no more data */
				break;

		#if SHOW_DUMPDMESG_SIZE
			DM ("htc_dk read_nointr = %d\n", rlen);
		#endif

			count += rlen;

			if (count >= len)
			{
				DM ("htc_dk read not enough buffer!\n");
				break;
			}
		}

		close_nointr (socket_fd);
	}

#if SHOW_DUMPDMESG_SIZE
	DM ("htc_dk read_nointr total = %d\n", count);
#endif

	if (count > 0)
	{
		char *ptr;

		/*
		 * remove tailing useless new lines
		 */
		if (buffer [count - 1] == '\n')
		{
			for (ptr = & buffer [count - 1], rlen = 0; (ptr != buffer) && ((*ptr == '\r') || (*ptr == '\n')); ptr --, rlen ++);

			if ((*(ptr + 1) == '\n') && ((*(ptr + 2) == '\r') || (*(ptr + 2) == '\n')))
			{
				count -= rlen - 1;
			}
		}
		else
		{
			for (ptr = & buffer [count - 1], rlen = 0; (ptr != buffer) && (*ptr != '\n'); ptr --, rlen ++);

			if (*ptr == '\n')
			{
				count -= rlen;
			}
		}

	#if 0
		/*
		 * remove incomplete header data
		 */
		if ((buffer [0] != '<') || (buffer [2] != '>'))
		{
			*head = strchr (buffer, '\n');

			if (*head) *head ++;
		}
	#endif

	#if SHOW_DUMPDMESG_SIZE
		DM ("htc_dk count fixed = %d\n", count);
	#endif
	}

	return count;
}

static char kbuf [192000]; /* the size must > KLOG_BUF_LEN */
static char last [1024];
static char state [128];

static void debugdump (int count)
{
	int fd;

	unlink (DAT_DIR "info.txt");
	unlink (DAT_DIR "kernel.buf");
	unlink (DAT_DIR "kernel.txt");
	unlink (DAT_DIR "last.buf");
	unlink (DAT_DIR "last.txt");

	if ((fd = open_nointr (DAT_DIR "info.txt", O_CREAT | O_WRONLY, 0666)) >= 0)
	{
		char info [128];
		snprintf (info, sizeof (info), "kernel count=%d\nkernel len=%d\nlast len=%d\n", count, (int) strlen (kbuf), (int) strlen (last));
		info [sizeof (info) - 1] = 0;
		write_nointr (fd, info, strlen (info));
		close_nointr (fd);
	}

	if ((fd = open_nointr (DAT_DIR "kernel.buf", O_CREAT | O_WRONLY, 0666)) >= 0)
	{
		write_nointr (fd, kbuf, sizeof (kbuf));
		close_nointr (fd);
	}

	if ((fd = open_nointr (DAT_DIR "kernel.txt", O_CREAT | O_WRONLY, 0666)) >= 0)
	{
		write_nointr (fd, kbuf, strlen (kbuf));
		close_nointr (fd);
	}

	if ((fd = open_nointr (DAT_DIR "last.buf", O_CREAT | O_WRONLY, 0666)) >= 0)
	{
		write_nointr (fd, last, sizeof (last));
		close_nointr (fd);
	}

	if ((fd = open_nointr (DAT_DIR "last.txt", O_CREAT | O_WRONLY, 0666)) >= 0)
	{
		write_nointr (fd, last, strlen (last));
		close_nointr (fd);
	}
}

static void *thread_main (void *arg)
{
#if TIMEGEN
	char update [sizeof (curtime) << 1];
#endif
	const char *name = arg;

	int count;
	int fd = -1;
	char *head;
	char *ptr;
	int do_fsync = 1;
	int write_error = 0;

	prctl (PR_SET_NAME, (unsigned long) "logkmsg:logger", 0, 0, 0);

	sem_init (& timed_lock, 0, 0);

	property_get ("debugtool.fsync", kbuf, "1");

	do_fsync = (kbuf [0] == '1');

	DM ("do fsync=%d\n", do_fsync);

	file_log (LOG_TAG ": file=%s, klogctl=%d\n", log_filename, ! use_htc_dk_service);

	memset (state, 0, sizeof (state));
	memset (last, 0, sizeof (last));
	last [0] = 0;
	overload = 0;

	DM ("logging kernel messages ... done=%d\n", done);

	for (; ! done;)
	{
		if (verbose > 1) file_log (LOG_TAG ": [embedded] open_nointr() fd[%d] log_filename[%s] +++\n", fd, log_filename);

		if (fd < 0)
		{
			fd = open_nointr (log_filename, O_CREAT | O_RDWR | O_TRUNC, LOG_FILE_MODE);

			if (fd < 0)
			{
				file_log (LOG_TAG ": failed to open [%s]: %s\n", log_filename, strerror (errno));
				log_filename [0] = 0;
				break;
			}
		}

		if (verbose > 1) file_log (LOG_TAG ": [embedded] open_nointr() fd[%d] log_filename[%s] ---\n", fd, log_filename);

	#if TIMEGEN
		/*
		 * write timestamp
		 */
		update [0] = 0;

		if (verbose > 1) file_log (LOG_TAG ": [embedded] pthread_mutex_lock(time_lock) curtime[%s], prevtime[%s] +++\n", curtime, prevtime);

		pthread_mutex_lock (& time_lock);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] pthread_mutex_lock(time_lock) curtime[%s], prevtime[%s]\n", curtime, prevtime);

		if (strcmp (curtime, prevtime) != 0)
		{
			snprintf (update, sizeof(update), "Update: [%s]", curtime);

			update[sizeof(update)-1] = 0;

			strcpy (prevtime, curtime);
		}

		pthread_mutex_unlock (& time_lock);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] pthread_mutex_lock(time_lock) curtime[%s], prevtime[%s] ---\n", curtime, prevtime);

		/*
		 * if curtime is "", this will also be ""
		 */
		if (prevtime [0] == 0)
		{
			timed_wait (& timed_lock, 1000);
			continue;
		}
	#endif

		/*
		 * read kernel log
		 */
		if (use_htc_dk_service)
		{
			if (verbose > 1) file_log (LOG_TAG ": [embedded] read_from_htc_dk_service() +++\n");

			count = read_from_htc_dk_service (kbuf, sizeof (kbuf) - 1);

			if (verbose > 1) file_log (LOG_TAG ": [embedded] read_from_htc_dk_service() count[%d] ---\n", count);

			if (count < 0)
			{
				file_log (LOG_TAG ": failed to read from htc_dk service\n");
				break;
			}
		}
		else
		{
			if (verbose > 1) file_log (LOG_TAG ": [embedded] klogctl() +++\n");

			count = klogctl (KLOG_READ_ALL, kbuf, sizeof (kbuf) - 1);

			if (verbose > 1) file_log (LOG_TAG ": [embedded] klogctl() count[%d] ---\n", count);

			if (count < 0)
			{
				file_log (LOG_TAG ": klogctl: %s\n", strerror (errno));
				break;
			}
		}

		/*
		 * replace null string with blank space on the buffer to avoid failing to find last string on the buffer.
		 */
		int pos = 0;

		do
		{
			if (kbuf[pos] == 0)
			{
				kbuf[pos] = ' ';
			}

			pos ++;
		}while(pos < count);

		kbuf [count] = 0;

		/*
		 * scan new records
		 */
		head = kbuf;

		if (last [0])
		{
			/*
			 * find the last line
			 */
			ptr = strstr (kbuf, last);

			if (ptr)
			{
				for (; *ptr && (*ptr != '\n') && (*ptr != '\r'); ptr ++);
				for (; *ptr && ((*ptr == '\n') || (*ptr == '\r')); ptr ++);

				if (! *ptr)
				{
					/* no new log */
					timed_wait (& timed_lock, 3000);
					continue;
				}

				count -= (int) ((unsigned long) ptr - (unsigned long) kbuf);
				head = ptr;

				/*
				 * buffer the last line for checking
				 */
				ptr += (count - 1);

				if (overload == 0)
				{
					overload = logkmsg_get_overload_size (name);

					DM ("get overload size[%d].\n", overload);
				}

				if ( (overload > 0) && (count > ( overload * 3 * 1024) ) )
					sprintf (state, "Found last line, append partial logs, ***LOG_BUFFER_OVERLOADED***, buffer[%d](B/s), criteria[%d](KB/s)", (count / 3), overload);
				else
					sprintf (state, "Found last line, append partial logs.");

				state[sizeof(state)-1] = 0;
			}
			else
			{
				//debugdump (count);

				file_log (LOG_TAG ": dmesg %d bytes, missing last=[%s]\n", count, last);

				if (count == 0)
				{
					/* it's possible that nothing read */
					timed_wait (& timed_lock, 3000);
					continue;
				}

				sprintf (state, "Missing last line, append all buffer logs.");
				state[sizeof(state)-1] = 0;

				ptr = head + count - 1;
			}
		}
		else
		{
			sprintf (state, "No last line record.");
			state[sizeof(state)-1] = 0;

			ptr = head + count - 1;
		}

	#if TIMEGEN
		/*
		 * write timestamp
		 */

		if (update [0])
		{
			char record [1024];
			memset (record, 0, sizeof (record));
			record [0] = 0;

			snprintf (record, sizeof(record), "%s, state[%s], last[%s]\n", update, state, last);
			record[sizeof(record)-1] = 0;
			memset (state, 0, sizeof (state));

			if (write_nointr (fd, record, strlen (record)) < 0)
			{
				file_log (LOG_TAG ": write_nointr time: %s\n", strerror (errno));
				write_error = 1;
				break;
			}
		}
	#endif

		/*
		 * buffer the last line for checking
		 */
		for (; (ptr != head) && ((*ptr == '\n') || (*ptr == '\r')); ptr --);
		for (; (ptr != head) && (*ptr != '\n') && (*ptr != '\r'); ptr --);

		if (ptr != head) ptr ++;

		memset (last, 0, sizeof (last));
		strncpy (last, ptr, sizeof (last) - 1);
		strtrim (last);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] write_nointr() +++\n");

		count = write_nointr (fd, head, count);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] write_nointr() count[%d]---\n", count);

		if (count < 0)
		{
			file_log (LOG_TAG ": write_nointr: %s\n", strerror (errno));
			write_error = 1;
			break;
		}

		if (do_fsync)
		{
			if (verbose > 1) file_log (LOG_TAG ": [embedded] fsync()\n");

			fsync (fd);
		}

		/*
		 * for watchdog
		 */
		if (verbose > 1) file_log (LOG_TAG ": [embedded] pthread_mutex_lock(data_lock) +++\n");

		pthread_mutex_lock (& data_lock);
		watchdog_counter ++;
		pthread_mutex_unlock (& data_lock);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] pthread_mutex_lock(data_lock) watchdog_counter[%d] ---\n", watchdog_counter);

		/*
		 * sleep
		 */
		timed_wait (& timed_lock, 3000);
	}

end:;
	if (fd >= 0)
	{
		close_nointr (fd);
		fd = -1;
	}

	if (verbose > 1) file_log (LOG_TAG ": [embedded] stop kernel message, pthread_mutex_lock(data_lock) +++\n");

	pthread_mutex_lock (& data_lock);
	done = 1;
	log_filename [0] = 0;
	pthread_mutex_unlock (& data_lock);

	if (verbose > 1) file_log (LOG_TAG ": [embedded] stop kernel message, pthread_mutex_lock(data_lock) ---\n");

	if (write_error)
	{
		write_error_flag = 1;
		do_emergency_dump (NULL, kbuf, sizeof (kbuf));
	}

	file_log (LOG_TAG ": stopped\n");

	DM ("stop logging kernel messages ...\n");
	return NULL;
}

void logkmsg_get_log_filepath (const char *UNUSED_VAR (name), char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s", log_filename);
}

void logkmsg_clear_log_files (const char *path)
{
	char buffer [32];
	logger_common_logdata_filename ("", FILE_PREFIX, buffer, sizeof (buffer));
	GLIST_NEW (patterns);
	glist_add (& patterns, FILE_PREFIX);
	glist_add (& patterns, buffer);
	glist_add (& patterns, "zfllog_"); /* also clear logs in zfllog_xxx sub-folders */
	dir_clear (path, patterns);
	glist_clear (& patterns, NULL);
	total_size = 0;
}

int logkmsg_check_file_type (const char *name, const char *filepath)
{
	return logger_common_check_file_type (name, filepath, FILE_PREFIX, log_filename);
}

unsigned long long logkmsg_get_total_size (const char *UNUSED_VAR (name))
{
	return total_size;
}

int logkmsg_update_state (const char *name, int state)
{
	if (write_error_flag)
	{
		state |= LOGGER_STATE_MASK_ERROR_WRITE;
		write_error_flag = 0;
	}
	return logger_common_update_state (name, state, -1, working, log_filename, 0);
}

int logkmsg_start (const char *name)
{
	set_debug_logkmsg_level ();

	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_start() +++\n");

	int ret = 0;

	write_error_flag = 0;

	if (working == (pthread_t) -1)
	{
		if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_start() call klogctl() +++\n");

		int count = klogctl (KLOG_READ_ALL, kbuf, sizeof (kbuf) - 1);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_start() call klogctl() count[%d] ---\n", count);

		if (count < 0)
		{
			DM ("klogctl failed: %s, use htc_dk service instead.\n", strerror (errno));

			use_htc_dk_service = 1;
		}

		/*
		 * for watchdog
		 */
		if (verbose > 1) file_log (LOG_TAG ": [embedded] start kernel message, pthread_mutex_lock(data_lock) +++\n");

		pthread_mutex_lock (& data_lock);
		watchdog_lastchecked = time (NULL);
		watchdog_counter = 0;
		pthread_mutex_unlock (& data_lock);

		if (verbose > 1) file_log (LOG_TAG ": [embedded] start kernel message, pthread_mutex_lock(data_lock) ---\n");

		if (logger_common_generate_new_file (name, FILE_PREFIX, log_filename, sizeof (log_filename)) != 0)
		{
			file_log (LOG_TAG ": failed to generate new file!\n");
			ret = -1;
		}
		else if (pthread_create (& working, NULL, thread_main, (void *) name) < 0)
		{
			if (verbose > 1) file_log (LOG_TAG ": [embedded] create thread_main failed.\n");

			DM ("pthread_create: %s\n", strerror (errno));
			ret = -1;
		}
		else
		{
		#if TIMEGEN

			if (verbose > 1) file_log (LOG_TAG ": [embedded] create thread_timegen +++\n");

			if (pthread_create (& timegen, NULL, thread_timegen, NULL) < 0)
			{
				if (verbose > 1) file_log (LOG_TAG ": [embedded] create thread_timegen failed.\n");

				DM ("pthread_create timegen: %s\n", strerror (errno));
			}

			if (verbose > 1) file_log (LOG_TAG ": [embedded] create thread_timegen ---\n");
		#endif
		}
	}

	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_start() ---\n");

	return ret;
}

int logkmsg_stop (const char *UNUSED_VAR (name))
{
	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_stop() +++\n");

	pthread_mutex_lock (& data_lock);
	done = 1;
	pthread_mutex_unlock (& data_lock);

	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_stop() call pthread_join(working) +++\n");

	if (working != (pthread_t) -1)
	{
		/* quit thread */
		sem_post (& timed_lock);
		pthread_join (working, NULL);
		working = (pthread_t) -1;
	}

	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_stop() call pthread_join(working) ---\n");

#if TIMEGEN
	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_stop() call pthread_join(timegen) +++\n");

	if (timegen != (pthread_t) -1)
	{
		/* quit thread */
		pthread_join (timegen, NULL);
		timegen = (pthread_t) -1;
		curtime [0] = 0;
		prevtime [0] = 0;
	}

	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_stop() call pthread_join(timegen) ---\n");
#endif

	pthread_mutex_lock (& data_lock);
	done = 0;
	pthread_mutex_unlock (& data_lock);

	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_stop() ---\n");

	return 0;
}

int logkmsg_rotate_and_limit (const char *name, int state, unsigned long rotate_size_mb, unsigned long limited_size_mb, unsigned long reserved_size_mb)
{
	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_rotate_and_limit() +++\n");

	char path_with_slash [PATH_MAX], *ptr;
	int fd = -1, ret = -1;

	SAFE_SPRINTF (path_with_slash, sizeof (path_with_slash), "%s", log_filename);

	if ((ptr = strrchr (path_with_slash, '/')) == NULL)
	{
		DM ("%s: invalid log file [%s]!\n", name, log_filename);
		goto end;
	}

	*(ptr + 1) = 0;

	if ((fd = logger_common_logdata_open (name, path_with_slash, FILE_PREFIX)) < 0)
	{
		DM ("failed to open log data!\n");
		goto end;
	}

	if (logger_common_logdata_limit_size (fd, name, path_with_slash, limited_size_mb, reserved_size_mb, & total_size) < 0)
	{
		DM ("failed to limit size!\n");
		goto end;
	}

	ret = 0;

	if (logger_common_need_rotate (name, log_filename, rotate_size_mb))
	{
		int compress = (state & LOGGER_STATE_MASK_COMPRESS) != 0;

		file_log (LOG_TAG ": rotate, compress=%d\n", compress);

		logkmsg_stop (name);

		if (compress)
		{
			logger_common_logdata_compress_file (fd, name, log_filename);
		}

		ret = logkmsg_start (name);
	}

end:;
	if (fd >= 0)
	{
		close (fd);
	}
	if (verbose > 1) file_log (LOG_TAG ": [embedded] logkmsg_rotate_and_limit() ---\n");
	return ret;
}
