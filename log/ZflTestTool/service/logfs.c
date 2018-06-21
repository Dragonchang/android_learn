#define	LOG_TAG		"STT:logfs"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/statfs.h>
#include "common.h"
#include "server.h"

#define	VERSION	"1.2"
/*
 * 1.2	: support specific external storage.
 * 1.1	: create log path if not exists.
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

typedef struct _fss {
	char	*path;	/* mount path */
	long	total;
	long	free;
	long	base;
	struct _fss *next;
} FSS;

static FSS *head = NULL;

static int done = 0;
static int interval = 10;
static char *log_filename = NULL;
static char data [PATH_MAX];
static char path [PATH_MAX] = LOG_DIR;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void free_fss (void)
{
	FSS *p;
	for (p = head; p; p = head)
	{
		head = p->next;
		if (p->path)
			free (p->path);
		free (p);
	}
}

static void add_fs (const char *path)
{
	struct statfs st;
	FSS *node;

	/* check first */
	if ((statfs (path, & st) < 0) || (st.f_blocks == 0))
		return;

	/* add the node */
	if ((node = (FSS *) malloc (sizeof (FSS))) != NULL)
	{
		memset (node, 0, sizeof (FSS));
		node->path = strdup (path);
		if (! node->path)
		{
			free (node);
			return;
		}
		node->next = head;
		head = node;
	}
}

static void make_fss (void)
{
	char line [PATH_MAX << 1];
	char *p1, *p2;
	FILE *fp;

	fp = fopen ("/proc/mounts", "rb");

	if (! fp)
	{
		const char *ext = dir_get_external_storage ();

		/* make default fss */
		add_fs ("/system");
		add_fs ("/data");

		if (ext)
		{
			add_fs (ext);
		}

		add_fs (TMP_DIR);
		return;
	}

	while (fgets (line, sizeof (line), fp) != NULL)
	{
		for (p1 = line; *p1 && (*p1 != ' '); p1 ++);

		if (*p1 ++ == 0)
			continue;

		for (p2 = p1; *p2 && (*p2 != ' '); p2 ++);

		*p2 = 0;

		add_fs (p1);
	}

	fclose (fp);
}

static void update_fss (void)
{
	struct statfs st;
	FSS *p;

	for (p = head; p; p = p->next)
	{
		if (statfs (p->path, & st) < 0)
		{
			DM ("%s: %s", p->path, strerror (errno));
			continue;
		}

		if (st.f_blocks == 0)
		{
			p->total = 0;
			p->free = 0;
			p->base = 0;
		}
		else
		{
			p->total = (long) (((long long) st.f_blocks * (long long) st.f_bsize) / 1024);
			p->free = (long) (((long long) st.f_bfree * (long long) st.f_bsize) / 1024);

			if (p->base == 0)
				p->base = p->free;
		}
	}
}

static int log_main (void)
{
	static char buf [PATH_MAX];
	FILE *fplog = NULL;
	struct tm *ptm;
	time_t t;
	FSS *p;

	t = time (NULL);

	ptm = localtime (& t);

	if (! log_filename)
	{
		dir_create_recursive (path);

		pthread_mutex_lock (& data_lock);
		if (! ptm)
		{
			sprintf (buf, "%sfslog_NA_%d.txt", path, getpid ());
		}
		else
		{
			sprintf (buf, "%sfslog_%04d%02d%02d_%02d%02d%02d.txt", path,
				ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
		}
		pthread_mutex_unlock (& data_lock);

		pthread_mutex_lock (& data_lock);
		log_filename = strdup (buf);
		pthread_mutex_unlock (& data_lock);

		fplog = fopen (buf, "wb");

		if (! fplog)
		{
			DM ("%s: %s\n", buf, strerror (errno));
			return -1;
		}
	}
	else
	{
		fplog = fopen (log_filename, "a+b");

		if (! fplog)
		{
			DM ("%s: %s\n", buf, strerror (errno));
			return -1;
		}

		fseek (fplog, 0, SEEK_END);
	}

	if (! ptm)
	{
		strcpy (buf, "N/A");
	}
	else
	{
		sprintf (buf, "%02d%02d-%02d:%02d:%02d", ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
	}

	update_fss ();

	for (p = head; p; p = p->next)
	{
		sprintf (& buf [strlen (buf)], ",%s total:%ld free:%ld diff:%+ld", p->path, p->total, p->free, p->free - p->base);
	}
	strcat (buf, "\n");

	fprintf (fplog, "%s", buf);

	pthread_mutex_lock (& data_lock);
	for (data [0] = 0, p = head; p; p = p->next)
	{
		sprintf (buf,	"%1$s:\n"
				"%6$4sTotal : %2$8ld kB\n"
				"%6$4s Used : %3$8ld kB\n"
				"%6$4s Free : %4$8ld kB\n"
				"%6$4s Diff : %5$+8ld kB\n\n",
				p->path,
				p->total,
				p->total - p->free,
				p->free,
				p->free - p->base,
				"");

		if ((strlen (data) + strlen (buf)) >= sizeof (data))
			break;

		strcat (data, buf);
	}
	pthread_mutex_unlock (& data_lock);

	fclose (fplog);
	return 0;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	struct timespec ts;

	free_fss ();
	make_fss ();

	do
	{
		/* do log */
		if (log_main () < 0)
			break;

		/* set next timeout */
		pthread_mutex_lock (& data_lock);
		clock_gettime (CLOCK_REALTIME, & ts);
		ts.tv_sec += interval;
		pthread_mutex_unlock (& data_lock);

		pthread_cond_timedwait (& cond, & time_lock, & ts);
	}
	while (! done);

	pthread_mutex_lock (& data_lock);
	if (log_filename)
	{
		free (log_filename);
		log_filename = NULL;
	}
	data [0] = 0;
	pthread_mutex_unlock (& data_lock);

	/* free fss */
	free_fss ();

	return NULL;
}

int logfs_main (int server_socket)
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
				ret = (working == (pthread_t) -1) ? 0 : 1;
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
				if (! log_filename)
				{
					strcpy (buffer, path);
				}
				else
				{
					strcpy (buffer, log_filename);
				}
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
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				/* start log */
				if (working == (pthread_t) -1)
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
						ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_STOP))
			{
				if (working != (pthread_t) -1)
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

	if (working != (pthread_t) -1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	return 0;
}
