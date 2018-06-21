#define	LOG_TAG		"STT:logbattery"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/prctl.h>
#include <cutils/properties.h>

#include "headers/sem.h"
#include "headers/poll.h"
#include "headers/dir.h"
#include "headers/fio.h"
#include "headers/process.h"
#include "headers/battery.h"

#include "common.h"
#include "server.h"

#define	VERSION	"3.4"
/*
 * 3.4	: add bms and usb battery information into system attributes.
 * 3.3	: detect charging source and log into file from system attributes.
 * 3.2	: keep logs in logger history.
 * 3.1	: do bugreport in another thread.
 * 3.0	: enlarge socket buffer.
 * 2.9	: make TCXO log being an option.
 * 2.8	: 1. default saving logs to external storage. 2. update working thread status before socket commands.
 * 2.7	: change log filename.
 * 2.6	: 1. show "Failed!" to user when got NULL battery info. 2. take system attribute as the default method.
 * 2.5	: dump procrank on TI projects.
 * 2.4	: rewrite to use new battery.c.
 * 2.3	: show all available system attributes.
 * 2.2	: correct the sign of temperature value.
 * 2.1	: also do extra dump when starting logger.
 * 2.0	: change commands to support system battery attributes.
 * 1.9	: search GEP battery log too.
 * 1.8	: also save power and battery service info.
 * 1.7	: supports bugreport in both rpc and attribute methods.
 * 1.6	: choose from one of the two attribute files.
 * 1.5	: use dumpstate instead of bugreport in eclair.
 * 1.4	: do not be waken by uevent if interval specified.
 */

/* custom commands */
#define	LOG_GETINTERVAL		":getinterval:"
#define	LOG_SETINTERVAL		":setinterval:"
#define	LOG_GETPATH		":getpath:"
#define	LOG_SETPATH		":setpath:"
#define	LOG_RUN			":run:"
#define	LOG_STOP		":stop:"
#define	LOG_ISLOGGING		":islogging:"
#define	LOG_GETDATA		":getdata:"
#define LOG_GETMETHOD		":getmethod:"
#define LOG_SETMETHOD		":setmethod:"
#define LOG_GETDUMPTCXO		":getdumptcxo:"
#define LOG_SETDUMPTCXO		":setdumptcxo:"
#define LOG_GETDEBUGMORE	":getdebugmore:"
#define LOG_SETDEBUGMORE	":setdebugmore:"

#define	SOCKET_BUFFER_SIZE	(4640)
#define	DUMP_PROCRANK		(0)

static int done = 0;
static int interval = 0;
static int method = BATT_PROG_SYS_ATTR;
static int bugreport_done = 0;
static int dumptcxo = 0;
static int debugmore = 0;
static char data [SOCKET_BUFFER_SIZE];
static char path [PATH_MAX] = LOG_DIR;
static char log_filename [PATH_MAX] = "";

static int ufd = -1;
static POLL poll_uevent = POLL_INITIAL;
static sem_t timed_lock;
static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

/* GEP battery log */
static const char *sys_gep_log = "/sys/kernel/debug/battery_log";
/*
 * timestamp    mV     mA avg mA      uAh   dC   %   src  mode   reg full
 *         2  4167     94    -15  1438400  283 100  none   off  0x01 0
 *        52  4167     71     90  1440000  292 100   usb  slow  0x01 0
 */

static void do_extra_dump (void)
{
	if (dumptcxo)
	{
		system ("am start -n com.htc.android.ssdtest/.TCXOLog");
	}
}

#if DUMP_PROCRANK
//HTC_CSP_START
static void dump_procrank(void)
{
	#define LOG_FILE_NAME DAT_DIR "procrank_"
	#define LOG_FILE_EXTENSION ".txt"
	#define DUMP_COMMAND "procrank > "

	char buf_string[80];
	static int index =1;

	//dump current procrank to file
	sprintf(buf_string,"%s%s%d%s",DUMP_COMMAND,LOG_FILE_NAME,index,LOG_FILE_EXTENSION);
	system(buf_string);
	index++;
}
//HTC_CSP_END
#endif

