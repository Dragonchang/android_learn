#define	LOG_TAG		"STT:proctrl"

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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common.h"
#include "server.h"

#include "headers/process.h"

/* ======================= */
/* === Service Version === */
/* ======================= */

#define	VERSION	"1.6"
/*
 * 1.6	: Add flag to teminate child process while parent got killed.
 * 1.5	: do not use functions would call malloc() or free() in child process.
 * 1.4	: stop loggers before reboot.
 * 1.3	: do special check on reboot command.
 * 1.2	: show some debug messages in child process.
 * 1.1	: renamed to "proctrl" (origin is "runcmd"), do process control instead of simply running commands.
 * 1.0	: initial commit.
 */

/* ======================= */
/* === Custom Commands === */
/* ======================= */

#define	CMD_ADDARG		":addarg:"
/*
 * Add one argument to command line.
 * Param	: argument string
 * Return	: 0 for success, -1 for failure
 * Note		: 1. add empty string "" to clear arguments set before, 2. use TAG_DATETIME to present time code in strings.
 */

#define	CMD_RUN			":run:"
/*
 * Run command line and wait for complete.
 * Param	: none
 * Return	: process exit code
 * Note		: none
 */

#define	CMD_KILL		":kill:"
/*
 * Kill a process.
 * Param	: PID
 * Return	: 0 for success, -1 for failure
 * Note		: none
 */

#define	CMD_ISALIVE		":isalive:"
/*
 * Check if a process is alive.
 * Param	: PID
 * Return	: 1 for alive, 0 for not alive, -1 for failure
 * Note		: none
 */

#define	CMD_RUN_SERVICE		":runservice:"
/*
 * Run command line, return immediately, and monitor the process status.
 * Param	: name of the service
 * Return	: 0 for success, -1 for failure
 * Note		: if the process died, run it again
 */

#define	CMD_RUN_SERVICE_ONE	":runserviceone:"
/*
 * Run command line, return immediately, and monitor the process status.
 * Param	: name of the service
 * Return	: 0 for success, -1 for failure
 * Note		: if the process died, remove it from monitoring list
 */

#define	CMD_STOP_SERVICE	":stopservice:"
/*
 * Run command line and return immediately, and monitor the process status.
 * Param	: name of the service
 * Return	: 0 for success, -1 for failure
 * Note		: remove process from monitoring list
 */

#define	CMD_CHECK_SERVICE	":checkservice:"
/*
 * Run command line and return immediately, and monitor the process status.
 * Param	: name of the service
 * Return	: 1 for monitoring, 0 for not monitoring, -1 for failure
 * Note		: none
 */

/* ========================= */
/* === Service Variables === */
/* ========================= */

#define	MONITOR_INTERVAL	5	/* seconds */

typedef struct {
	pid_t	pid;	/* process id */
	int	count;	/* rest times to run, -1 for infinit */
	GLIST	*cmd;	/* command line */
	char	*key;	/* service key for identification */
} PROCESS;

static GLIST_NEW (monitoring);
static int thread_stop = 0;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* ================== */
/* === Procedures === */
/* ================== */

/* dump command line for debug */
static void dump_command_line (int argc, char **argv)
{
	int i;
	DM ("command line:\n");
	for (i = 0; i < argc; i ++) DM ("\t%s\n", argv [i]);
}

