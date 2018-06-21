#define	LOG_TAG		"STT:logprocrank"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#include "common.h"
#include "server.h"

#include "headers/poll.h"
#include "headers/process.h"

#define	VERSION	"1.1"
/*
 * 1.1	: Do not set thread id to -1 inside thread to prevent timing issue.
 * 1.0	: save process rank logs.
 */

#define LOG_BUF_LEN	2048
#define	LOG_DEFAULT_RECORD_INTERVAL	"30"

/* custom commands */
#define	LOG_GETPATH		":getpath:"
#define	LOG_SETPATH		":setpath:"
#define	LOG_GETPARAM	":getparam:"
#define	LOG_SETPARAM	":setparam:"
#define	LOG_RUN			":run:"
#define	LOG_STOP		":stop:"
#define	LOG_ISLOGGING	":islogging:"

static char path [PATH_MAX] = LOG_DIR;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t working = (pthread_t) -1;
//static pthread_t timegen = (pthread_t) -1;

static int done = 0;
static char log_filename [PATH_MAX] = "";

static char log_size [12] = LOG_DEFAULT_ROTATE_SIZE;
static char log_rotate [12] = LOG_DEFAULT_ROTATE_COUNT;
static char log_interval [12] = LOG_DEFAULT_RECORD_INTERVAL;

static int rotate_file (int rotate)
{
	char *pf1, *pf2;
	int i, fd;

	if (log_filename [0] == 0)
	{
		DM ("no file name set!\n");
		return -1;
	}

	pf1 = (char *) malloc (strlen (log_filename) + 16);
	pf2 = (char *) malloc (strlen (log_filename) + 16);

	if ((! pf1) || (! pf2))
	{
		DM ("malloc failed!\n");
		if (pf1) free (pf1);
		if (pf2) free (pf2);
		return -1;
	}

	for (i = rotate; i > 0; i --)
	{
		sprintf (pf1, "%s.%d", log_filename, i);

		if (i == 1)
		{
			sprintf (pf2, "%s", log_filename);
		}
		else
		{
			sprintf (pf2, "%s.%d", log_filename, i - 1);
		}

		if ((rename (pf2, pf1) < 0) && (errno != ENOENT))
		{
			DM ("cannot rename [%s] to [%s]: %s\n", pf2, pf1, strerror (errno));
			break;
		}
	}

	free (pf1);
	free (pf2);

	fd = open (log_filename, O_CREAT | O_RDWR | O_TRUNC, LOG_FILE_MODE);

	if (fd < 0)
	{
		DM ("failed to open log file [%s]: %s\n", log_filename, strerror (errno));
	}

	//DM ("[DEBUG]Rotate file[%s].\n", log_filename);

	return fd;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	LOG_SESSION ls;

	char buf [LOG_BUF_LEN];
	long f_size = 0;
	int f_rotate, f_interval;
	int fd = -1;

	struct stat s;
	char system_command [PATH_MAX] = "";

	prctl (PR_SET_NAME, (unsigned long) "logprocrank:logger", 0, 0, 0);

	f_size = atol (log_size) * 1000000;
	f_rotate = atoi (log_rotate);
	f_interval = atoi (log_interval);

	//DM ("[DEBUG]parameters! size = %ld, rotate = %d, f_interval = %d\n", f_size, f_rotate, f_interval);

	if ((f_size <= 0) || (f_rotate <= 0) || (f_interval <= 0))
	{
		DM ("invalid parameters! size = %ld, rotate = %d, interval = %d\n", f_size, f_rotate, f_interval);
		return NULL;
	}

	/*
	 * create timestamp tag
	 */
	bzero (& ls, sizeof (ls));

	strncpy (ls.timestamp, TAG_DATETIME, sizeof (ls.timestamp));

	str_replace_tags (ls.timestamp);

	/*
	 * make log file name before system() call.
	 */
	snprintf (log_filename, sizeof (log_filename), "%sprocrank_%s.txt", path, ls.timestamp);
	log_filename [sizeof (log_filename) - 1] = 0;

	//DM ("[DEBUG]parameters! log_filename[%s].\n", log_filename);

	for (; ! done;)
	{
		//DM ("[DEBUG]Check log_filename1[%s], fd[%d] ...\n", log_filename, fd);

		if ((fd < 0) || (((lstat (log_filename, & s) == 0) && (s.st_size >= f_size))))
		{
			//DM ("[DEBUG]Check file size[%lld] file[%s] ...\n", s.st_size, log_filename);

			if (fd >= 0) close (fd);

			fd = rotate_file (f_rotate);

			//DM ("[DEBUG]Check log_filename2[%s], fd[%d] ...\n", log_filename, fd);

			if (fd < 0)
			{
				log_filename [0] = 0;
				break;
			}
		}

		DM ("logging procrank messages to file[%s], size[%lld]...\n", log_filename, (long long int) s.st_size);

		snprintf (system_command, sizeof (system_command), "date >> %s", log_filename);
		system(system_command);

		snprintf (system_command, sizeof (system_command), "procrank >> %s", log_filename);
		system(system_command);

		//DM ("[DEBUG]system_command[%s] ...\n", system_command);

		sleep(f_interval);
	}

end:;
	if (fd >= 0)
	{
		close (fd);
		fd = -1;
	}

	pthread_mutex_lock (& data_lock);
	done = 1;
	log_filename [0] = 0;
	pthread_mutex_unlock (& data_lock);

	DM ("end of procrank log thread.\n");

	return NULL;
}

