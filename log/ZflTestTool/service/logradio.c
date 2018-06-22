#define	LOG_TAG		"STT:logradio"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include "headers/fio.h"
#include "headers/dir.h"
#include "headers/process.h"

#include "common.h"
#include "server.h"

#define	VERSION	"2.6"
/*
 * 2.6	: output radio buffer log while stopping logger to trigger router thread quit immediately.
 * 2.5	: support new logger control service.
 * 2.4	: check also missing logging file.
 * 2.3	: share log session with other loggers.
 * 2.2	: remove socket interface.
 * 2.1	: do not use functions would call malloc() or free() in child process.
 * 2.0	: dump logs with threadtime.
 * 1.9	: add storage code to identify the log path.
 * 1.8	: support sn.
 * 1.7	: keep local logs.
 * 1.6	: support auto select log path.
 * 1.5	: use session structure.
 * 1.4	: create timestamp list.
 * 1.3	: set default path to LOG_DIR.
 * 1.2	: add external apis for logctl.
 * 1.1	: change default value by SQA's request.
 */

#define	FILE_PREFIX	"radio_" LOG_FILE_TAG
#define	LOG_ID		LOG_ID_RADIO

#define	KILL_LOGCAT(p)	kill_signal (p, "logcat", NULL, SIGTERM)

static char log_filename [PATH_MAX] = "";
static unsigned long long total_size = 0;
static int pid = 0;
static int pipefd = -1;
static PIPEINFO pipeinfo = {NULL, NULL, 0, -1, PIPE_ROUTER_ERROR_NONE};
static pthread_t router = (pthread_t) -1;

int logradio_init (const char *name, LOGGER_EXTRA_CONFIG **UNUSED_VAR (extra_configs), int *UNUSED_VAR (extra_config_count))
{
	int ret;

	logger_common_pipe_filename (name, log_filename, sizeof (log_filename));

	ret = logger_common_pipe_create (name, log_filename);

	log_filename [0] = 0;
	return ret;
}

void logradio_get_log_filepath (const char *UNUSED_VAR (name), char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s", log_filename);
}

void logradio_clear_log_files (const char *path)
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

int logradio_check_file_type (const char *name, const char *filepath)
{
	return logger_common_check_file_type (name, filepath, FILE_PREFIX, log_filename);
}

unsigned long long logradio_get_total_size (const char *UNUSED_VAR (name))
{
	return total_size;
}

int logradio_update_state (const char *name, int state)
{
	if (pipeinfo.error != PIPE_ROUTER_ERROR_NONE)
	{
		switch (pipeinfo.error)
		{
		case PIPE_ROUTER_ERROR_OPEN:
			state |= LOGGER_STATE_MASK_ERROR_OPEN;
			break;
		case PIPE_ROUTER_ERROR_READ:
			state |= LOGGER_STATE_MASK_ERROR_READ;
			break;
		case PIPE_ROUTER_ERROR_WRITE:
			state |= LOGGER_STATE_MASK_ERROR_WRITE;
			break;
		}
	}
	return logger_common_update_state (name, state, pid, router, log_filename, 1);
}

int logradio_start (const char *name)
{
	char pipefile [PATH_MAX];
	int status;

	GLIST_NEW (fds);

	logger_common_pipe_filename (name, pipefile, sizeof (pipefile));

	if (pipefd != -1)
	{
		pipeinfo.pipefd = -1;

		logger_common_pipe_close (name, pipefd);
		pipefd = -1;
	}

	/*
	 * release zombi child
	 */
	if (is_process_zombi (pid) == 1)
	{
		char *s;

		DM ("waitpid: waiting for zombi child (%d:%c) finish ...\n", pid, get_pid_stat (pid));

		waitpid (pid, & status, WNOHANG);

		s = alloc_waitpid_status_text (status);

		DM ("waitpid: zombi finished. (%s)\n", s);

		if (s) free (s);
	}

	if (! is_process_alive (pid))
	{
		int message_pipe [2];

		if (logger_common_generate_new_file (name, FILE_PREFIX, log_filename, sizeof (log_filename)) != 0)
		{
			file_log (LOG_TAG ": failed to generate new file!\n");
			return -1;
		}

		/*
		 * Do not use functions would call malloc() or free() in child process.
		 * A no-longer-running thread may be holding on to the heap lock, and
		 * an attempt to malloc() or free() would result in deadlock.
		 */
		fds = find_all_fds ();

		if (pipe (message_pipe) < 0)
		{
			glist_clear (& fds, free);
			file_log (LOG_TAG ": failed to create message pipe!\n");
			return -1;
		}

		pid = fork ();

		if (pid == 0)	/* child */
		{
			char *argv [8];
			int i;

			write (message_pipe [1], "beg;", 4);
			close (message_pipe [0]);

			argv [0] = "/system/bin/logcat";
			argv [1] = "-v";
			argv [2] = "threadtime";
			argv [3] = "-f";
			argv [4] = pipefile; // write to a pipe
			argv [5] = "-b";
			argv [6] = "radio";
			argv [7] = NULL;

			write (message_pipe [1], "fg1;", 4);

			close_all_fds (fds);

			write (message_pipe [1], "fg2;", 4);

			for (i = 0; (i < 10) && (logger_common_pipe_validate (name, pipefile, 0) < 0); i ++)
			{
				write (message_pipe [1], "try;", 4);
				sleep (1);
			}

			if (i == 10)
			{
				write (message_pipe [1], "invd", 4);
				unlink (pipefile);
				close (message_pipe [1]);
				exit (0);
			}

			write (message_pipe [1], "exec", 4);
			close (message_pipe [1]);

			execv (argv [0], argv);
			DM ("[embedded] execv: %s\n", strerror (errno));
			exit (127);
		}

		close (message_pipe [1]);

		glist_clear (& fds, free);

		if (pid < 0)
		{
			DM ("fork: %s\n", strerror (errno));
			pid = 0;
		}
		else
		{
			char msg [32];
			int r;

			DM ("fork: parent got child pid (%d).\n", pid);

			/* make sure pipe existing */
			if (logger_common_pipe_create (name, pipefile) < 0)
			{
				KILL_LOGCAT (pid);
				pid = 0;

				pipefd = -1;

				close (message_pipe [0]);

				file_log (LOG_TAG ": failed to create pipe [%s]!\n", pipefile);
				return -1;
			}

			/* dump child's messages */
			for (;;)
			{
				memset (msg, 0, sizeof (msg));

				r = read_timeout (message_pipe [0], msg, sizeof (msg) - 1, 30000);

				if (r == 0 /* timeout */)
				{
					file_log (LOG_TAG ": [embedded] detected child got blocked!!! generate tombstone...\n");
					dump_process (pid);
					break;
				}

				if (r < 0)
					break;

				DM ("child msg: [%s]\n", msg);
			}
			DM ("child msg: end\n");

			/* will bolck here until logcat writing pipe */
			if ((pipefd = logger_common_pipe_open (name, pipefile)) < 0)
			{
				KILL_LOGCAT (pid);
				pid = 0;

				pipefd = -1;

				close (message_pipe [0]);

				file_log (LOG_TAG ": failed to open pipe [%s]!\n", pipefile);
				return -1;
			}

			if (! pipeinfo.name)
			{
				pipeinfo.name = strdup (name);
			}

			pipeinfo.log_filename = log_filename; /* log_filename is a static variable, safe to pass to thread directly */
			pipeinfo.rotated = 0;
			pipeinfo.pipefd = pipefd;
			pipeinfo.error = PIPE_ROUTER_ERROR_NONE;

			if (pthread_create (& router, NULL, logger_common_pipe_router_thread, (void *) & pipeinfo) < 0)
			{
				router = (pthread_t) -1;
				file_log (LOG_TAG ": pthread_create router: %s\n", strerror (errno));
			}
		}

		close (message_pipe [0]);
	}

	file_log (LOG_TAG ": file=%s, logcat=%d\n", log_filename, pid);

	return (pid == 0) ? -1 : 0;
}

