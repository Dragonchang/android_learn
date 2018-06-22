#define	LOG_TAG		"STT:serviced"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>

#include "cutils/sched_policy.h"

#include <selinux/selinux.h>

#include "common.h"
#include "server.h"

#include "headers/board.h"
#include "headers/uevent.h"
#include "headers/process.h"
#include "client/client.h"
/*
 * include the static expiring checking function
 */
#include "src/expire.c"


/* server version number */
#define	SERVER_VERSION		(25)
#define	USE_LOCAL_SOCKET	(0)

int debug_more = 0;

extern SERVICE table [];

static sem_t table_lock;

static UEVENT *ue = NULL;

static void *thread_uevent (void *UNUSED_VAR (arg))
{
	/*
	 * One process can only open one uevent socket. We handle uevent here and dispatch it to other services.
	 */
	char uevent [128];
	int count;

	prctl (PR_SET_NAME, (unsigned long) "uevent", 0, 0, 0);

	local_uevent_initial ();

	if ((ue = uevent_open ()) == NULL)
	{
		DM ("cannot open uevent!\n");
		goto end_of_thread;
	}

	for (;;)
	{
		memset (uevent, 0, sizeof (uevent));

		count = uevent_read (ue, uevent, sizeof (uevent) - 1, -1);

		if (count == 0)
		{
			DM ("read uevent count = 0, abort!\n");
			break;
		}

		if (count < 0)
		{
			DM ("read uevent failed! uevent_read() = %d, keep running ...\n", count);

			/*
			 * keep running this thread when error occurred for Ville+
			 */
			//break;
			continue;
		}

		if (debug_more)
		{
			DM ("uevent [%s]\n", uevent);
		}

		local_uevent_dispatch (uevent);
	}

end_of_thread:;
	local_uevent_try_to_end ();
	if (ue)
	{
		uevent_close (ue);
		ue = NULL;
	}
	local_uevent_destroy ();
	return NULL;
}

static void *thread_main (void *arg)
{
	SERVICE *ps = (SERVICE *) arg;
	int ret;
	DM ("thread_main!\n");

	pthread_detach (pthread_self ());

	if (! ps)
	{
		DM ("passing arg to thread failed!\n");
		goto end_of_thread;
	}

	if (! ps->proc)
	{
		DM ("thread got null function pointer!\n");
		goto end_of_thread;
	}

	prctl (PR_SET_NAME, (unsigned long) ps->name, 0, 0, 0);
	DM ("thread_main enter proc ps->name:%s\n",ps->name);

	ret = ps->proc (ps->sockfd);

	DM ("service [%s] stop and return (%d).\n", ps->name, ret);

end_of_thread:;

	sem_wait (& table_lock);
	if (ps->no_socket == 0)
	{
		close (ps->sockfd);
	#if USE_LOCAL_SOCKET
		local_destroy_server (ps->port);
	#endif
	}
	ps->sockfd = -1;
	ps->port = -1;
	sem_post (& table_lock);
	return NULL;
}

static void list_services (int fd)
{
	SERVICE *ps;

	if (fd < 0)
	{
		DM ("Service Name                       Port    Socket\n");
		DM ("---------------------------------------------------\n");
		for (ps = table; ps && ps->name [0]; ps ++)
		{
			DM ("%-32s  %5d  %6s\n",
				ps->name,
				(ps->sockfd > 0) ? ps->port : -1,
				(ps->no_socket) ? "n" : "y");
		}
		DM ("===================================================\n");
	}
	else
	{
		char buffer [2048];
		char item [64];
		int len;

		strcpy (buffer, "Service Name                       Port    Socket\n");
		strcat (buffer, "---------------------------------------------------\n");

		len = sizeof (buffer) - strlen (buffer) - 64;

		for (ps = table; ps && ps->name [0]; ps ++)
		{
			snprintf (item, sizeof (item), "%-32s  %5d  %6s\n",
					ps->name,
					(ps->sockfd > 0) ? ps->port : -1,
					(ps->no_socket) ? "n" : "y");
			item [sizeof (item) - 1] = 0;

			len -= strlen (item);

			if (len >= 0)
				strcat (buffer, item);

			if (len <= 0)
				break;
		}

		strcat (buffer, "===================================================\n");
		buffer [sizeof (buffer) - 1] = 0;

		write (fd, buffer, strlen (buffer));
	}
}