int logprocrank_islogging (void)
{
	return is_thread_alive (working);
}

void logprocrank_getpath (char *buffer, int len)
{
	strncpy (buffer, path, len - 1);
}

void logprocrank_getlogfile (char *buffer, int len)
{
	strncpy (buffer, log_filename, len - 1);
}

int logprocrank_main (int server_socket)
{
	char buffer [PATH_MAX + 16];
	char *ptr;

	int ret = 0;
	int commfd = -1;

	if ((ptr = (char *) db_get ("systemloggers.path", NULL)) != NULL)
	{
		strncpy (path, ptr, sizeof (path) - 1);
		path [sizeof (path) - 1] = 0;

		if (dir_select_log_path (path, sizeof (path)) < 0)
			return -1;
	}
	if ((ptr = (char *) db_get ("systemloggers.rotatesize", NULL)) != NULL)
	{
		strncpy (log_size, ptr, sizeof (log_size) - 1);
		log_size [sizeof (log_size) - 1] = 0;
	}
	if ((ptr = (char *) db_get ("systemloggers.rotatecount", NULL)) != NULL)
	{
		strncpy (log_rotate, ptr, sizeof (log_rotate) - 1);
		log_rotate [sizeof (log_rotate) - 1] = 0;
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

			if (! is_thread_alive (working))
			{
				working = (pthread_t) -1;
			}

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
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				sprintf (buffer, "%d", (working == (pthread_t) -1) ? 0 : 1);
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
					strcpy (buffer, path);
				}
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_GETPARAM))
			{
				sprintf (buffer, "%s:%s:%s", log_size, log_rotate, log_interval);
			}
			else if (CMP_CMD (buffer, LOG_SETPATH))
			{
				/* change log path */
				if (working == (pthread_t) -1)
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
					DM ("cannot change path while logging!\n");
					ret = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_SETPARAM))
			{
				/* change parameters */
				if (working == (pthread_t) -1)
				{
					MAKE_DATA (buffer, LOG_SETPARAM);
					datatok (buffer, log_size);
					datatok (buffer, log_rotate);
					datatok (buffer, log_interval);
				}
				else
				{
					DM ("cannot change parameters while logging!\n");
					ret = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				DM ("Run logprocrank!\n");

				/* start log */
				if (working == (pthread_t) -1)
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
					{
						DM ("pthread_create: %s\n", strerror (errno));
						ret = -1;
					}
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_STOP))
			{
				if (working != (pthread_t) -1)
				{
					/* quit thread */
					pthread_mutex_lock (& data_lock);
					done = 1;
					pthread_mutex_unlock (& data_lock);
					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
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

		close (commfd);
		commfd = -1;

		ret = 0;
	}

	if (working != (pthread_t) -1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	return ret;
}
