#define	LOGGER_NAME	"logmeminfo"

#define	LOG_TAG		"STT:" LOGGER_NAME

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

#define	VERSION	"1.6"
/*
 * 1.6	: Do not set thread id to -1 inside thread to prevent timing issue.
 * 1.5	: set 10 times of extra config interval timer to log dumpsys meminfo.
 * 1.4	: set 10 times of extra config interval timer to log ION info.
 * 1.3	: support IONinfo.
 * 1.2	: support vmallocinfo, kmemleak and procrank.
 * 1.1	: adjust log message and support slabinfo.
 * 1.0	: log meminfo.
 */

#define	FILE_PREFIX	"meminfo_" LOG_FILE_TAG

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t working = (pthread_t) -1;
static sem_t timed_lock;

static int done = 0;
static char log_filename [PATH_MAX] = "";
static unsigned long long total_size = 0;

static int write_error_flag = 0;

#define	EXTRA_CONFIG_COUNT			(6)
#define	EXTRA_CONFIG_INTERVAL_KEY				LOGGER_CONFIG_KEY (LOGGER_NAME, "Interval")
#define	EXTRA_CONFIG_INTERVAL_DEFAULT			"60"
#define	EXTRA_CONFIG_VMALLOCINFO_KEY			LOGGER_CONFIG_KEY (LOGGER_NAME, "EnableVmallocinfo")
#define	EXTRA_CONFIG_VMALLOCINFO_DEFAULT		"false"
#define	EXTRA_CONFIG_KMEMLEAK_KEY				LOGGER_CONFIG_KEY (LOGGER_NAME, "EnableKmemleak")
#define	EXTRA_CONFIG_KMEMLEAK_DEFAULT			"false"
#define	EXTRA_CONFIG_PROCRANK_KEY				LOGGER_CONFIG_KEY (LOGGER_NAME, "EnableProcrank")
#define	EXTRA_CONFIG_PROCRANK_DEFAULT			"false"
#define	EXTRA_CONFIG_IONINFO_KEY				LOGGER_CONFIG_KEY (LOGGER_NAME, "EnableIONinfo")
#define	EXTRA_CONFIG_IONINFO_DEFAULT			"false"
#define	EXTRA_CONFIG_DUMPSYS_MEMINFO_KEY		LOGGER_CONFIG_KEY (LOGGER_NAME, "EnableDumpsysMeminfo")
#define	EXTRA_CONFIG_DUMPSYS_MEMINFO_DEFAULT	"false"

#define EXTRA_CONFIG_IONINFO_COUNTER			10
#define EXTRA_CONFIG_DUMPSYS_MEMINFO_COUNTER	10

static char extra_config_interval [8]			= EXTRA_CONFIG_INTERVAL_DEFAULT;
static char extra_config_vmallocinfo [8]		= EXTRA_CONFIG_VMALLOCINFO_DEFAULT;
static char extra_config_kmemleak [8]			= EXTRA_CONFIG_KMEMLEAK_DEFAULT;
static char extra_config_procrank [8]			= EXTRA_CONFIG_PROCRANK_DEFAULT;
static char extra_config_ioninfo [8]			= EXTRA_CONFIG_IONINFO_DEFAULT;
static char extra_config_dumpsys_meminfo [8]	= EXTRA_CONFIG_DUMPSYS_MEMINFO_DEFAULT;