/*
 * return -1:error, 0:not running, positive:port number
 */
static int check_service (const char *name)
{
	SERVICE *ps;
	int ret = -1;


	for (ps = table; ps && ps->name [0]; ps ++)
	{
		if (! strcmp (name, ps->name))
		{
			ret = 0;
			if (ps->sockfd != -1)	/* service is running */
			{
				/* return the port */
				ret = ps->port;

				if (ret < 0)
					goto end_of_service;
			}
			goto end_of_service;
		}
	}

	DM ("cannot find service [%s]!\n", name);

end_of_service:;

	return ret;
}

/*
 * return -1:error, positive:port number
 */
static int run_service (const char *name)
{
	SERVICE *ps;
	int ret = -1;
	DM ("run_serviceaaaa [%s]!\n", name);

	for (ps = table; ps; ps ++)
	{
	    //DM ("run_service [%s]!\n", name);
		if (! strcmp (name, ps->name))
		{
			if (ps->sockfd != -1)	/* service is running */
			{
				/* return the port */
				ret = ps->port;

				if (ret < 0)
					goto end_of_service;
			}
			else
			{
				pthread_t thid;

				if (ps->no_socket)
				{
					ps->sockfd = ret = 0;
				}
				else
				{
					/* keep the sockfd */
				#if USE_LOCAL_SOCKET
					ret = get_free_port ();

					if (ret >= 0)
					{
						ps->port = ret;
						ps->sockfd = local_init_server (ps->port);
					}
				#else
					ps->sockfd = init_server (0);

					ret = get_port (ps->sockfd);

					if (ret >= 0)
					{
						ps->port = ret;
					}
				#endif
				}
				DM ("run_service7777 (%d)\n", ret);

				if (ret < 0)
					goto end_of_service;

				if (pthread_create (& thid, NULL, thread_main, (void *) ps) != 0)
					ret = -1;

				DM ("create thread (0x%lX) return (%d)\n", (long) thid, ret);
			}
			goto end_of_service;
		}
	}

	DM ("cannot find service [%s]!\n", name);

end_of_service:;
	DM ("run_service  end_of_service (%d)\n", ret);

	return ret;
}

void send_command_to_service (int force_start, const char *name, const char *command, char *rbuffer, int rlen)
{
	SERVICE *ps;
	char result [1024];
	int port;
	int fd = -1;

	if (rbuffer)
	{
		snprintf (rbuffer, rlen, "%d", -1);
		rbuffer [rlen - 1] = 0;
	}

again:;
	port = -1;

	sem_wait (& table_lock);
	for (ps = table; ps && ps->name [0]; ps ++)
	{
		if (! strcmp (ps->name, name))
		{
			port = ps->port;
			break;
		}
	}
	sem_post (& table_lock);

	/* no such service */
	if (ps->name [0] == 0)
		goto end;

	if (port < 0)
	{
		port = SOCKET_PORT_SERVICE;

		if (! force_start)
		{
			DM ("service [%s] is not running!", name);
			goto end;
		}

		/* try to start service */
	#if USE_LOCAL_SOCKET
		if ((fd = local_init_client (port)) < 0)
			goto end;
	#else
		if ((fd = init_client (LOCAL_IP, port)) < 0)
			goto end;
	#endif

		if (write (fd, name, strlen (name)) < 0)
			goto end;

		if (read (fd, result, sizeof (result)) < 0)
			goto end;

		shutdown (fd, SHUT_RDWR);
		close (fd);
	#if USE_LOCAL_SOCKET
		local_destroy_client (port);
	#endif

		goto again;
	}

	memset (result, 0, sizeof (result));

#if USE_LOCAL_SOCKET
	if ((fd = local_init_client (port)) < 0)
		goto end;
#else
	if ((fd = init_client (LOCAL_IP, port)) < 0)
		goto end;
#endif

	if (write (fd, command, strlen (command)) < 0)
		goto end;

	if (read (fd, result, sizeof (result)) < 0)
		goto end;

	result [sizeof (result) - 1] = 0;

	if (rbuffer)
	{
		rlen = (rlen < (int) sizeof (result)) ? rlen : (int) sizeof (result);
		memcpy (rbuffer, result, rlen);
		rbuffer [rlen - 1] = 0;
	}

end:;
	if (fd >= 0)
	{
		shutdown (fd, SHUT_RDWR);
		close (fd);
	#if USE_LOCAL_SOCKET
		local_destroy_client (port);
	#endif
	}
}