/* fork new process and run command in it */
static int run_command (GLIST *cmd)
{
	char **argv = NULL;
	int len, i;
	int pid = -1;

	GLIST_NEW (fds);

	if (! cmd)
		return -1;

	/*
	 * Do not use functions would call malloc() or free() in child process.
	 * A no-longer-running thread may be holding on to the heap lock, and
	 * an attempt to malloc() or free() would result in deadlock.
	 */
	len = glist_length (& cmd);

	if (len > 0)
	{
		argv = (char **) malloc ((len + 1) * sizeof (char *));

		DM ("argc = %d, argv = %p\n", len, argv);

		for (i = 0; i < len; i ++)
		{
			argv [i] = strdup ((char *) glist_get (& cmd, i));
			str_replace_tags (argv [i]);
		}
		argv [len] = NULL;

		dump_command_line (len, argv);

		/*
		 * special steps for reboot command
		 */
		if ((strcmp (argv [0], "/system/bin/reboot") == 0) || (strcmp (argv [0], "/system/bin/reboot.stt") == 0))
		{
			const char *power_test_config = "/data/ghost/PowerTest.conf";

			DM ("is a reboot command.\n");

			if (access (power_test_config, F_OK) == 0)
			{
				int fd = open (power_test_config, O_RDONLY);

				if (fd < 0)
				{
					DM ("read %s: %s\n", power_test_config, strerror (errno));
				}
				else
				{
					off_t length = lseek (fd, 0, SEEK_END);

					if (length == (off_t) -1)
					{
						DM ("lseek %s: %s\n", power_test_config, strerror (errno));
					}
					else
					{
						DM ("length of %s = %ld\n", power_test_config, length);

						//file_log (LOG_TAG ": [%s][%u]\n", power_test_config, length);
					}

					close (fd);
				}
			}
			else
			{
				DM ("%s is not existed.\n", power_test_config);
			}

			sleep (1);

			/*
			 * sync with the reboot_service in "system/core/adb/services.c".
			 *
			 * Attempt to unmount the SD card first.
			 * No need to bother checking for errors.
			 */
			sync ();

			pid = fork ();

			if (pid == 0)
			{
				/* ask vdc to unmount it */
				execl ("/system/bin/vdc", "/system/bin/vdc",
					"volume",
					"unmount",
					getenv ("EXTERNAL_STORAGE"),
					"force",
					NULL);
			}
			else if (pid > 0)
			{
				/* wait until vdc succeeds or fails */
				waitpid (pid, & i, 0);
			}

			/*
			 * stop loggers
			 */
			send_command_to_service (0, "logctl", ":stop:", NULL, 0);
		}
	}

	fds = find_all_fds ();

	pid = fork ();

	if (pid == 0)	/* child, run the command */
	{
		close_all_fds (fds);
		//DM ("before execv ...\n");
		prctl (PR_SET_PDEATHSIG, SIGKILL);
		execv (argv [0], argv);
		//DM ("execv: %s\n", strerror (errno));
		exit (127);
	}

	glist_clear (& fds, free);

	if (pid > 0)
	{
		DM ("fork: %d\n", pid);
	}

	if (pid < 0)
	{
		DM ("fork: %s\n", strerror (errno));
		pid = -1;
	}

	if (argv)
	{
		for (i = 0; i < len; i ++)
		{
			if (argv [i]) free (argv [i]);
		}
		free (argv);
	}
	return pid;
}

/* helper functions to handle PROCESS structures */
static int monitor_compare (void *key, void *process)
{
	PROCESS *p = process;
	if (key && p && p->key) return strcmp (key, p->key);
	return -1;
}
static int monitor_find (const char *key)
{
	return glist_find_ex (& monitoring, (char *) key, monitor_compare);
}
static int monitor_add (const char *key, const int count, GLIST *cmd)
{
	PROCESS *p;
	if (key && (monitor_find (key) >= 0))
		return -1;
	p = malloc (sizeof (PROCESS));
	if (! p)
	{
		DM ("malloc failed!");
		return -1;
	}
	p->pid = -1;
	p->count = count;
	p->cmd = cmd;
	p->key = key ? strdup (key) : NULL;
	glist_append (& monitoring, p);
	return 0;
}
static void monitor_free (void *_p)
{
	if (_p)
	{
		PROCESS *p = _p;
		if (is_process_zombi (p->pid) == 1)
		{
			DM ("waitpid: waiting for zombi child (%d:%s:%c) finish ...\n", p->pid, p->key, get_pid_stat (p->pid));
			waitpid (p->pid, NULL, 0);
			DM ("waitpid: zombi finished.\n");
		}
		if (p->key) free (p->key);
		if (p->cmd) glist_clear (& p->cmd, free);
		free (p);
	}
}
static void monitor_remove (const char *key)
{
	PROCESS *p;
	int len, i;

	if (! key)
		return;

	if ((len = glist_length (& monitoring)) > 0)
	{
		for (i = 0; i < len; i ++)
		{
			p = (PROCESS *) glist_get (& monitoring, i);

			if (p->key && (strcmp (key, p->key) == 0))
			{
				if (is_process_alive (p->pid))
				{
					int c = get_pid_stat (p->pid);
					kill (p->pid, SIGTERM);
					DM ("waitpid: waiting for child (%d:%s:%c) finish ...\n", p->pid, p->key, c);
					waitpid (p->pid, NULL, 0);
					DM ("waitpid: finished.\n");
				}
				glist_delete (& monitoring, i, monitor_free);
				break;
			}
		}
	}
}
static void monitor_clear (void)
{
	if (monitoring)
	{
		glist_clear (& monitoring, monitor_free);
		monitoring = NULL;
	}
}

/* thread to monitor process life */
static void *thread_monitor_impl (void *arg)
{
	PROCESS *p;
	struct timespec ts;
	int count, i;

	if (! monitoring)
		return NULL;

	DM ("start process minitoring thread ...\n");

	while (! thread_stop)
	{
		pthread_mutex_lock (& data_lock);

		count = glist_length (& monitoring);

		for (i = 0; i < count; i ++)
		{
			p = (PROCESS *) glist_get (& monitoring, i);

			if (is_process_zombi (p->pid) == 1)
			{
				DM ("waitpid: waiting for zombi child (%d:%s:%c) finish ...\n", p->pid, p->key, get_pid_stat (p->pid));
				waitpid (p->pid, NULL, 0);
				DM ("waitpid: zombi finished.\n");
				p->pid = -1;
			}

			if (! is_process_alive (p->pid))
			{
				if (p->count == 0)
					p->pid = -1;
				else
				{
					DM ("launch command key [%s] ...\n", p->key);
					p->pid = run_command (p->cmd);
					if (p->count > 0) p->count --;
				}
			}
		}

		pthread_mutex_unlock (& data_lock);

		/* set next timeout */
		clock_gettime (CLOCK_REALTIME, & ts);
		ts.tv_sec += MONITOR_INTERVAL;

		pthread_cond_timedwait (& cond, & time_lock, & ts);
	}

	DM ("stop process minitoring thread ...\n");
	return NULL;
}

