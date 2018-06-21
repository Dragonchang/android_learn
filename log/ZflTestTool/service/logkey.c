#define	LOG_TAG		"STT:logkey"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <linux/input.h>

#include "common.h"
#include "server.h"

#include "headers/poll.h"
#include "headers/glist.h"
#include "headers/input.h"

#define	VERSION	"1.5"
/*
 * 1.5	: recover counting data when device unexpected reboot
 * 1.4	: support UI: PhysicalKeyLogger.java
 * 1.3	: sync the key devices with dumpstate service
 * 1.2	: log ms
 * 1.1	: use poll family helper functions
 */

/* custom commands */
#define	LOG_GETINTERVAL  ":getinterval:"
#define	LOG_SETINTERVAL  ":setinterval:"
#define	LOG_GETPARAM     ":getparam:"
#define	LOG_SETPARAM     ":setparam:"
#define	LOG_GETPATH      ":getpath:"
#define	LOG_SETPATH      ":setpath:"
#define	LOG_RUN          ":run:"
#define	LOG_STOP         ":stop:"
#define	LOG_ISLOGGING    ":islogging:"
#define	LOG_GETDATA      ":getdata:"
#define	LOG_RECOVERYDATA ":recoverydata:"
#define	LOG_RESETDATA    ":resetdata:"
#define	LOG_RESETCONF    ":resetconf:"

#define keyTableSize 32

#define	SETTING_FILENAME  LOG_DIR "PhysicalKeyLogger.conf"
#define	DEFAULT_VALUE "0"

static int commfd = -1;
static POLL poller = POLL_INITIAL;
static int done = 0;
static int interval = 1;
static int hasUI = 0;
static char *log_filename = NULL;
static int keyTable[3][keyTableSize]; // [0]:KeyCode  [1]:KeyDown  [2]:KeyUp
static char path [PATH_MAX] = LOG_DIR;
static CONF *conf = NULL;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

static int loadConfig (void)
{
	static char buf [32];
    static int num1 = 0, num2 = 0, i = 0;

	if (conf != NULL)
	{
		conf_destroy (conf);
		conf = NULL;
	}

	conf = conf_load_from_file (SETTING_FILENAME);
	if (conf == NULL)
	{
		conf = conf_new (SETTING_FILENAME);
	}

	if (conf != NULL)
	{
		//conf_dump (conf);

		GLIST *node;
		CONF_PAIR *p;

		DM ("conf [%s]\n", conf->path);

		for (node = conf->pair_list; node; node = GLIST_NEXT (node))
		{
			if ((p = (CONF_PAIR *) GLIST_DATA (node)) != NULL)
			{
				DM ("  [%s]=[%s]\n", p->name, p->value);
				for ( i = 0; i < keyTableSize; ++i )
				{
					if (keyTable[0][i] == -1)
					{
						sscanf(p->name, "%d", &num1);
						keyTable[0][i] = num1;
						sscanf(p->value, "%d,%d", &num1, &num2);
						keyTable[1][i] = num1;
						keyTable[2][i] = num2;
						break;
					}
				}
			}
			else { DM ("  !! null member !!\n"); }
		}
	}
	return 1;
}

static int saveConfig (void)
{
	static char buf1 [16];
	static char buf2 [32];
    static int num1 = 0, num2 = 0, i = 0;

	if (conf == NULL)
	{
		conf = conf_new (SETTING_FILENAME);

		if (conf == 0)
		{
			DM ("cannot create new config!");
			return 0;
		}

		conf_remove_all (conf);
	}

	for ( i = 0; i < keyTableSize; ++i )
	{
		if ( keyTable[0][i] != -1)
		{
			sprintf(buf1, "%d", keyTable[0][i]);
			sprintf(buf2, "%d,%d", keyTable[1][i], keyTable[2][i]);
			conf_set (conf, buf1, buf2);
		}
		else { break; }
	}

	//conf_sort (conf);
	conf_save_to_file (conf);

	return 1;
}