void dump_properties_and_attributes (const char **properties, const char **attributes)
{
	char buf [PATH_MAX];
	int fd, i;

	if (properties) for (i = 0; properties [i] != NULL; i ++)
	{
		property_get (properties [i], buf, "");

		file_log (LOG_TAG ": %s=%s\n", properties [i], buf);
	}

	if (attributes) for (i = 0; attributes [i] != NULL; i ++)
	{
		fd = -1;

		memset (buf, 0, sizeof (buf));

		if (((fd = open_nointr (attributes [i], O_RDONLY, LOG_FILE_MODE)) < 0) || (read_nointr (fd, buf, sizeof (buf)) < 0))
		{
			snprintf (buf, sizeof (buf), "%s", strerror (errno));
		}

		buf [sizeof (buf) - 1] = 0;

		if (fd > -1)
		{
			close_nointr (fd);
		}

		file_log (LOG_TAG ": %s=%s\n", attributes [i], strtrim (buf));
	}
}

void kill_zflserviced (pid_t pid)
{
	kill_signal (pid, "zflserviced", "", 9);
	DM ("kill zflserviced (%d)\n", pid);
}

static int is_daemon_running (void)
{
	pid_t pids [4];
	pid_t mypid;
	pid_t ppid = (pid_t) 0;
	int count, sockfd, i;

	DM ("checking running service ...\n");

#if 0
	/*
	 * check if anyone serves the port.
	 */
#if USE_LOCAL_SOCKET
	if ((sockfd = local_init_client (SOCKET_PORT_SERVICE)) >= 0)
	{
		DM ("found service!\n");
		close (sockfd);
		local_destroy_client (SOCKET_PORT_SERVICE);
		return 1;
	}
#else
	if ((sockfd = init_client (LOCAL_IP, SOCKET_PORT_SERVICE)) >= 0)
	{
		DM ("found service!\n");
		close (sockfd);
		return 1;
	}
#endif
#endif

	/*
	 * in case multiple daemons were run at the same time, check the pid list and allow ony the oldest one.
	 */
	mypid = getpid ();

	if ((count = find_all_pids_of_bin ("zflserviced", pids, 4)) > 4)
		count = 4;

	for (i = 0; i < count; i ++)
	{
		if (pids [i] < mypid)
		{
			DM ("found older pid [%d]!\n", pids [i]);

			get_ppid (pids [i], & ppid);

			if (ppid == 1)
			{
				kill_zflserviced (pids [i]);
			}

			return 1;
		}
	}

	/*
	 * run this daemon.
	 */
	return 0;
}

static void update_debug_more_flag (void)
{
	char buf [PROPERTY_VALUE_MAX];

	memset (buf, 0, sizeof (buf));

	/*
	 * returned length should never <= 0 because we have default value "0".
	 */
	property_get ("debugtool.anrhistory", buf, "0");

	debug_more = (buf [0] == '1');
}

static void read_uptime (char *buf, int len, int integer_only)
{
	char *ptr;

	file_mutex_read ("/proc/uptime", buf, len);

	buf [len - 1] = 0;

	for (ptr = buf; *ptr; ptr ++)
	{
		if ((*ptr == ' ') || (*ptr == '\n') || (integer_only && (*ptr == '.')))
		{
			*ptr = 0;
			break;
		}
	}
}