static void *thread_dump (void *UNUSED_VAR (null))
{
	prctl (PR_SET_NAME, (unsigned long) "logbattery:dump", 0, 0, 0);
	pthread_detach (pthread_self ());
	file_log (LOG_TAG ": bugreport begin\n");
	do_extra_dump ();
	do_bugreport (NULL, 0);
	file_log (LOG_TAG ": bugreport end\n");
	return NULL;
}

static int log_main (int save)
{
	BATT_INFO *pbi;
	char buf [PATH_MAX];
	char timestr [16];
	FILE *fplog = NULL;
	struct tm *ptm;
	time_t t;
	int capacity = 100;
	int create_title = 0;
	int i;

	t = time (NULL);

#if DUMP_PROCRANK
//HTC_CSP_START
	dump_procrank();
//HTC_CSP_END
#endif

	ptm = localtime (& t);

	if (save)
	{
		if (log_filename [0] == 0)
		{
			pthread_mutex_lock (& data_lock);
			if (! ptm)
			{
				sprintf (log_filename, "%sbatt_" LOG_FILE_TAG "_NA_%d.csv", path, getpid ());
			}
			else
			{
				sprintf (log_filename, "%sbatt_" LOG_FILE_TAG "_%04d%02d%02d_%02d%02d%02d.csv", path,
					ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
					ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
			}
			pthread_mutex_unlock (& data_lock);

			fplog = fopen_nointr (log_filename, "wb");

			if (! fplog)
			{
				DM ("%s: %s\n", log_filename, strerror (errno));
				return -1;
			}

			create_title = 1;
		}
		else
		{
			fplog = fopen_nointr (log_filename, "a+b");

			if (! fplog)
			{
				DM ("%s: %s\n", log_filename, strerror (errno));
				return -1;
			}

			fseek (fplog, 0, SEEK_END);
		}

		if (! ptm)
		{
			strncpy (timestr, "N/A", sizeof (timestr));
		}
		else
		{
			snprintf (timestr, sizeof (timestr) - 1, "%02d%02d-%02d:%02d:%02d", ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
			timestr [sizeof (timestr) - 1] = 0;
		}

		if (access (sys_gep_log, R_OK) == 0)
		{
			snprintf (buf, sizeof (buf) - 1, "cat %s > %s.battery_log", sys_gep_log, log_filename);
			buf [sizeof (buf) - 1] = 0;
			system (buf);
		}

		if (debugmore)
		{
			char *ptr = strrchr (log_filename, '.');

			if (ptr) *ptr = 0;

			/* do dumpsys power */
			snprintf (buf, sizeof (buf) - 1, "echo [[[ %s ]]] >> %s.power", timestr, log_filename);
			buf [sizeof (buf) - 1] = 0;
			system (buf);
			snprintf (buf, sizeof (buf) - 1, "dumpsys power >> %s.power", log_filename);
			buf [sizeof (buf) - 1] = 0;
			system (buf);

			/* do dumpsys batteryinfo */
			snprintf (buf, sizeof (buf) - 1, "echo [[[ %s ]]] >> %s.batteryinfo", timestr, log_filename);
			buf [sizeof (buf) - 1] = 0;
			system (buf);
			snprintf (buf, sizeof (buf) - 1, "dumpsys batteryinfo >> %s.batteryinfo", log_filename);
			buf [sizeof (buf) - 1] = 0;
			system (buf);

			if (ptr) *ptr = '.';
		}
	}

	pbi = get_battery_info (method);

	if (! pbi)
	{
		DM ("read battery info failed!\n");
		if (fplog) fclose_nointr (fplog);
		pthread_mutex_lock (& data_lock);
		strcpy (data, "Failed!");
		pthread_mutex_unlock (& data_lock);
		return -1;
	}

	if (save)
	{
		if (create_title)
		{
			fprintf (fplog, "Time");
			for (i = 0; i < pbi->count; i ++)
			{
				fprintf (fplog, ",%s", pbi->fields [i].name);
			}
			fprintf (fplog, "\n");
		}

		fprintf (fplog, "%s", timestr);
		for (i = 0; i < pbi->count; i ++)
		{
			switch (pbi->fields [i].type)
			{
			case BATT_FT_INT:
				fprintf (fplog, ",%d", pbi->fields [i].data.i);
				break;
			case BATT_FT_UINT:
				fprintf (fplog, ",%u", pbi->fields [i].data.ui);
				break;
			case BATT_FT_LONG:
				fprintf (fplog, ",%ld", pbi->fields [i].data.l);
				break;
			case BATT_FT_ULONG:
				fprintf (fplog, ",%lu", pbi->fields [i].data.ul);
				break;
			case BATT_FT_FLOAT:
				fprintf (fplog, ",%.03f", pbi->fields [i].data.f);
				break;
			case BATT_FT_STRING:
				fprintf (fplog, ",%s", pbi->fields [i].data.s);
				break;
			}
		}
		fprintf (fplog, "\n");
	}

	if (fplog) fclose_nointr (fplog);

	pthread_mutex_lock (& data_lock);
	memset (data, 0, sizeof (data));
	for (i = 0; i < pbi->count; i ++)
	{
		switch (pbi->fields [i].type)
		{
		case BATT_FT_INT:
			sprintf (buf, "%-24s %d\n", pbi->fields [i].name, pbi->fields [i].data.i);
			break;
		case BATT_FT_UINT:
			sprintf (buf, "%-24s %u\n", pbi->fields [i].name, pbi->fields [i].data.ui);
			break;
		case BATT_FT_LONG:
			sprintf (buf, "%-24s %ld\n", pbi->fields [i].name, pbi->fields [i].data.l);
			break;
		case BATT_FT_ULONG:
			sprintf (buf, "%-24s %lu\n", pbi->fields [i].name, pbi->fields [i].data.ul);
			break;
		case BATT_FT_FLOAT:
			sprintf (buf, "%-24s %3.03f\n", pbi->fields [i].name, pbi->fields [i].data.f);
			break;
		case BATT_FT_STRING:
			sprintf (buf, "%-24s %s\n", pbi->fields [i].name, pbi->fields [i].data.s);
			break;
		}
		if ((strlen (data) + strlen (buf)) >= SOCKET_BUFFER_SIZE)
		{
			DM ("not enough SOCKET_BUFFER_SIZE! ignore rest fields!");
			break;
		}
		strcat (data, buf);
	}
	if (! data [0]) strcpy (data, "No data!");
	pthread_mutex_unlock (& data_lock);

	if (bugreport_done == 0)
	{
		FILE *file;
		if ((file = fopen_nointr ("/sys/class/power_supply/battery/capacity", "r")) != NULL)
		{
			char value [16] = "";
			if (fscanf (file, "%s", value) == 1)
				capacity = atoi (value);
			fclose_nointr (file);
		}
	}

	if ((capacity > 0) && (capacity <= 5) && (bugreport_done == 0))
	{
		pthread_t dump = (pthread_t) -1;

		if (pthread_create (& dump, NULL, thread_dump, NULL) < 0)
		{
			file_log (LOG_TAG ": failed to create bugreport thread!\n");
		}
		else
		{
			bugreport_done = 1;
		}
	}

	return 0;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	char buf [1024];
	int len;

	prctl (PR_SET_NAME, (unsigned long) "logbattery:work", 0, 0, 0);

	sem_init (& timed_lock, 0, 0);

	do_extra_dump ();

	if (poll_open (& poll_uevent) < 0)
	{
		DM ("cannot create pipe!\n");
		return NULL;
	}

	if ((ufd = local_uevent_register ("logbattery")) < 0)
	{
		DM ("register local uevent failed!\n");
		return NULL;
	}

	buf [0] = 0;

	do
	{
		/* do log */
		if (log_main (1) < 0)
			break;

		/* wait uevent */
		for (;;)
		{
			pthread_mutex_lock (& data_lock);
			len = (interval == 0) ? -1 : (interval * 1000);
			pthread_mutex_unlock (& data_lock);

			if (len == -1)
			{
				if (poll_wait (& poll_uevent, ufd, -1) > 0)
				{
					len = read (ufd, buf, sizeof (buf) - 1);
				}

				if (len < 0)
				{
					pthread_mutex_lock (& data_lock);
					done = 1;
					pthread_mutex_unlock (& data_lock);
					break;
				}

				if (len == 0)
				{
					DM ("got uevent timeout.\n");
					break;
				}

				if (len > 0)
				{
					if (local_uevent_is_ended (buf))
					{
						done = 1;
						break;
					}

					buf [len] = 0;
					if (strstr (buf, "power_supply"))
					{
						DM ("got uevent [%s].\n", buf);
						break;
					}
				}
			}
			else
			{
				timed_wait (& timed_lock, len);
				break;
			}
		}
	}
	while (! done);

	local_uevent_unregister ("logbattery");
	ufd = -1;

	poll_close (& poll_uevent);

	pthread_mutex_lock (& data_lock);
	log_filename [0] = 0;
	data [0] = 0;
	pthread_mutex_unlock (& data_lock);

	return NULL;
}

int logbattery_main (int server_socket)
{
	pthread_t working = (pthread_t) -1;

	char buffer [SOCKET_BUFFER_SIZE + 16];
	int commfd = -1;
	int ret = 0;

	data [0] = 0;

	/* use external storage as the default path */
	snprintf (path, sizeof (path), "%s/" LOG_FOLDER_NAME, dir_get_external_storage ());

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

			if (! is_thread_alive (working))
			{
				working = (pthread_t) -1;
			}

			buffer [sizeof (buffer) - 1] = 0;

			ret = 0;

			//DM ("read command [%s].\n", buffer);

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				pthread_mutex_lock (& data_lock);
				done = 1;
				pthread_mutex_unlock (& data_lock);
				buffer [0] = 0;
				break;
			}
			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, LOG_GETDATA))
			{
				if (working == (pthread_t) -1)
				{
					log_main (0);
				}
				pthread_mutex_lock (& data_lock);
				strcpy (buffer, data);
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				ret = (working == (pthread_t) -1) ? 0 : 1;
				sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, LOG_GETINTERVAL))
			{
				sprintf (buffer, "%d", interval);
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
			else if (CMP_CMD (buffer, LOG_SETINTERVAL))
			{
				/* change log interval */
				MAKE_DATA (buffer, LOG_SETINTERVAL);

				ret = atoi (buffer);

				if (ret >= 0)
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
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				bugreport_done = 0;
				/* start log */
				if (working == (pthread_t) -1)
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
						ret = -1;
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

					/* try to break poll() and sem_wait() */
					if (ufd >= 0)
					{
						poll_break (& poll_uevent);
						sem_post (& timed_lock);
					}

					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_GETMETHOD))
			{
				ret = method;
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_SETMETHOD))
			{
				MAKE_DATA (buffer, LOG_SETMETHOD);

				ret = atoi (buffer);

				if ((ret > BATT_PROG_NONE) && (ret < BATT_PROG_LAST))
				{
					method = ret;
					ret = 0;
					DM ("use method %d.\n", method);
				}
				else
				{
					ret = -1;
				}

				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_GETDUMPTCXO))
			{
				ret = dumptcxo;
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_SETDUMPTCXO))
			{
				MAKE_DATA (buffer, LOG_SETDUMPTCXO);

				ret = atoi (buffer);

				if (ret >= 0)
				{
					dumptcxo = ret;
					ret = 0;
				}
				else
				{
					ret = -1;
				}

				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_GETDEBUGMORE))
			{
				ret = debugmore;
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_SETDEBUGMORE))
			{
				MAKE_DATA (buffer, LOG_SETDEBUGMORE);

				ret = atoi (buffer);

				if (ret >= 0)
				{
					debugmore = ret;
					ret = 0;
				}
				else
				{
					ret = -1;
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

			//DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close (commfd);
		commfd = -1;
	}

	if (ufd >= 0)
	{
		poll_break (& poll_uevent);
		sem_post (& timed_lock);
	}

	if (working != (pthread_t) -1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	return 0;
}
