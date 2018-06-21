#define	LOG_TAG		"STT:logrpm"

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

#define	VERSION	"1.3"
/*
 * 1.3	: Do not set thread id to -1 inside thread to prevent timing issue.
 * 1.2	: Replace "/data/d/" to "/sys/kernel/debug/" since mounting debugfs under "/data" would cause userdata partition encryption failed.
 * 1.1	: add storage code to identify the log path.
 * 1.0	: save rpm logs.
 */

#define LOG_BUF_LEN	2048

/* custom commands */
#define	LOG_GETPATH	":getpath:"
#define	LOG_SETPATH	":setpath:"
#define	LOG_GETPARAM	":getparam:"
#define	LOG_SETPARAM	":setparam:"
#define	LOG_RUN		":run:"
#define	LOG_STOP	":stop:"
#define	LOG_ISLOGGING	":islogging:"

static const char *rpm_node = "/sys/kernel/debug/rpm_log";

static char path [PATH_MAX] = LOG_DIR;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t working = (pthread_t) -1;
static pthread_t timegen = (pthread_t) -1;

static int done = 0;
static char log_filename [PATH_MAX] = "";

static POLL poll_read = POLL_INITIAL;

static char log_size [12] = LOG_DEFAULT_ROTATE_SIZE;
static char log_rotate [12] = LOG_DEFAULT_ROTATE_COUNT;

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

	return fd;
}

static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

static char curtime [24] = "";
static char prevtime [24] = "";