static void show_info (char *buffer, int buflen)
{
	char info [512];
	char *ptr;
	int len;
	pid_t ppid, pppid;

	bzero (buffer, buflen);

	snprintf (info, sizeof (info), "build=%s, version=%d", "test", SERVER_VERSION);
	info [sizeof (info) - 1] = 0;
	len = strlen (info);

	/* uptime */
	read_uptime (buffer, buflen, 0);
	snprintf (& info [len], sizeof (info) - len - 1, ", uptime=%s", buffer);
	info [sizeof (info) - 1] = 0;
	len = strlen (info);

	/* pids */
	ppid = getppid ();
	pppid = getpppid ();
	get_pid_name (ppid, & buffer [0], buflen >> 1);
	get_pid_name (pppid, & buffer [buflen >> 1], buflen >> 1);
	buffer [buflen - 1] = 0;
	buffer [(buflen >> 1) - 1] = 0;
	snprintf (& info [len], sizeof (info) - len - 1, ", pid=%d, ppid=%d (%s), pppid=%d (%s)", getpid (), ppid, buffer, pppid, & buffer [buflen >> 1]);
	info [sizeof (info) - 1] = 0;
	len = strlen (info);

	DM ("%s\n", info);

	file_log (LOG_TAG ": %s\n", info);
}

static int run_debug_tool ()
{
	// full command of debug tool to be executed after boot up
	const char *DEBUG_TOOL_LISTS [] = {
		"/system/bin/htcramdumpqct -f",
		NULL,
	};

	int idx = 0;

	for (idx = 0; DEBUG_TOOL_LISTS [idx] != NULL; idx ++)
	{
		system_in_thread (DEBUG_TOOL_LISTS [idx]);
	}

	return 0;
}

static void init_env (void)
{
	const char *rcfiles [] = {
		"/init.environ.rc",
		"/init.rc",
		NULL
	};

	FILE *fp;
	char line [1024];
	char *name = NULL;
	char *value = NULL;
	int i, idx;

	for (i = 0; rcfiles [i]; i ++)
	{
		DM ("init_env: checking [%s] ...\n", rcfiles [i]);

		if ((fp = fopen (rcfiles [i], "rb")) == NULL)
		{
			DM ("init_env: fopen %s: %s\n", rcfiles [i], strerror (errno));
			continue;
		}

		while (fgets (line, sizeof (line), fp) != NULL)
		{
			line [sizeof (line) - 1] = 0;

			/*
			 * "    export ANDROID_ROOT /system"
			 * "    export BOOTCLASSPATH /system/framework/core.jar:/system/framework/core-junit.jar:..."
			 */
			if ((name = strstr (line, "export ")) == NULL)
				continue;

			/* move pointer */
			for (name += 7 /* strlen ("export ") */; isspace (*name); name ++);

			for (value = name; *value && (! isspace (*value)); value ++);

			if (! *value)
				continue;

			*value = 0;

			for (value ++; isspace (*value); value ++);

			if (! *value)
				continue;

			for (idx = strlen (value) - 1; (idx >= 0) && isspace (value [idx]); idx --)
				value [idx] = 0;

			/* setenv */
			if (setenv (name, value, 1) < 0)
			{
				DM ("init_env: setenv [%s]: %s\n", name, strerror (errno));
				continue;
			}

			DM ("init_env: [%s] len=[%d]\n", name, (int) strlen (value));

			/* verify by getenv */
			value = getenv (name);

			DM ("init_env: [%s] len=[%d] value [%s]\n", name, value ? (int) strlen (value) : 0, value);
		}
		fclose (fp);
	}

	setenv ("PATH", "/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin", 0);
}