int logradio_stop (const char *name)
{
	if (pipefd != -1)
	{
		pipeinfo.pipefd = -1;

		logger_common_pipe_close (name, pipefd);
		pipefd = -1;
	}

	if (router != (pthread_t) -1)
	{
		__android_log_buf_write (LOG_ID, ANDROID_LOG_WARN, LOG_TAG, "stop\n"); /* trigger log output */

		pthread_join (router, NULL);
		router = (pthread_t) -1;

		if (pipeinfo.name)
		{
			free (pipeinfo.name);

			pipeinfo.name = NULL;
		}
	}

	if (is_process_alive (pid))
	{
		if (KILL_LOGCAT (pid) < 0)
		{
			file_log (LOG_TAG ": [embedded] failed to kill child (%d), skipped!\n", pid);
		}
		else
		{
			int i, last, status = 0, c = get_pid_stat (pid);
			char *s;

			for (i = 0; i < 10; i ++)
			{
				last = get_pid_stat (pid);

				DM ("waitpid: waiting for child (%d:%c) finish ... %d\n", pid, last, i);

				if (waitpid (pid, & status, WNOHANG) > 0)
					break;

				usleep (100000);
			}

			s = alloc_waitpid_status_text (status);

			DM ("waitpid: finished. (%s)\n", s);

			file_log (LOG_TAG ": %d (%c,%c) stopped, retried %d, %s\n", pid, c, last, i, s);

			if (s) free (s);
		}

		log_filename [0] = 0;

		pid = 0;

		return 0;
	}
	return -1;
}

static int do_rotate (int logdatafd, const char *name, int compress)
{
	int ret = -1;

	if (! is_process_alive (pid))
	{
		logradio_stop (name);

		if (compress)
		{
			logger_common_logdata_compress_file (logdatafd, name, log_filename);
		}

		ret = logradio_start (name);
	}
	else
	{
		char *old_filename = strdup (log_filename);

		if (logger_common_generate_new_file (name, FILE_PREFIX, log_filename, sizeof (log_filename)) != 0)
		{
			file_log (LOG_TAG ": failed to generate new file!\n");
			ret = -1;
		}
		else
		{
			for (ret = 0, pipeinfo.rotated = 1; (ret < 10) && (pipeinfo.rotated != 0 /* this value will be updated by router thread */); ret ++)
			{
				__android_log_buf_write (LOG_ID, ANDROID_LOG_WARN, LOG_TAG, "rotate\n"); /* trigger log output */

				usleep (200000);
			}

			file_log (LOG_TAG ": file=%s, retry=%d, logcat=%d\n", log_filename, ret, pid);

			if (compress && old_filename)
			{
				logger_common_logdata_compress_file (logdatafd, name, old_filename);
			}

			ret = 0;
		}

		if (old_filename) free (old_filename);
	}

	return ret;
}

int logradio_rotate_and_limit (const char *name, int state, unsigned long rotate_size_mb, unsigned long limited_size_mb, unsigned long reserved_size_mb)
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

		ret = do_rotate (fd, name, compress);
	}

end:;
	if (fd >= 0)
	{
		close (fd);
	}
	return ret;
}