static LOGGER_EXTRA_CONFIG local_extra_configs [EXTRA_CONFIG_COUNT] = {
	{
		"Interval (sec.)", LOGGER_EXTRA_CONFIG_DATA_TYPE_INTEGER,
		EXTRA_CONFIG_INTERVAL_KEY, EXTRA_CONFIG_INTERVAL_DEFAULT,
		extra_config_interval, sizeof (extra_config_interval)
	}
	,
	{
		"Include vmallocinfo", LOGGER_EXTRA_CONFIG_DATA_TYPE_BOOLEAN,
		EXTRA_CONFIG_VMALLOCINFO_KEY, EXTRA_CONFIG_VMALLOCINFO_DEFAULT,
		extra_config_vmallocinfo, sizeof (extra_config_vmallocinfo)
	}
	,
	{
		"Include kmemleak", LOGGER_EXTRA_CONFIG_DATA_TYPE_BOOLEAN,
		EXTRA_CONFIG_KMEMLEAK_KEY, EXTRA_CONFIG_KMEMLEAK_DEFAULT,
		extra_config_kmemleak, sizeof (extra_config_kmemleak)
	}
	,
	{
		"Include procrank (WARN: WOULD IMPACT SYSTEM PERFORMANCE)", LOGGER_EXTRA_CONFIG_DATA_TYPE_BOOLEAN,
		EXTRA_CONFIG_PROCRANK_KEY, EXTRA_CONFIG_PROCRANK_DEFAULT,
		extra_config_procrank, sizeof (extra_config_procrank)
	}
	,
	{
		"Include IONinfo [Log every 10 times of interval.]", LOGGER_EXTRA_CONFIG_DATA_TYPE_BOOLEAN,
		EXTRA_CONFIG_IONINFO_KEY, EXTRA_CONFIG_IONINFO_DEFAULT,
		extra_config_ioninfo, sizeof (extra_config_ioninfo)
	}
	,
	{
		"Include dumpsys meminfo [Log every 10 times of interval.]", LOGGER_EXTRA_CONFIG_DATA_TYPE_BOOLEAN,
		EXTRA_CONFIG_DUMPSYS_MEMINFO_KEY, EXTRA_CONFIG_DUMPSYS_MEMINFO_DEFAULT,
		extra_config_dumpsys_meminfo, sizeof (extra_config_dumpsys_meminfo)
	}
};

static int get_extra_config_interval (void)
{
	int i, ret;

	for (i = 0, ret = 0; i < EXTRA_CONFIG_COUNT; i ++)
	{
		if (strcmp (local_extra_configs [i].conf_key, EXTRA_CONFIG_INTERVAL_KEY) == 0)
		{
			if (local_extra_configs [i].value)
			{
				local_extra_configs [i].value [local_extra_configs [i].value_len - 1] = 0;

				ret = atoi (local_extra_configs [i].value);
			}
			if (ret <= 0)
			{
				ret = atoi (local_extra_configs [i].default_value);
			}
			break;
		}
	}

	if (ret <= 0)
	{
		ret = atoi (EXTRA_CONFIG_INTERVAL_DEFAULT);
	}
	return ret;
}

static int get_extra_config_ion_counter (void)
{
	return EXTRA_CONFIG_IONINFO_COUNTER;
}

static int get_extra_config_dumpsys_meminfo_counter (void)
{
	return EXTRA_CONFIG_DUMPSYS_MEMINFO_COUNTER;
}

static int get_extra_config_enable (const char *key, const char *default_value)
{
	int i, ret;

	ret = IS_VALUE_TRUE (default_value);

	for (i = 0, ret = 0; i < EXTRA_CONFIG_COUNT; i ++)
	{
		if (strcmp (local_extra_configs [i].conf_key, key) == 0)
		{
			if (local_extra_configs [i].value)
			{
				local_extra_configs [i].value [local_extra_configs [i].value_len - 1] = 0;

				ret = IS_VALUE_TRUE (local_extra_configs [i].value);
			}
			break;
		}
	}
	return ret;
}

static int get_extra_config_vmallocinfo ()
{
	return get_extra_config_enable (EXTRA_CONFIG_VMALLOCINFO_KEY, EXTRA_CONFIG_VMALLOCINFO_DEFAULT);
}

static int get_extra_config_kmemleak (void)
{
	return get_extra_config_enable (EXTRA_CONFIG_KMEMLEAK_KEY, EXTRA_CONFIG_KMEMLEAK_DEFAULT);
}

static int get_extra_config_procrank (void)
{
	return get_extra_config_enable (EXTRA_CONFIG_PROCRANK_KEY, EXTRA_CONFIG_PROCRANK_DEFAULT);
}

static int get_extra_config_ioninfo (void)
{
	return get_extra_config_enable (EXTRA_CONFIG_IONINFO_KEY, EXTRA_CONFIG_IONINFO_DEFAULT);
}