static void *thread_timegen (void *UNUSED_VAR (null))
{
	struct tm *ptm;
	time_t t;

	prctl (PR_SET_NAME, (unsigned long) "logrpm:timegen", 0, 0, 0);

	for (; ! done;)
	{
		t = time (NULL);

		ptm = localtime (& t);

		pthread_mutex_lock (& time_lock);

		snprintf (curtime, sizeof (curtime), "%04d/%02d/%02d %02d:%02d:%02d",
				ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

		pthread_mutex_unlock (& time_lock);

		sleep (1);
	}

	DM ("end of timegen thread.\n");
	return NULL;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	LOG_SESSION ls;

	char buf [LOG_BUF_LEN];
	int count;
	long f_size, f_size_count = 0;
	int f_rotate;
	int kfd = -1, fd = -1;

	prctl (PR_SET_NAME, (unsigned long) "logrpm:logger", 0, 0, 0);

	f_size = atol (log_size) * 1000000;
	f_rotate = atoi (log_rotate);

	if ((f_size <= 0) || (f_rotate <= 0))
	{
		DM ("invalid parameters! size = %ld, rotate = %d\n", f_size, f_rotate);
		return NULL;
	}

	/*
	 * create timestamp tag
	 */
	bzero (& ls, sizeof (ls));

	strncpy (ls.timestamp, TAG_DATETIME, sizeof (ls.timestamp));

	str_replace_tags (ls.timestamp);

	/*
	 * save timestamp
	 */
	snprintf (log_filename, sizeof (log_filename), "%s.rpm.%c." LOG_DATA_EXT, path, dir_get_storage_code (path));
	log_filename [sizeof (log_filename) - 1] = 0;

	file_mutex_write (log_filename, (const char *) & ls, sizeof (ls), O_CREAT | O_RDWR | O_APPEND);

	/*
	 * make log file name before system() call.
	 */
	snprintf (log_filename, sizeof (log_filename), "%srpm_%s.txt", path, ls.timestamp);
	log_filename [sizeof (log_filename) - 1] = 0;

	/* init polling */
	if (poll_open (& poll_read) < 0)
	{
		DM ("poll_open: %s\n", strerror (errno));
		goto end;
	}

	kfd = open (rpm_node, O_RDONLY);

	if (kfd < 0)
	{
		DM ("%s: %s!\n", rpm_node, strerror (errno));
		goto end;
	}

	DM ("logging rpm messages ...\n");

	for (; ! done;)
	{
		if ((fd < 0) || (f_size_count > f_size))
		{
			if (fd >= 0) close (fd);

			fd = rotate_file (f_rotate);

			if (fd < 0)
			{
				log_filename [0] = 0;
				break;
			}

			f_size_count = 0;
		}

		if (poll_wait (& poll_read, kfd, -1) <= 0)
			break;

		/*
		 * write timestamp
		 */
		buf [0] = 0;

		pthread_mutex_lock (& time_lock);

		if (strcmp (curtime, prevtime) != 0)
		{
			sprintf (buf, "Update: [%s]\n", curtime);
			strcpy (prevtime, curtime);
		}

		pthread_mutex_unlock (& time_lock);

		/*
		 * if curtime is "", this will also be ""
		 */
		if (prevtime [0] == 0)
			continue;

		/*
		 * curtime and prevtime are different
		 */
		if (buf [0])
		{
			count = write (fd, buf, strlen (buf));

			if (count < 0)
			{
				DM ("write time: %s\n", strerror (errno));
				break;
			}
		}

		/*
		 * read rpm log
		 */
		count = read (kfd, buf, sizeof (buf) - 1);

		if (count <= 0)
		{
			if (errno != 0) DM ("read: %s\n", strerror (errno));
			break;
		}

		buf [count] = 0;

		f_size_count += count;

		count = write (fd, buf, count);

		if (count < 0)
		{
			DM ("write: %s\n", strerror (errno));
			break;
		}

		fsync (fd);
	}

end:;
	if (fd >= 0)
	{
		close (fd);
		fd = -1;
	}
	if (kfd >= 0)
	{
		close (kfd);
		kfd = -1;
	}
	if (poll_is_opened (& poll_read))
	{
		poll_close (& poll_read);
	}

	pthread_mutex_lock (& data_lock);
	done = 1;
	log_filename [0] = 0;
	pthread_mutex_unlock (& data_lock);

	DM ("end of rpm log thread.\n");
	return NULL;
}

int logrpm_islogging (void)
{
	return is_thread_alive (working);
}

void logrpm_getpath (char *buffer, int len)
{
	strncpy (buffer, path, len - 1);
}

void logrpm_getlogfile (char *buffer, int len)
{
	strncpy (buffer, log_filename, len - 1);
}

int logrpm_main (int server_socket)
{
	char buffer [PATH_MAX + 16];
	char *ptr;

	int ret = 0;
	int commfd = -1;

	if (access (rpm_node, R_OK) != 0)
	{
		DM ("cannot find %s!\n", rpm_node);
		return -1;
	}

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

	if (db_get ("logrpm", NULL) != NULL)
	{
		db_remove ("logrpm");

		if (pthread_create (& working, NULL, thread_main, NULL) < 0)
		{
			DM ("pthread_create: %s\n", strerror (errno));
		}
		else
		{
			if (pthread_create (& timegen, NULL, thread_timegen, NULL) < 0)
			{
				DM ("pthread_create: %s\n", strerror (errno));
			}
		}
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

				if (! is_thread_alive (timegen))
				{
					timegen = (pthread_t) -1;
				}
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
				sprintf (buffer, "%s:%s", log_size, log_rotate);
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
				/* start log */
				if (working == (pthread_t) -1)
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
					{
						DM ("pthread_create: %s\n", strerror (errno));
						ret = -1;
					}
					else
					{
						if (pthread_create (& timegen, NULL, thread_timegen, NULL) < 0)
						{
							DM ("pthread_create: %s\n", strerror (errno));
						}
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
					poll_break (& poll_read);
					//pthread_kill (working, SIGTERM);
					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
				}
				if (timegen != (pthread_t) -1)
				{
					/* quit thread */
					pthread_mutex_lock (& data_lock);
					done = 1;
					pthread_mutex_unlock (& data_lock);
					pthread_join (timegen, NULL);
					timegen = (pthread_t) -1;
					curtime [0] = 0;
					prevtime [0] = 0;
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

	if (timegen != (pthread_t) -1)
		pthread_join (timegen, NULL);

	curtime [0] = 0;
	prevtime [0] = 0;

	/* reset done flag */
	done = 0;

	return ret;
}
