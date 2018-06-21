#define	LOG_TAG		"STT:dumplastkmsg"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/input.h>

#include "headers/dir.h"

#include "common.h"
#include "server.h"

#define	VERSION	"2.4"
/*
 * 2.4	: support new last kmsg path on T1. Requested by Woody Lin/Jone Chou, Google changed the path since linux kernel 3.10.
 * 2.3	: support auto clear and getting log size.
 * 2.2	: support new logger control service.
 * 2.1	: share log session with other loggers.
 * 2.0	: remove socket interface.
 * 1.9	: add storage code to identify the log path.
 * 1.8	: support sn.
 * 1.7	: keep local logs.
 * 1.6	: support auto select log path.
 * 1.5	: use session structure.
 * 1.4	: create timestamp list.
 * 1.3	: 1. set default path to LOG_DIR. 2. when logdevice is logging, use its setting.
 * 1.2	: let logctl know this timestamp.
 * 1.1	: change log file name.
 * 1.0	: dump last kmsg. (Pierce Chen)
 */

#define	FILE_PREFIX	"lastkmsg_" LOG_FILE_TAG

const char *lastks [] = {
	"/proc/last_kmsg",
	"/sys/fs/pstore/console-ramoops", // T1
	NULL
};
const char *lastk = NULL;

static char log_filename [PATH_MAX] = "";
static unsigned long long total_size = 0;

static int log_state = LOGGER_STATE_MASK_STOPPED;

static const char *find_lastk (void)
{
	int i;

	for (i = 0; lastks [i]; i ++)
	{
		if (access (lastks [i], F_OK) == 0)
		{
			lastk = lastks [i];
			break;
		}
	}

	DM ("found lastk [%s]\n", lastk);

	if (lastk && (access (lastk, R_OK) == 0))
		return lastk;

	return NULL;
}

void dumplastkmsg_get_log_filepath (const char *UNUSED_VAR (name), char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s", log_filename);
}

void dumplastkmsg_clear_log_files (const char *path)
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

int dumplastkmsg_check_file_type (const char *name, const char *filepath)
{
	return logger_common_check_file_type (name, filepath, FILE_PREFIX, log_filename);
}

unsigned long long dumplastkmsg_get_total_size (const char *UNUSED_VAR (name))
{
	return total_size;
}

int dumplastkmsg_update_state (const char *UNUSED_VAR (name), int UNUSED_VAR (state))
{
	return log_state;
}

int dumplastkmsg_start (const char *name)
{
	char buffer [PATH_MAX];

	log_state = LOGGER_STATE_MASK_STARTED;

	if (find_lastk ())
	{
		if (logger_common_generate_new_file (name, FILE_PREFIX, log_filename, sizeof (log_filename)) != 0)
		{
			file_log (LOG_TAG ": failed to generate new file!\n");
			return -1;
		}

		SAFE_SPRINTF (buffer, sizeof (buffer), "/system/bin/cat %s > %s", lastk, log_filename);

		DM ("run [%s]\n", buffer);

		system (buffer);

		file_log (LOG_TAG ": file=%s\n", log_filename);

		log_state |= LOGGER_STATE_MASK_LOGGING;
	}
	else
	{
		file_log (LOG_TAG ": cannot access [%s]!\n", lastk);
	}
	return 0;
}

int dumplastkmsg_stop (const char *UNUSED_VAR (name))
{
	log_state = LOGGER_STATE_MASK_STOPPED;
	return 0;
}

int dumplastkmsg_rotate_and_limit (const char *name, int UNUSED_VAR (state), unsigned long UNUSED_VAR (rotate_size_mb), unsigned long limited_size_mb, unsigned long reserved_size_mb)
{
	char path_with_slash [PATH_MAX], *ptr;
	int fd = -1, ret = -1;

	if (log_filename [0] == 0) /* no log file, maybe /proc/last_kmsg does not exist */
		goto end;

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

end:;
	if (fd >= 0)
	{
		close (fd);
	}
	return ret;
}