static int get_extra_config_dumpsys_meminfo (void)
{
	return get_extra_config_enable (EXTRA_CONFIG_DUMPSYS_MEMINFO_KEY, EXTRA_CONFIG_DUMPSYS_MEMINFO_DEFAULT);
}

int logmeminfo_init (const char *UNUSED_VAR (name), LOGGER_EXTRA_CONFIG **extra_configs, int *extra_config_count)
{
	*extra_configs = local_extra_configs;
	*extra_config_count = EXTRA_CONFIG_COUNT;
	return 0;
}

static char *get_cur_time (void)
{
	static char curtime [24]; /* static buffer for return */

	struct tm *ptm;
	time_t t;

	t = time (NULL);

	ptm = localtime (& t);

	curtime [0] = 0;

	SAFE_SPRINTF (curtime, sizeof (curtime), "%04d-%02d-%02d %02d:%02d:%02d",
			ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
			ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

	return curtime;
}

static int dump_file (int logfd, const char *prompt, char *buf, int buflen, const char *filepath)
{
	int count;
	int fd = -1, ret = -1;

	if ((! filepath) || (buflen <= 1))
		goto end;

	/*
	 * write timestamp
	 */
	if (prompt)
	{
		SAFE_SPRINTF (buf, buflen, "------ %s (%s: %s) ------\n", prompt, filepath, get_cur_time ());

		if (write_nointr (logfd, buf, strlen (buf)) < 0)
		{
			file_log (LOG_TAG ": write_nointr time: %s\n", strerror (errno));
			goto end;
		}
	}

	/*
	 * read and write file
	 */
	fd = open_nointr (filepath, O_RDONLY, DEFAULT_FILE_MODE);

	if (fd < 0)
	{
		SAFE_SPRINTF (buf, buflen, "%s: %s\n\n", filepath, strerror (errno));
		write_nointr (logfd, buf, strlen (buf));
		ret = 0;
		goto end;
	}

	for (;;)
	{
		count = read_nointr (fd, buf, buflen - 1);

		if (count == 0)
			break;

		if (count < 0)
		{
			file_log (LOG_TAG ": read_nointr [%s]: %s\n", filepath, strerror (errno));
			goto end;
		}

		buf [count] = 0;

		count = write_nointr (logfd, buf, count);

		if (count < 0)
		{
			file_log (LOG_TAG ": write_nointr log: %s\n", strerror (errno));
			goto end;
		}
	}

	buf [0] = '\n';
	buf [1] = 0;
	write_nointr (logfd, buf, strlen (buf));
	ret = 0;

end:;
	if (fd >= 0)
		close_nointr (fd);

	return ret;
}

static int dump_command_output (int logfd, const char *prompt, char *buf, int buflen, const char *command)
{
	const char *tmp = "/data/.logmeminfo.tmp";
	int cmdlen;
	char *cmd = NULL;
	int ret = -1;

	if ((! command) || (buflen <= 1))
		goto end;

	/*
	 * write timestamp
	 */
	SAFE_SPRINTF (buf, buflen, "------ %s (%s: %s) ------\n", prompt, command, get_cur_time ());

	if (write_nointr (logfd, buf, strlen (buf)) < 0)
	{
		file_log (LOG_TAG ": write_nointr time: %s\n", strerror (errno));
		goto end;
	}

	/*
	 * run command
	 */
	unlink (tmp);

	cmdlen = strlen (command) + 32 /* " > /data/.logmeminfo.tmp 2>&1" */;

	if ((cmd = malloc (cmdlen)) == NULL)
	{
		SAFE_SPRINTF (buf, buflen, "%s: malloc: %s\n\n", command, strerror (errno));
		write_nointr (logfd, buf, strlen (buf));
		goto end;
	}

	SAFE_SPRINTF (cmd, cmdlen, "%s > %s 2>&1", command, tmp);
	cmdlen = system (cmd);

	SAFE_SPRINTF (buf, buflen, "%s: exitcode: %d\n", command, cmdlen);
	write_nointr (logfd, buf, strlen (buf));

	ret = dump_file (logfd, NULL, buf, buflen, tmp);

end:;
	unlink (tmp);

	if (cmd)
		free (cmd);

	return ret;
}

static void *thread_main (void *arg)
{
	const char *name = arg;

	const char *kmemleak_nodes [] = {
		"/proc/kmemleak",
		"/sys/kernel/debug/kmemleak",
		NULL
	};
	const char *kmemleak_node = NULL;

	const char *ioninfo_nodes [] = {
		"/sys/kernel/debug/ion/heaps/system",			// For QCT
		"/sys/kernel/debug/ion/heaps/ion_mm_heap",		// For MTK
		NULL
	};
	const char *ioninfo_node = NULL;

	char mbuf [4096];

	int interval;
	int enable_vmallocinfo;
	int enable_kmemleak;
	int enable_procrank;
	int enable_ioninfo;
	int ion_counter;
	int ion_countdown;
	int enable_dumpsys_meminfo;
	int dumpsys_meminfo_counter;
	int dumpsys_meminfo_countdown;
	int i;
	int fd = -1;
	int write_error = 0;

	prctl (PR_SET_NAME, (unsigned long) LOGGER_NAME ":logger", 0, 0, 0);

	sem_init (& timed_lock, 0, 0);

	interval = get_extra_config_interval ();
	enable_vmallocinfo = get_extra_config_vmallocinfo ();
	enable_kmemleak = get_extra_config_kmemleak ();
	enable_procrank = get_extra_config_procrank ();

	enable_ioninfo = get_extra_config_ioninfo ();
	ion_counter = get_extra_config_ion_counter ();		// the interval to log ION info will be the 10 times of extra config interval time.
	ion_countdown = ion_counter;

	enable_dumpsys_meminfo = get_extra_config_dumpsys_meminfo ();
	dumpsys_meminfo_counter = get_extra_config_dumpsys_meminfo_counter ();		// the interval to log dumpsys meminfo will be the 10 times of extra config interval time.
	dumpsys_meminfo_countdown = ion_counter;

	for (i = 0; kmemleak_nodes [i]; i ++)
	{
		if (access (kmemleak_nodes [i], R_OK) == 0)
		{
			kmemleak_node = kmemleak_nodes [i];
			break;
		}
	}

	for (i = 0; ioninfo_nodes [i]; i ++)
	{
		if (access (ioninfo_nodes [i], R_OK) == 0)
		{
			ioninfo_node = ioninfo_nodes [i];
			break;
		}
	}

	file_log (LOG_TAG ": file=%s, interval=%d, vmallocinfo=%d, kmemleak=%d (%s), procrank=%d, ioninfo=%d (%s).\n",
		log_filename,
		interval,
		enable_vmallocinfo,
		enable_kmemleak, kmemleak_node,
		enable_procrank,
		enable_ioninfo, ioninfo_node);

	if (! kmemleak_node)
	{
		/* assign a dummy node */
		kmemleak_node = kmemleak_nodes [0];
	}

	if (! ioninfo_node)
	{
		/* assign a dummy node */
		ioninfo_node = ioninfo_nodes [0];
	}

	DM ("logging meminfo ... done=%d\n", done);

	for (; ! done;)
	{
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

		/* meminfo */
		if (dump_file (fd, "MEMORY INFO", mbuf, sizeof (mbuf), "/proc/meminfo") < 0)
		{
			write_error = 1;
			break;
		}

		/* slabinfo */
		if (dump_file (fd, "SLAB INFO", mbuf, sizeof (mbuf), "/proc/slabinfo") < 0)
		{
			write_error = 1;
			break;
		}

		/* vmallocinfo */
		if (enable_vmallocinfo && (dump_file (fd, "VMALLOC INFO", mbuf, sizeof (mbuf), "/proc/vmallocinfo") < 0))
		{
			write_error = 1;
			break;
		}

		/* kmemleak */
		if (enable_kmemleak && (dump_file (fd, "KMEMLEAK", mbuf, sizeof (mbuf), kmemleak_node) < 0))
		{
			write_error = 1;
			break;
		}

		/* procrank */
		if (enable_procrank && (dump_command_output (fd, "PROCRANK", mbuf, sizeof (mbuf), "/system/xbin/procrank") < 0))
		{
			write_error = 1;
			break;
		}

		/* ION info */
		if (enable_ioninfo)
		{
			if (ion_countdown <= 0)				// the interval to log ION info will be the 10 times of extra config interval time.
			{
				ion_countdown = ion_counter;
				if (dump_file (fd, "ION INFO", mbuf, sizeof (mbuf), ioninfo_node) < 0)
				{
					write_error = 1;
					break;
				}
			}
			ion_countdown --;
		}

		/* dumpsys meminfo */
		if (enable_dumpsys_meminfo)
		{
			if (dumpsys_meminfo_countdown <= 0)				// the interval to log dumpsys meminfo will be the 10 times of extra config interval time.
			{
				dumpsys_meminfo_countdown = dumpsys_meminfo_counter;
				if (dump_command_output (fd, "DUMPSYS MEMINFO", mbuf, sizeof (mbuf), "/system/bin/dumpsys meminfo") < 0)
				{
					write_error = 1;
					break;
				}
			}
			dumpsys_meminfo_countdown --;
		}

		/*
		 * sleep
		 */
		timed_wait (& timed_lock, interval * 1000);
	}

end:;
	if (fd >= 0)
	{
		close_nointr (fd);
		fd = -1;
	}

	pthread_mutex_lock (& data_lock);
	done = 1;
	log_filename [0] = 0;
	pthread_mutex_unlock (& data_lock);

	if (write_error)
	{
		write_error_flag = 1;
	}

	file_log (LOG_TAG ": stopped\n");

	DM ("stop logging meminfo ...\n");
	return NULL;
}

void logmeminfo_get_log_filepath (const char *UNUSED_VAR (name), char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s", log_filename);
}

void logmeminfo_clear_log_files (const char *path)
{
	char buffer [32];
	logger_common_logdata_filename ("", FILE_PREFIX, buffer, sizeof (buffer));
	GLIST_NEW (patterns);
	glist_add (& patterns, FILE_PREFIX);
	glist_add (& patterns, buffer);
	glist_add (& patterns, "htclog_"); /* also clear logs in htclog_xxx sub-folders */
	dir_clear (path, patterns);
	glist_clear (& patterns, NULL);
	total_size = 0;
}

int logmeminfo_check_file_type (const char *name, const char *filepath)
{
	return logger_common_check_file_type (name, filepath, FILE_PREFIX, log_filename);
}

unsigned long long logmeminfo_get_total_size (const char *UNUSED_VAR (name))
{
	return total_size;
}

int logmeminfo_update_state (const char *name, int state)
{
	if (write_error_flag)
	{
		state |= LOGGER_STATE_MASK_ERROR_WRITE;
		write_error_flag = 0;
	}
	return logger_common_update_state (name, state, -1, working, log_filename, 0);
}

int logmeminfo_start (const char *name)
{
	int ret = 0;

	write_error_flag = 0;

	if (working == (pthread_t) -1)
	{
		if (logger_common_generate_new_file (name, FILE_PREFIX, log_filename, sizeof (log_filename)) != 0)
		{
			file_log (LOG_TAG ": failed to generate new file!\n");
			ret = -1;
		}
		else if (pthread_create (& working, NULL, thread_main, (void *) name) < 0)
		{
			DM ("pthread_create: %s\n", strerror (errno));
			ret = -1;
		}
	}
	return ret;
}

int logmeminfo_stop (const char *UNUSED_VAR (name))
{
	pthread_mutex_lock (& data_lock);
	done = 1;
	pthread_mutex_unlock (& data_lock);

	if (working != (pthread_t) -1)
	{
		/* quit thread */
		sem_post (& timed_lock);
		pthread_join (working, NULL);
		working = (pthread_t) -1;
	}

	pthread_mutex_lock (& data_lock);
	done = 0;
	pthread_mutex_unlock (& data_lock);
	return 0;
}

int logmeminfo_rotate_and_limit (const char *name, int state, unsigned long rotate_size_mb, unsigned long limited_size_mb, unsigned long reserved_size_mb)
{
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

		logmeminfo_stop (name);

		if (compress)
		{
			logger_common_logdata_compress_file (fd, name, log_filename);
		}

		ret = logmeminfo_start (name);
	}

end:;
	if (fd >= 0)
	{
		close (fd);
	}
	return ret;
}