static void statistics (struct input_event *pie)
{
	static int i = 0;

	for ( i = 0; i < keyTableSize; ++i )
	{
		if (keyTable[0][i] == pie->code)
		{
			if (pie->value==1) { ++keyTable[1][i]; }
			else if (pie->value==0) { ++keyTable[2][i]; }
			break;
		}
		if (keyTable[0][i] == -1)
		{
			keyTable[0][i] = pie->code;
			if (pie->value==1)
			{
				keyTable[1][i] = 1;
				keyTable[2][i] = 0;
			}
			else if (pie->value==0)
			{
				keyTable[1][i] = 0;
				keyTable[2][i] = 1;
			}
			break;
		}
	}
	saveConfig();
}

static int log_main (struct input_event *pie)
{
	static char buf [PATH_MAX];
	FILE *fplog = NULL;
	struct timespec ts;
	struct tm *ptm;
	time_t t;
	long ns;

	clock_gettime (CLOCK_REALTIME, & ts);

	t = ts.tv_sec; //time (NULL);
	ns = ts.tv_nsec;

	ptm = localtime (& t);

	if (! log_filename)
	{
		pthread_mutex_lock (& data_lock);
		if (! ptm)
		{
			sprintf (buf, "%skeylog_NA_%d.csv", path, getpid ());
		}
		else
		{
			sprintf (buf, "%skeylog_%04d%02d%02d_%02d%02d%02d.csv", path,
				ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
		}
		log_filename = strdup (buf);
		pthread_mutex_unlock (& data_lock);

		fplog = fopen (buf, "wb");

		if (! fplog)
		{
			DM ("%s: %s\n", buf, strerror (errno));
			return -1;
		}

		/* csv title */
		fprintf (fplog,
			"Time,"
			"Type,"
			"Code,"
			"Value\n");
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
		sprintf (buf, "%02d%02d-%02d:%02d:%02d.%03ld", ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, ns / 1000000);
	}

	if (pie->code!=0) { statistics(pie); }

	/* csv data */
	fprintf (fplog, "%s,%d,%d,%d\n", buf, pie->type, pie->code, pie->value);

	sprintf (buf, ": %d, %d, %d\n", pie->type, pie->code, pie->value);

	DM ("Key (type, code, value)%s", buf);

	pthread_mutex_lock (& data_lock);
	if ((hasUI == 0) && (commfd > 0))
	{
		write (commfd, buf, strlen (buf));
	}
	pthread_mutex_unlock (& data_lock);

	fclose (fplog);
	return 0;
}

static int init_log_file (void)
{
	static char buf [PATH_MAX];
	FILE *fplog = NULL;
	struct timespec ts;
	struct tm *ptm;
	time_t t;
	long ns;

	clock_gettime (CLOCK_REALTIME, & ts);

	t = ts.tv_sec; //time (NULL);
	ns = ts.tv_nsec;

	ptm = localtime (& t);

	if (! log_filename)
	{
		pthread_mutex_lock (& data_lock);
		if (! ptm)
		{
			sprintf (buf, "%skeylog_NA_%d.csv", path, getpid ());
		}
		else
		{
			sprintf (buf, "%skeylog_%04d%02d%02d_%02d%02d%02d.csv", path,
				ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
		}
		log_filename = strdup (buf);
		pthread_mutex_unlock (& data_lock);

		fplog = fopen (buf, "wb");

		if (! fplog)
		{
			DM ("%s: %s\n", buf, strerror (errno));
			return -1;
		}

		/* csv title */
		fprintf (fplog,
			"Time,"
			"Type,"
			"Code,"
			"Value\n");
	}

	fclose (fplog);
	return 0;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	const char *white_list [] = {
		"keypad",
		"Keypad",
		"keys",
		NULL
	};

	const char *black_list [] = {
		"dummy",
		"projector",
		NULL
	};

	struct input_event ie;
	int *fds;
	int nr;

	prctl (PR_SET_NAME, (unsigned long) "logkey:key", 0, 0, 0);

	fds = open_input_devices (white_list, black_list);

	if (! fds)
		return NULL;

	DM ("got %d fds\n", fds [0]);

	init_log_file();

	while (! done)
	{
		nr = poll_multiple_wait (& poller, -1, & fds [1], fds [0]);

		if (nr <= 0)
		{
			DM ("break on poll returned %d\n", nr);
			break;
		}

		if (read (fds [nr], & ie, sizeof (ie)) != sizeof (ie))
		{
			DM ("invalid event size!\n");
			continue;
		}

		/* do log */
		if (log_main (& ie) < 0)
			break;
	}

	close_input_devices (fds);
	fds = NULL;

	pthread_mutex_lock (& data_lock);

	done = 1;

	poll_close (& poller);

	if (log_filename)
	{
		free (log_filename);
		log_filename = NULL;
	}

	pthread_mutex_unlock (& data_lock);
	return NULL;
}

int logkey_main (int server_socket)
{
	pthread_t working = (pthread_t) -1;

	char buffer [1024];
	char tmp [PATH_MAX + 16];
	int ret = 0, i = 0;

	while (! done)
	{
		DM ("waiting connection ...\n");

		ret = wait_for_connection (server_socket);

		if (ret < 0)
		{
			DM ("accept client connection failed!\n");
			continue;
		}

		pthread_mutex_lock (& data_lock);
		commfd = ret;
		pthread_mutex_unlock (& data_lock);

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
			}
			else if (CMP_CMD (buffer, LOG_GETDATA))
			{
				pthread_mutex_lock (& data_lock);
				sprintf (buffer, " KeyCode      KeyDown    KeyUp\n");
				for ( i = 0; i < keyTableSize; ++i )
				{
					if (keyTable[0][i] == -1) { break; }
					switch (keyTable[0][i])
					{
						case 114:
							sprintf (buffer + strlen(buffer), " VOLUME_DOWN  %7d  %7d\n", keyTable[1][i], keyTable[2][i]);
							break;
						case 115:
							sprintf (buffer + strlen(buffer), " VOLUME_UP    %7d  %7d\n", keyTable[1][i], keyTable[2][i]);
							break;
						case 116:
							sprintf (buffer + strlen(buffer), " POWER        %7d  %7d\n", keyTable[1][i], keyTable[2][i]);
							break;
						default:
							sprintf (buffer + strlen(buffer), " %-11d  %7d  %7d\n", keyTable[0][i], keyTable[1][i], keyTable[2][i]);
							break;
					}
				}
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				sprintf (buffer, "%d", (working == (pthread_t) -1) ? 0 : 1);
			}
			else if (CMP_CMD (buffer, LOG_GETINTERVAL))
			{
				ret = interval;
				buffer [0] = 0;
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
				buffer [0] = 0;
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
			else if (CMP_CMD (buffer, LOG_GETPARAM))
			{
				pthread_mutex_lock (& data_lock);
				if (hasUI == 1) { strcpy (buffer, "1"); }
				else { strcpy (buffer, "0"); }
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_SETPARAM))
			{
				MAKE_DATA (buffer, LOG_SETPARAM);
				datatok (buffer, tmp);
				if (tmp [0])
				{
					hasUI = atoi (tmp);
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				if (hasUI == 0)
				{
					for ( i = 0; i < keyTableSize; ++i )
					{
						keyTable[0][i] = -1;
						keyTable[1][i] = -1;
						keyTable[2][i] = -1;
					}
				}

				/* start log */
				if (working == (pthread_t) -1)
				{
					if (poll_open (& poller) < 0)
					{
						DM ("pipe: %s\n", strerror (errno));
						ret = -1;
					}
					else if (pthread_create (& working, NULL, thread_main, NULL) < 0)
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
					poll_break (& poller);
					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RECOVERYDATA))
			{
				loadConfig();
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RESETDATA))
			{
				for ( i = 0; i < keyTableSize; ++i )
				{
					keyTable[0][i] = -1;
					keyTable[1][i] = -1;
					keyTable[2][i] = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RESETCONF))
			{
				if (conf == NULL)
				{
					conf = conf_load_from_file (SETTING_FILENAME);
				}
				if (conf != NULL)
				{
					conf_remove_all (conf);
					conf_save_to_file (conf);
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

			pthread_mutex_lock (& data_lock);
			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
			pthread_mutex_unlock (& data_lock);
		}

		pthread_mutex_lock (& data_lock);
		close (commfd);
		commfd = -1;
		pthread_mutex_unlock (& data_lock);
	}

	if (working != (pthread_t) -1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	if (conf != NULL)
	{
		conf_destroy (conf);
		conf = NULL;
	}

	return 0;
}