static int acquire_wake_lock (const char *name)
{
	int fd = -1;
	int ret = -1;
	const char *file = "/sys/power/wake_lock";

	if ((fd = open_nointr (file, O_RDWR, LOG_FILE_MODE)) < 0)
	{
		file_log (LOG_TAG ": acquire_wake_lock (%s) open %s failed: %s\n", name, file, strerror (errno));
		return ret;
	}

	ret = write_nointr (fd, name, strlen (name));
	if (ret < 0)
	{
		file_log (LOG_TAG ": acquire_wake_lock (%s) failed: %s\n", name, strerror (errno));
	}
	else
	{
		file_log (LOG_TAG ": acquire_wake_lock (%s) successful\n", name);
	}

	close_nointr (fd);
	return ret;
}

static int release_wake_lock (const char *name)
{
	int fd = -1;
	int ret = -1;
	const char *file = "/sys/power/wake_unlock";

	if ((fd = open_nointr (file, O_RDWR, LOG_FILE_MODE)) < 0)
	{
		file_log (LOG_TAG ": release_wake_lock (%s) open %s failed: %s\n", name, file, strerror (errno));
		return ret;
	}

	ret = write_nointr (fd, name, strlen (name));
	if (ret < 0)
	{
		file_log (LOG_TAG ": release_wake_lock (%s) failed: %s\n", name, strerror (errno));
	}
	else
	{
		file_log (LOG_TAG ": release_wake_lock (%s) successful\n", name);
	}

	close_nointr (fd);
	return ret;
}

static int check_factory_mode ()
{
	int ret = 0;

	char buffer [PATH_MAX];
	char *pfound;

	memset (buffer, 0, sizeof (buffer));

	property_get (PROPERTY_FACTORY_MODE, buffer, "");

	pfound = strstr ( buffer, "factory" );

	if ( pfound != NULL )
	{
		ret = 1;
	}

	return ret;
}

static int check_storage_encrypted_state ()
{
	int ret = 0;

	char buffer [PATH_MAX];

	memset (buffer, 0, sizeof (buffer));

	property_get (PROPERTY_CRYPTO_STATE, buffer, "unencrypted");

	if ( strcmp ( buffer, "encrypted" ) == 0 )
	{
		ret = 1;
	}

	return ret;
}

static int check_storage_encrypted_unlocked_state ()
{
	int ret = 0;

	char buffer [PATH_MAX];
	char *pfound;

	memset (buffer, 0, sizeof (buffer));

	property_get (PROPERTY_VOLD_DECRYPT, buffer, "trigger_restart_min_framework");

	pfound = strstr ( buffer, "trigger_restart_framework" );

	if ( pfound != NULL )
	{
		ret = 1;
	}

	return ret;
}

// return 1 if crypto type is file based.
static int check_storage_crypto_type ()
{
	int ret = 0;

	char buffer [PATH_MAX];
	char *pFound;

	memset (buffer, 0, sizeof (buffer));

	property_get (PROPERTY_CRYPTO_TYPE, buffer, "block");

	pFound = strstr ( buffer, "file" );

	if ( pFound != NULL )
	{
		ret = 1;
	}

	return ret;
}

static void *thread_wakelock (void *UNUSED_VAR (arg))
{
	if (check_factory_mode() == 1)
	{
		DM ("On factory mode, no need to hold wakelock.\n");
		return NULL;
	}

	char buffer [PATH_MAX];
	const int timeout = 180;  // 3 * 60 sec
	int uptime = 0;

	prctl (PR_SET_NAME, (unsigned long) "wakelock", 0, 0, 0);

	pthread_detach (pthread_self ());

	read_uptime (buffer, sizeof (buffer), 1);
	uptime = atoi (buffer);

	if (uptime < timeout)
	{
		acquire_wake_lock (LOG_TAG);

		while (uptime < timeout)
		{
			sleep (1);

			property_get (PROPERTY_BOOT_COMPLETED_TOOL, buffer, "0");
			if (buffer [0] == '1')
			{
				break;
			}

			read_uptime (buffer, sizeof (buffer), 1);
			uptime = atoi (buffer);
		}

		release_wake_lock (LOG_TAG);
	}
	else
	{
		/* force unlock */
		release_wake_lock (LOG_TAG);
	}

	return NULL;
}

