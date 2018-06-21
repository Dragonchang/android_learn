#define	LOG_TAG		"STT:fsspeed"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/statfs.h>

#include "common.h"
#include "server.h"

#include "headers/process.h"

#define	VERSION	"1.0"
/*
 * 1.0	: test fs writing speed.
 */

/* custom commands */
#define	LOG_GETINTERVAL	":getinterval:"
#define	LOG_SETINTERVAL	":setinterval:"
#define	LOG_GETPATH	":getpath:"
#define	LOG_SETPATH	":setpath:"
#define	LOG_RUN		":run:"
#define	LOG_STOP	":stop:"
#define	LOG_ISLOGGING	":islogging:"
#define	LOG_GETDATA	":getdata:"

#ifndef	TMP_DIR
#define	TMP_DIR	"/tmp"
#endif

static int done = 0;
static int interval = 10;
static char data [PATH_MAX];
static char path [PATH_MAX] = LOG_DIR;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void *thread_main (void *UNUSED_VAR (null))
{
	struct timespec ts;
	struct timespec begin, end;
	char file [PATH_MAX];
	char buffer [1024];
	double speed;
	int fd, i;

	prctl (PR_SET_NAME, (unsigned long) "fsspeed:test", 0, 0, 0);

	snprintf (file, sizeof (file), "%s/.fsspeed", path);
	file [sizeof (file) - 1] = 0;

	memset (buffer, '1', sizeof (buffer));

	fd = open (file, O_WRONLY | O_CREAT | O_TRUNC, LOG_FILE_MODE);

	if (fd < 0)
	{
		pthread_mutex_lock (& data_lock);
		snprintf (data, sizeof (data), "Open file on [%s] error:\n%s", path, strerror (errno));
		data [sizeof (data) - 1] = 0;
		pthread_mutex_unlock (& data_lock);
	}

	do
	{
		/* test */
		if (fd >= 0)
		{
			DM ("begin\n");

			if (clock_gettime (CLOCK_REALTIME, & begin) < 0)
			{
				pthread_mutex_lock (& data_lock);
				snprintf (data, sizeof (data), "Failed to get begin time:\n%s", strerror (errno));
				data [sizeof (data) - 1] = 0;
				pthread_mutex_unlock (& data_lock);
				goto close_fd;
			}

			for (i = 0; i < 1024; i ++)
			{
				if (write (fd, buffer, sizeof (buffer)) < 0)
				{
					pthread_mutex_lock (& data_lock);
					snprintf (data, sizeof (data), "Write file on [%s] error:\n%s", path, strerror (errno));
					data [sizeof (data) - 1] = 0;
					pthread_mutex_unlock (& data_lock);
					goto close_fd;
				}
			}

			DM ("end\n");

			if (clock_gettime (CLOCK_REALTIME, & end) < 0)
			{
				pthread_mutex_lock (& data_lock);
				snprintf (data, sizeof (data), "Failed to get begin time:\n%s", strerror (errno));
				data [sizeof (data) - 1] = 0;
				pthread_mutex_unlock (& data_lock);
				goto close_fd;
			}

			speed = ((double) 1024 * 1024) / ((double) (end.tv_sec - begin.tv_sec) + ((double) (end.tv_nsec - begin.tv_nsec) / (double) 1000000000L));

			DM ("speed = %lf\n", speed);
		}

		if (0)
		{
		close_fd:;
			if (fd >= 0) close (fd);
			fd = -1;
		}

		/* set next timeout */
		pthread_mutex_lock (& data_lock);
		clock_gettime (CLOCK_REALTIME, & ts);
		ts.tv_sec += interval;
		pthread_mutex_unlock (& data_lock);

		pthread_cond_timedwait (& cond, & time_lock, & ts);
	}
	while (! done);

	if (fd >= 0) close (fd);

	unlink (file);

	pthread_mutex_lock (& data_lock);
	data [0] = 0;
	pthread_mutex_unlock (& data_lock);

	return NULL;
}

int fsspeed_main (int server_socket)
{
	pthread_t working = (pthread_t) -1;

	char buffer [PATH_MAX + 16];
	int commfd = -1;
	int ret = 0;

	pthread_mutex_lock (& time_lock);

	data [0] = 0;

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

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				pthread_mutex_lock (& data_lock);
				done = 1;
				pthread_mutex_unlock (& data_lock);
				break;
			}
			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_GETDATA))
			{
				pthread_mutex_lock (& data_lock);
				strcpy (buffer, data);
				pthread_mutex_unlock (& data_lock);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				ret = is_thread_alive (working);
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, LOG_GETINTERVAL))
			{
				ret = interval;
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, LOG_GETPATH))
			{
				pthread_mutex_lock (& data_lock);
				strcpy (buffer, path);
				pthread_mutex_unlock (& data_lock);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_SETINTERVAL))
			{
				/* change log interval */
				MAKE_DATA (buffer, LOG_SETINTERVAL);

				ret = atoi (buffer);

				if (ret > 0)
				{
					pthread_mutex_lock (& data_lock);
					interval = ret;
					pthread_mutex_unlock (& data_lock);
					ret = 0;
				}
				else
				{
					DM ("bad interval value (%d)!\n", ret);
					ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_SETPATH))
			{
				if (! is_thread_alive (working))
				{
					MAKE_DATA (buffer, LOG_SETPATH);

					if (buffer [strlen (buffer) - 1] != '/')
						strcat (buffer, "/");

					if (access (buffer, R_OK | W_OK) < 0)
					{
						DM ("%s: %s\n", buffer, strerror (errno));
						ret = -1;
					}
					else
					{
						pthread_mutex_lock (& data_lock);
						strcpy (path, buffer);
						pthread_mutex_unlock (& data_lock);
					}
				}
				else
				{
					DM ("cannot change path while running!\n");
					ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				/* start log */
				if (! is_thread_alive (working))
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
						ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_STOP))
			{
				if (is_thread_alive (working))
				{
					/* quit thread */
					//pthread_cancel (working);
					pthread_mutex_lock (& data_lock);
					done = 1;
					pthread_mutex_unlock (& data_lock);
					pthread_cond_signal (& cond);
					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
				}
			}
			else
			{
				DM ("unknown command [%s]!\n", buffer);
				ret = -1;
			}

			/* command response */
			if (ret != 1)
				sprintf (buffer, "%d", ret);

			DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close (commfd);
		commfd = -1;
	}

	pthread_mutex_unlock (& time_lock);

	if (is_thread_alive (working))
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	return 0;
}