/* service entry */
int proctrl_main (int server_socket)
{
	GLIST_NEW (cmd);

	pthread_t thread_monitor = (pthread_t) -1;

	char buffer [PATH_MAX + 16];

	int ret = 0;
	int done = 0;
	int commfd = -1;

	pthread_mutex_lock (& time_lock);

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
				done = 1;
				break;
			}

			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, CMD_ISALIVE))
			{
				MAKE_DATA (buffer, CMD_ISALIVE);
				if (! buffer [0])
				{
					ret = -1;
				}
				else
				{
					ret = atoi (buffer);
					ret = is_process_alive (ret);
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_ADDARG))
			{
				MAKE_DATA (buffer, CMD_ADDARG);
				if (buffer [0])
				{
					ret = glist_append (& cmd, strdup (buffer));
				}
				else
				{
					ret = glist_clear (& cmd, free);
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_RUN))
			{
				if (! cmd)
				{
					ret = -1;
				}
				else
				{
					ret = run_command (cmd);

					DM ("run command pid = %d\n", ret);

					if (ret >= 0)
					{
						int status;
						int pid = ret;

						do
						{
							DM ("waitpid: waiting for child (%d) ...\n", pid);

							if (waitpid (pid, & status, 0) < 0)
							{
								DM ("waitpid failed: %s\n", strerror (errno));
								ret = -1;
								break;
							}

							DM ("waitpid: status=0x%08x\n", status);

							ret = WEXITSTATUS (status);
						}
						while (! WIFEXITED (status));

						glist_clear (& cmd, free);
					}
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_RUN_SERVICE) || CMP_CMD (buffer, CMD_RUN_SERVICE_ONE))
			{
				int once = CMP_CMD (buffer, CMD_RUN_SERVICE_ONE);

				if (once)
				{
					MAKE_DATA (buffer, CMD_RUN_SERVICE_ONE);
				}
				else
				{
					MAKE_DATA (buffer, CMD_RUN_SERVICE);
				}

				if (! cmd)
				{
					ret = -1;
				}
				else
				{
					pthread_mutex_lock (& data_lock);
					if (monitor_add (buffer [0] ? buffer : NULL, once ? 1 : -1, cmd) < 0)
					{
						glist_clear (& cmd, free);
					}
					cmd = NULL;
					pthread_mutex_unlock (& data_lock);

					if (monitoring && (thread_monitor == (pthread_t) -1) &&
						(pthread_create (& thread_monitor, NULL, thread_monitor_impl, NULL) < 0))
					{
						DM ("pthread_create thread_monitor failed: %s\n", strerror (errno));
						thread_monitor = (pthread_t) -1;
						ret = -1;
					}
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_STOP_SERVICE))
			{
				MAKE_DATA (buffer, CMD_STOP_SERVICE);
				if (! buffer [0])
				{
					ret = -1;
				}
				else
				{
					pthread_mutex_lock (& data_lock);
					monitor_remove (buffer);
					pthread_mutex_unlock (& data_lock);

					if ((! monitoring) && (thread_monitor != (pthread_t) -1))
					{
						thread_stop = 1;
						pthread_cond_signal (& cond);
						pthread_join (thread_monitor, NULL);
						thread_monitor = (pthread_t) -1;
						thread_stop = 0;
					}
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_CHECK_SERVICE))
			{
				MAKE_DATA (buffer, CMD_CHECK_SERVICE);
				if (! buffer [0])
				{
					ret = -1;
				}
				else
				{
					pthread_mutex_lock (& data_lock);
					ret = (monitor_find (buffer) < 0) ? 0 : 1;
					pthread_mutex_unlock (& data_lock);
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_KILL))
			{
				MAKE_DATA (buffer, CMD_KILL);
				if (! buffer [0])
				{
					ret = -1;
				}
				else
				{
					ret = atoi (buffer);
					if (is_process_alive (ret))
					{
						kill (ret, SIGTERM);
						ret = 0;
					}
					else
					{
						ret = -1;
					}
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

	thread_stop = 1;

	pthread_mutex_unlock (& time_lock);

	if (thread_monitor != (pthread_t) -1)
	{
		pthread_join (thread_monitor, NULL);
		thread_monitor = (pthread_t) -1;
	}

	monitor_clear ();

	thread_stop = 0;

	return ret;
}