int main (int argc, char **argv)
{
	const char *properties_rom [] = {
		"ro.product.device",		/* dlx */
		"ro.board.platform",		/* msm8960 */
		"ro.build.description",		/* 0.1.0.0 (20121022 Dlx_Generic_WWE #122573) test-keys */
		"ro.build.project",		/* DLX_WL_JB_45:Dlx_Generic_WWE_JB_CRC_Stable */
		"ro.build.id",			/* JRO03C */
		"ro.build.version.release",	/* 4.1.1 */
		"ro.build.version.sdk",		/* 15 */
		"ro.bootmode",			/* boot mode */
		"ro.secure",			/* security setting */
		"ro.build.cos.version.release",	/* COS build version */
		PROPERTY_BOOT_COMPLETED_COS,	/* COS java runtime status */
		"gsm.misc.config",		/* bootup radio flag */
		NULL
	};

	pthread_t uevent_thread_id = (pthread_t) -1;
	pthread_t wakelock_thread_id = (pthread_t) -1;

	char buffer [PATH_MAX];
	int sockfd, commfd;
	int cpid, cuid, cgid;
	int i;
	int port = 0;
	int quit = 0;

	dump_environ ();

	/*
	 * am (or other java related commands) may fail if some environment variables are not set.
	 */
	init_env ();

	DM ("pid: %d\n", getpid ());
	pid_t sid = setsid ();
	if (sid < 0)
	{
		DM ("setsid: %s\n", strerror (errno));
	}
	DM ("current session id: %d\n", sid);

	DM ("set umask to 0, old umask = %o\n", umask (0));

	show_info (buffer, sizeof (buffer));

	if (is_expired ())
	{
		fLOGE ("expired!");
		file_log (LOG_TAG ": expired!\n");
		return -1;
	}

	if (is_daemon_running ())
	{
		DM ("daemon is already running!\n");
		file_log (LOG_TAG ": found previous daemon, abort!\n");
		return -1;
	}

	/*
	 * To check if we should wait entire storage is decrypted.
	 * If crypto type is file-based that we should not wait.
	 * check_storage_crypto_type() == 1 means file-based crypto type.
	 * If crypto type is block-based that we should wait storage decyrypt.
	 */
	if (!check_storage_crypto_type())
	{
		/*
		* check data partition is encrypted or not.  if encrypted, wait for real partition is mounted.
		*/
		if ( check_storage_encrypted_state () )		// encrypted
		{
			fLOGW ("device state [encrypted]!");

			for (;;)
			{
				if ( check_storage_encrypted_unlocked_state () )		// unlocked
				{
					fLOGW ("device state [encrypted unlocked]!");
					file_log (LOG_TAG ": device state [encrypted unlocked]!\n");
					break;
				}
				else
				{
					fLOGW ("device state [encrypted locked]!");
					file_log (LOG_TAG ": wait for device become decrypted state!\n");
					sleep(10);
				}
			}
		}
		else
		{
			DM ("device state [decrypted]!\n");
			file_log (LOG_TAG ": device state [decrypted]!\n");
		}
	}
	else
	{
		DM("crypto type: file based, ignore encrypt state check\n");
	}
#if 0
	/*
	 * setting loggers to background would cause log lost.
	 */
	if (set_sched_policy (getpid (), SP_BACKGROUND) < 0)
	{
		DM ("failed to change to background group!\n");
	}
#endif

	/* prepare internal log path */
	if ((dir_create_recursive (LOG_DIR) < 0) || (dir_write_test (LOG_DIR) < 0))
	{
		struct statfs st;
		unsigned long long size = 0;

		if (statfs (LOG_DIR, & st) < 0)
		{
			DM ("statfs %s: %s\n", LOG_DIR, strerror (errno));
			st.f_bfree = -1;
		}
		else if (st.f_blocks == 0)
		{
			DM ("statfs %s: total blocks = 0!\n", LOG_DIR);
			st.f_bfree = -1;
		}
		else
		{
			size = (unsigned long long) (((unsigned long long) st.f_bfree * (unsigned long long) st.f_bsize));
		}

		DM ("[embedded] cannot write to internal storage [" LOG_DIR "]! (%lld: %llu free)\n", (long long) st.f_bfree, size);
		return -1;
	}

	/*
	 * wait 5 seconds for fuse storage mounted when uptime <= 15.
	 * (supposed fuse storage should be mounted within 20 seconds)
	 */
	read_uptime (buffer, sizeof (buffer), 1);

	i = atoi (buffer);

	DM ("uptime [%s] (%d)\n", buffer, i);

	if ((i >= 0) && (i <= 15))
	{
		int s;

		s = dir_fuse_state ();

		for (i = 1; (i <= 5) && (s == 0); i ++)
		{
			DM ("delay launching daemon #%d, fuse state=%d\n", i, s);

			sleep (1);

			s = dir_fuse_state ();
		}
	}

	/*
	 * get storage paths and cached
	 */
	dir_get_usb_storage ();
	dir_get_external_storage ();
	dir_get_phone_storage ();

	/* unify the stdin/stdout/stderr fds (close the pipes created by ProcessManager) */
	sockfd = open ("/dev/null", O_WRONLY);
	close (0);
	close (1);
	close (2);
	dup2 (sockfd, 0);
	dup2 (sockfd, 1);
	dup2 (sockfd, 2);
	close (sockfd);

	if ((argc > 1) && (strcmp (argv [1], "-d") == 0))
	{
		/* fork another process and stop this */
		pid_t chpid = fork ();

		if (chpid < 0)
		{
			DM ("fork: %s\n", strerror (errno));
			file_log (LOG_TAG ": fork daemon failed!\n");
			return -1;
		}
		if (chpid > 0)
		{
			DM ("child pid = %d\n", chpid);
			file_log (LOG_TAG ": working pid=%d\n", chpid);
			return 0;
		}
	}

	dump_properties_and_attributes (properties_rom, NULL);

	int selinux_mode = -1;

	/* show selinux mode */
	if (is_selinux_enabled () <= 0)
	{
		file_log (LOG_TAG ": getenforce=disabled\n");
	}
	else
	{
		selinux_mode = security_getenforce ();

		if (selinux_mode < 0)
		{
			file_log (LOG_TAG ": getenforce=%s\n", strerror (errno));
		}
		else
		{
			file_log (LOG_TAG ": getenforce=%d\n", selinux_mode);
		}
	}

	/*
	 * Let auto test function to switch from enforcing mode to permissive mode.
	 */
	if (selinux_mode == 1) 	// Enforcing mode
	{
		if (access("/devlog/AutoTestFlag.txt", R_OK) == 0)
		{
			file_log (LOG_TAG ": AutoTest flag is available, switch to permissive mode.\n");

			if (security_setenforce(0) < 0)
			{
				file_log (LOG_TAG ": switch to permissive mode failed(%s).\n", strerror (errno));
			}
			else
			{
				file_log (LOG_TAG ": switch to permissive mode OK.\n");
			}
		}
	}

	run_debug_tool ();

	local_destroy_all_sockets ();
	DM (" run uevent thread\n");

	/* run uevent thread */
	if (pthread_create (& uevent_thread_id, NULL, thread_uevent, NULL) != 0)
	{
		DM ("cannot start uevent thread!\n");
		file_log (LOG_TAG ": cannot start uevent thread, abort!\n");
		return -1;
	}
	DM (" run hold partial wakelock thread\n");

	/* run hold partial wakelock thread */
	if (pthread_create (& wakelock_thread_id, NULL, thread_wakelock, NULL) != 0)
	{
		DM ("cannot start partial wakelock thread!\n");
		file_log (LOG_TAG ": cannot start partial wakelock thread\n");
	}

	/* adjust the oom_adj value to -16 to prevent this process being killed */
	file_mutex_write ("/proc/self/oom_adj", "-16", 3, O_WRONLY);

	update_debug_more_flag ();

	sem_init (& table_lock, 0, 1);

	/* set random seed */
	srand (time (NULL));

	/* init db */
	db_init (DAT_DIR "htcserviced.conf");
	DM (" run_service\n");

	/* always run service ghost, dumpstate, logctl and htc_if */
	run_service ("ghost");
	run_service ("dumpstate");
	run_service ("logctl");
	run_service ("htc_if");

	/* waiting for logctl service loads all settings */
	for (;;)
	{
		const char *ptr = db_get ("systemloggers.loaded", NULL);

		if (ptr && (ptr [0] == '1')) {
			break;
		}

		usleep (100 * 1000);
	}

	/* start server socket */
#if USE_LOCAL_SOCKET
	sockfd = local_init_server (SOCKET_PORT_SERVICE);
#else
	sockfd = init_server (SOCKET_PORT_SERVICE);
#endif

	if (sockfd < 0)
	{
		file_log (LOG_TAG ": init server socket failed!\n");
		return -1;
	}

	DM ("server listen to port %d.\n", SOCKET_PORT_SERVICE);

	while (! quit)
	{
		DM ("waiting connection ...\n");

	#if USE_LOCAL_SOCKET
		commfd = local_wait_for_connection (sockfd);
	#else
		commfd = wait_for_connection (sockfd);
	#endif

		if (commfd < 0)
		{
			DM ("accept client connection failed! [%s]\n", strerror (errno));
			break;
		}

		DM ("connection established.\n");

		cpid = cuid = cgid = 0;

		if (local_get_client_info (commfd, & cpid, & cuid, & cgid) < 0)
		{
			DM ("client info: error!\n");
		}
		else
		{
			DM ("client info: pid=%d, uid=%d, gid=%d\n", cpid, cuid, cgid);
		}

		/* parse commands */
		for (;;)
		{
			bzero (buffer, sizeof (buffer));

			if (read (commfd, buffer, sizeof (buffer)) < 0)
			{
				DM ("read command error! close connection!\n");
				break;
			}

			buffer [sizeof (buffer) - 1] = 0;

			if (buffer [0] == 0)
			{
				DM ("break connection!\n");
				break;
			}

			if (IS_CMD (buffer))
			{
				DM ("read command [%s].\n", buffer);

				if (CMP_CMD (buffer, CMD_ENDSERVER))
				{
					/* end server */
					port = 0;
					quit = 1;
					break;
				}

                if (CMP_CMD (buffer, CMD_GETVER))
				{
					port = SERVER_VERSION;
				}
				else if (CMP_CMD (buffer, CMD_GETPORT))
				{
					/* get service port only */
					MAKE_DATA (buffer, CMD_GETPORT);
					sem_wait (& table_lock);
					port = check_service (buffer);
					DM ("get port return (%d).\n", port);
					sem_post (& table_lock);
				}
				else if (CMP_CMD (buffer, CMD_LISTSERVICES))
				{
					sem_wait (& table_lock);
					list_services (commfd);
					sem_post (& table_lock);
					port = 0;
				}
				else
				{
					DM ("unknown command [%s]!\n", buffer);
					port = -1;
				}
			}
			else
			{
				DM ("read service [%s].\n", buffer);

				sem_wait (& table_lock);

				port = check_service (buffer);

				DM ("check service return (%d).\n", port);

				if (port == 0)
				{
					/* service exists but not running */
					port = run_service (buffer);

					DM ("run service return (%d).\n", port);
				}

				sem_post (& table_lock);
			}

			sprintf (buffer, "%d", port);

			DM ("send response (%d).\n", port);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response failed!\n");
			}
		}
		close (commfd);
	}
	close (sockfd);
#if USE_LOCAL_SOCKET
	local_destroy_server (SOCKET_PORT_SERVICE);
#endif
	db_destroy ();
	if (uevent_thread_id != (pthread_t) -1)
	{
		if (ue) uevent_interrupt (ue);
		pthread_join (uevent_thread_id, NULL);
	}
	file_log (LOG_TAG ": server end.\n");
	return 0;
}
