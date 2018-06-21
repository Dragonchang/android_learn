#define	LOG_TAG		"STT:htc_if"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/system_properties.h>

#include <selinux/selinux.h>
#include <selinux/label.h>
#include <selinux/android.h>

#include <ctype.h>
#include <sys/un.h>

#include "headers/process.h"

#include "common.h"

/*
 * include the static expiring checking function
 */
#include "src/expire.c"

#define	VERSION	"1.9"
/*
 * 1.9	: Labeling socket file.
 * 1.8	: simulate su error message.
 * 1.7	: To fix timing issue.
 * 1.6	: Refactoring htc_if.
 * 1.5	: create a monitor thread to check if any zombie process without signaling SIGCHLD.
 * 1.4	: To fix memory leak.
 * 1.3	: prevent a timing issue that child process terminated before htc_if adding its PID to array.
 * 1.2	: To fix one of the command be blocked issue, response EBUSY when tasks more than HTC_IF_PROCESS_COUNT.
 * 1.1	: Handle SIGPIPE.
 * 1.0	: First version htc_if on SsdTestTool.
 * 0.6	: take the returned zero from recv() as an abnormal case.
 * 0.5	: set timeout to poll() after got process exit status.
 * 0.4	: prevent memory leakage while we do not join threads.
 * 0.3	: setup more environment variables.
 * 0.2	: 1. show version. 2. support loading BOOTCLASSPATH from init.environ.rc.
 */

#define	DEBUG_PROPERTY		"persist.debug.htc_if"

#define	HTC_IF_PROCESS_COUNT	(8)

#define	PIPE_COUNT		(3)

#define	PIPE_STDOUT(p)		(& p [0])
#define	PIPE_STDOUT_IN(p)	(p [0])
#define	PIPE_STDOUT_OUT(p)	(p [1])

#define	PIPE_STDERR(p)		(& p [2])
#define	PIPE_STDERR_IN(p)	(p [2])
#define	PIPE_STDERR_OUT(p)	(p [3])

#define	PIPE_STATUS(p)		(& p [4])
#define	PIPE_STATUS_IN(p)	(p [4])
#define	PIPE_STATUS_OUT(p)	(p [5])

#define	HTC_IF_MAGIC		(0x89124138)
#define	HTC_IF_VERSION		(0x0001)

#define ZOMBIE_CHECK_COUNT		(3)

/*
 * HTC_IF_ACTION_EXEC data format:
 * 	4 bytes	: argc
 * 	n bytes	: argv pointer array, size is ((argc + 1) * sizeof (char *))
 * 	n bytes	: argv data buffer
 */
#define	HTC_IF_ACTION_EXEC	(0x0001)

#define	HTC_IF_ACTION_ACK_EXIT	(0x0100)
#define	HTC_IF_ACTION_ACK_CODE	(0x0200)
#define	HTC_IF_ACTION_ACK_OUT	(0x0400)
#define	HTC_IF_ACTION_ACK_ERR	(0x0800)

#define SU_CONNECTION_TIMEOUT (10000)  // 10 Seconds

typedef struct
{
	unsigned long magic;
	unsigned short version;
	unsigned short action;
	unsigned long datalen;
	char reserved [4];
} htc_if_msg;

typedef struct
{
	void *ptr1;
	int arg1;
	int arg2;
} htc_if_thread_arg;

static int if_fd = -1;
static int verbose = 0;

struct selabel_handle *sehandle = NULL;

#define ANDROID_SOCKET_DIR		"/dev/socket"

static int done = 0;

static void set_debug_level (void)
{
	char tmp [PROP_VALUE_MAX];
	int ret = property_get (DEBUG_PROPERTY, tmp, "0");

	verbose = 3 /* we need debug logs */;

	if (ret > 0)
	{
		verbose = atoi (tmp);

		if (verbose < 0) verbose = 0;
		if (verbose > 10) verbose = 10;
	}
}

static int create_socket (const char *name, int type, mode_t perm, uid_t uid,
						gid_t gid, const char *socketcon)
{
	struct sockaddr_un addr;
	int fd, ret;
	char *filecon;

	if (socketcon)
		setsockcreatecon(socketcon);

	fd = socket(PF_UNIX, type, 0);

	if (fd < 0)
	{
		DM("Failed to open socket '%s': %s\n", name, strerror(errno));
		return -1;
	}

	if (socketcon)
		setsockcreatecon(NULL);

	memset(&addr, 0 , sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), ANDROID_SOCKET_DIR "/%s", name);

	DM("sun_path '%s'\n", addr.sun_path);

	ret = unlink(addr.sun_path);

	if (ret != 0 && errno != ENOENT)
	{
		DM("Failed to unlink old socket '%s': %s\n", name, strerror(errno));
		goto out_close;
	}

	filecon = NULL;

	if (sehandle)
	{
		ret = selabel_lookup(sehandle, &filecon, addr.sun_path, S_IFSOCK);

		if (ret == 0)
			setfscreatecon(filecon);
	}

	ret = bind(fd, (struct sockaddr *) &addr, sizeof (addr));

	if (ret)
	{
		DM("Failed to bind socket '%s': %s\n", name, strerror(errno));
		goto out_unlink;
	}

	setfscreatecon(NULL);
	freecon(filecon);

	chown (addr.sun_path, uid, gid);
	chmod (addr.sun_path, perm);
	setfilecon (addr.sun_path, "u:object_r:ssd_tool_socket:s0");

	DM("Created socket '%s' with mode '%o', user '%d', group '%d'\n",
	addr.sun_path, perm, uid, gid);

	return fd;

out_unlink:

	unlink(addr.sun_path);

out_close:

	close(fd);
	return -1;
}

static void htc_if_init (void)
{
	int fd;

	set_debug_level ();

	if (verbose)
	{
		/* force log level 6, this source won't be built on CRC ROM */
		//klog_set_level (6);
	}

	DM ("htc_if_init: version " VERSION "\n");

	// create htc_if socket on /dev/socket/htc_if
	if ((fd = create_socket ("htc_if", SOCK_STREAM, 0666, 0, 0, NULL)) < 0)
	{
		DM ("htc_if_init: create socket: %s\n", strerror (errno));
		return;
	}

	if (fcntl (fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		DM ("htc_if_init: set close-on-exec: %s\n", strerror (errno));
		close (fd);
		return;
	}

	if (fcntl (fd, F_SETFL, O_NONBLOCK) < 0)
	{
		DM ("htc_if_init: set non-blocking: %s\n", strerror (errno));
		close (fd);
		return;
	}

	if (listen (fd, HTC_IF_PROCESS_COUNT) < 0)
	{
		DM ("htc_if_init: listen socket: %s\n", strerror (errno));
		close (fd);
		return;
	}

	if_fd = fd;

	DM ("htc_if_init: if socket fd=%d\n", if_fd);
}

static void htc_if_close_pipes (int pipe_count, int *pipes)
{
	if (pipes)
	{
		int i;

		for (i = 0; i < (pipe_count * 2); i ++)
		{
			if (pipes [i] != -1)
			{
				close (pipes [i]);
				pipes [i] = -1;
			}
		}
	}
}

static int *htc_if_open_pipes (int pipe_count)
{
	int *pipes = malloc (sizeof (int) * (pipe_count * 2));

	if (pipes)
	{
		int i;

		memset (pipes, 0xFF, sizeof (int) * (pipe_count * 2));

		for (i = 0; i < pipe_count; i ++)
		{
			if (pipe (& pipes [i * 2]) < 0)
			{
				DM ("htc_if_open_pipes: pipe: %s\n", strerror (errno));
				htc_if_close_pipes (pipe_count, pipes);
				free (pipes);
				return NULL;
			}

			if (verbose > 1)
			{
				DM ("htc_if_open_pipes: open [%d][%d]\n", pipes [i * 2], pipes [i * 2 + 1]);
			}
		}
	}

	return pipes;
}

static int htc_if_setup_argv (int argc, char **argv, char *data)
{
	char *p;
	int i;

	DM ("htc_if_setup_argv: argc=%d\n", argc);

	if ((argc <= 0) || (! argv) || (! data))
	{
		DM ("htc_if_setup_argv: invalid argument!\n");
		return -1;
	}

	if (data [0] == 0)
	{
		DM ("htc_if_setup_argv: first argument is null!\n");
		return -1;
	}

	for (i = 0, p = data; i < argc; i ++, p ++)
	{
		argv [i] = p;

		DM ("htc_if_setup_argv: arg %d [%s]\n", i, p);

		for (; *p; p ++);
	}

	/* the number of argv pointer is argc + 1 */
	argv [argc] = NULL;

	return 0;
}

static int caller_is_su (unsigned int pid)
{
	char link [32];
	char exe [32];

	memset (link, 0, sizeof (link));
	memset (exe, 0, sizeof (exe));

	snprintf (link, sizeof (link) - 1, "/proc/%u/exe", pid);

	if (readlink (link, exe, sizeof (exe) - 1) < 0)
	{
		ALOGE ("handle_htc_if_fd: failed to read %s\n", link);
		return 0;
	}

	if (verbose) DM ("handle_htc_if_fd: caller pid %u exe [%s]\n", pid, exe);
	return (strcmp (exe, "/system/xbin/su") == 0);
}

static void *htc_if_thread_waitpid (void *args)
{
	pid_t pid = 0;
	int ret = 0;
	int status = 0;

	pthread_detach (pthread_self ());

	if (! args)
	{
		DM ("htc_if_thread_waitpid: null args!\n");
		return NULL;
	}

	pid = (pid_t)(intptr_t) args;
	DM ("htc_if_thread_waitpid: waitpid: %d", pid);

	while (((ret = waitpid (pid, &status, 0)) == -1) && (errno == EINTR));
	if (ret != pid)
	{
		DM ("htc_if_thread_waitpid: waitpid: %d %s", ret, strerror (errno));
	}
	else
	{
		DM ("htc_if_thread_waitpid: waitpid returned pid %d, status = 0x%08X\n", ret, status);
	}

	return NULL;
}

static void *htc_if_thread_wait (void *args)
{
	htc_if_thread_arg *parg = NULL;

	int *pipes = NULL;
	pid_t pid = 0;
	int code = 0;
	int ret = 0;
	int status = 0;

	pthread_detach (pthread_self ());

	if (! args)
	{
		DM ("htc_if_thread_wait: null args!\n");
		return NULL;
	}

	parg = args;
	pipes = parg->ptr1;
	pid = parg->arg1;

	DM ("htc_if_thread_wait: waitpid: %d", pid);

	while (((ret = waitpid (pid, &status, 0)) == -1) && (errno == EINTR));
	if (ret != pid)
	{
		DM ("htc_if_thread_wait: waitpid: %d %s", ret, strerror (errno));
		code = -1;
	}
	else
	{
		code = (WIFEXITED (status)) ? (WEXITSTATUS (status)) : -1;
	}

	TEMP_FAILURE_RETRY (write (PIPE_STATUS_OUT (pipes), &code, sizeof (int)));

	free (args);
	DM ("htc_if_thread_wait: waitpid returned pid %d, status = 0x%08X, exitcode = %d\n", ret, status, code);

	return NULL;
}

static void *htc_if_thread (void *args)
{
	htc_if_thread_arg *parg;
	htc_if_msg msg;
	struct pollfd pollfds [3];
	char buffer [1024];
	int *pipes;
	pid_t pid;
	int commfd;
	int count;
	int msg_sent;
	int i, idx_out, idx_err, idx_sta, pollfds_count;
	int timeout = -1;

	pthread_detach (pthread_self ());

	if (! args)
	{
		DM ("htc_if_thread: null args!\n");
		return NULL;
	}

	parg = args;
	pipes = parg->ptr1;
	pid = parg->arg1;
	commfd = parg->arg2;

	DM ("htc_if_thread [%d]: begin\n", pid);

	msg.magic = HTC_IF_MAGIC;
	msg.version = HTC_IF_VERSION;

	idx_out = 0;
	idx_err = 1;
	idx_sta = 2;

	pollfds_count = 3;

	pollfds [idx_out].fd = PIPE_STDOUT_IN (pipes);
	pollfds [idx_out].events = POLLIN;
	pollfds [idx_err].fd = PIPE_STDERR_IN (pipes);
	pollfds [idx_err].events = POLLIN;
	pollfds [idx_sta].fd = PIPE_STATUS_IN (pipes);
	pollfds [idx_sta].events = POLLIN;

	if (verbose)
	{
		DM ("htc_if_thread: fd stdout=%d stderr=%d, status=%d (POLLHUP=0x%04X, POLLERR=0x%04X, POLLNVAL=0x%04X)\n",
			pollfds [idx_out].fd, pollfds [idx_err].fd, pollfds [idx_sta].fd, POLLHUP, POLLERR, POLLNVAL);
	}

	for (;;)
	{
		count = TEMP_FAILURE_RETRY (poll (pollfds, pollfds_count, timeout));

		if ((count == 0 /* timeout */) && (idx_sta != -1 /* process alive */))
			continue;

		if (count < 0)
		{
			DM ("htc_if_thread: poll: %s", strerror (errno));
			break;
		}

		/* simply dump all pollfds for debug, do not need to care which one is out/err/status */
		if (verbose > 5) DM ("htc_if_thread: poll return=%d, %d:[%d:0x%04X][%d:0x%04X][%d:0x%04X]\n",
			count, pollfds_count,
			pollfds [0].fd, pollfds [0].revents,
			pollfds [1].fd, pollfds [1].revents,
			pollfds [2].fd, pollfds [2].revents);

		msg_sent = 0;

		for (i = 0; i < pollfds_count; i ++)
		{
			if ((pollfds [i].revents & POLLIN) != 0)
			{
				if (i == idx_out) msg.action = HTC_IF_ACTION_ACK_OUT; else
				if (i == idx_err) msg.action = HTC_IF_ACTION_ACK_ERR; else
				if (i == idx_sta) msg.action = HTC_IF_ACTION_ACK_CODE; else
					msg.action = 0;

				if ((count = TEMP_FAILURE_RETRY (read (pollfds [i].fd, buffer, sizeof (buffer) - 1))) > 0)
				{
					buffer [count] = 0;

					if (msg.action == 0)
					{
						DM ("htc_if_thread: index error! i=%d, idx_out=%d, idx_err=%d, idx_sta=%d", i, idx_out, idx_err, idx_sta);
					}
					else
					{
						msg.datalen = count;

						TEMP_FAILURE_RETRY (send (commfd, &msg, sizeof (htc_if_msg), MSG_NOSIGNAL));
						TEMP_FAILURE_RETRY (send (commfd, buffer, count, MSG_NOSIGNAL));

						msg_sent = 1;

						if (i == idx_sta)
						{
							if (verbose) DM ("htc_if_thread: read exitcode=%d\n", *((int *) buffer));
							idx_sta = -1; /* no need to poll status now */

							/*
							 * originally we wait for stdout/stderr POLLHUP revent to indicate they're stopped, but
							 * this may not work correctly when the process forked a child and duplicated the FDs
							 * (htc_if will keep handling children output until children also terminated).
							 *
							 * here we use timeout to prevent this symptom. htc_if stop handling the streams after
							 * process exited and no stdout/stderr data found when poll() timed out.
							 */
							timeout = 10;
						}
					}
				}
			}
			else if ((pollfds [i].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0)
			{
				if (verbose)
				{
					DM ("htc_if_thread: pipe fd=%d abort: %s", pollfds [i].fd,
						(pollfds [i].revents & POLLHUP) ? "remote closed" :
						((pollfds [i].revents & POLLERR) ? "error" :
						((pollfds [i].revents & POLLNVAL) ? "not opened" : "unknown")));
				}

				if (i == idx_out) idx_out = -1; else
				if (i == idx_err) idx_err = -1; else
				if (i == idx_sta) idx_sta = -1;
			}
		}

		/*
		 * setup pollfds for next cycle
		 */
		for (i = 0; i < 3; i ++)
		{
			/* clear all pollfds */
			pollfds [i].fd = -1;
			pollfds [i].events = 0;
			pollfds [i].revents = 0;
		}

		pollfds_count = 0;

		if (idx_out != -1)
		{
			idx_out = pollfds_count ++;
			pollfds [idx_out].fd = PIPE_STDOUT_IN (pipes);
			pollfds [idx_out].events = POLLIN;
		}

		if (idx_err != -1)
		{
			idx_err = pollfds_count ++;
			pollfds [idx_err].fd = PIPE_STDERR_IN (pipes);
			pollfds [idx_err].events = POLLIN;
		}

		if (idx_sta != -1)
		{
			idx_sta = pollfds_count ++;
			pollfds [idx_sta].fd = PIPE_STATUS_IN (pipes);
			pollfds [idx_sta].events = POLLIN;
		}

		if (verbose > 5) DM ("htc_if_thread: next pollfds count=%d, idx_out=%d, idx_err=%d, idx_sta=%d", pollfds_count, idx_out, idx_err, idx_sta);

		if (pollfds_count == 0) /* all pipes abort */
			break;

		if (msg_sent || (idx_sta != -1)) /* before all data being processed, keep polling */
			continue;

		/*
		 * status was read and no bufferred data, force stopping
		 */
		DM ("htc_if_thread: process exited and no output data found, force stopped");

		break;
	}

	/* send exit notification */
	msg.action = HTC_IF_ACTION_ACK_EXIT;
	msg.datalen = 0;
	TEMP_FAILURE_RETRY (send (commfd, &msg, sizeof (htc_if_msg), MSG_NOSIGNAL));

	pollfds_count = 1;
	pollfds [0].fd = commfd;
	pollfds [0].events = POLLHUP;

	count = TEMP_FAILURE_RETRY (poll (pollfds, pollfds_count, SU_CONNECTION_TIMEOUT));
	if (count == 0)  // timeout
	{
		DM ("htc_if_thread: poll timeout");
	}
	else if (count < 0)
	{
		DM ("htc_if_thread: poll: %s", strerror (errno));
	}
	else
	{
		if ((pollfds [0].revents & POLLHUP) != 0)  // remote closed
		{
			if (verbose) DM ("htc_if_thread: poll remote closed");
		}
	}

	close (commfd);

	htc_if_close_pipes (PIPE_COUNT, pipes);
	free (pipes);
	free (args);

	DM ("htc_if_thread [%d]: end\n", pid);
	return NULL;
}

static void response_error (pid_t pid, int commfd, const char *message, int error)
{
	htc_if_msg msg;
	pthread_t id;
	int ret, status, count;
	struct pollfd pollfds [1];

	/* send back the error string */
	char buffer [64];
	memset (buffer, 0, sizeof (buffer));
	snprintf (buffer, sizeof (buffer) - 1, "%s%s\n", message, strerror (error));
	count = strlen (buffer) + 1;

	DM ("response_error: %s\n", buffer);

	msg.magic = HTC_IF_MAGIC;
	msg.version = HTC_IF_VERSION;
	msg.action = HTC_IF_ACTION_ACK_ERR;
	msg.datalen = count;

	TEMP_FAILURE_RETRY (send (commfd, &msg, sizeof (htc_if_msg), MSG_NOSIGNAL));
	TEMP_FAILURE_RETRY (send (commfd, buffer, count, MSG_NOSIGNAL));

	msg.action = HTC_IF_ACTION_ACK_CODE;
	msg.datalen = sizeof (int);

	TEMP_FAILURE_RETRY (send (commfd, &msg, sizeof (htc_if_msg), MSG_NOSIGNAL));
	TEMP_FAILURE_RETRY (send (commfd, &error, sizeof (int), MSG_NOSIGNAL));

	/* send exit notification */
	msg.action = HTC_IF_ACTION_ACK_EXIT;
	msg.datalen = 0;
	TEMP_FAILURE_RETRY (send (commfd, &msg, sizeof (htc_if_msg), MSG_NOSIGNAL));

	pollfds [0].fd = commfd;
	pollfds [0].events = POLLHUP;

	count = TEMP_FAILURE_RETRY (poll (pollfds, 1, SU_CONNECTION_TIMEOUT));
	if (count == 0)  // timeout
	{
		DM ("response_error: poll timeout");
	}
	else if (count < 0)
	{
		DM ("response_error: poll: %s", strerror (errno));
	}
	else
	{
		if ((pollfds [0].revents & POLLHUP) != 0)  // remote closed
		{
			if (verbose) DM ("response_error: poll remote closed");
		}
	}

	close (commfd);

	if (pthread_create (& id, NULL, htc_if_thread_waitpid, (void *)(intptr_t) pid) != 0)
	{
		DM ("response_error: pthread_create htc_if_thread_waitpid: %s\n", strerror (errno));

		while (((ret = waitpid (pid, &status, 0)) == -1) && (errno == EINTR));
		if (ret != pid)
		{
			DM ("response_error: waitpid: %d %s", ret, strerror (errno));
		}
		else
		{
			DM ("response_error: waitpid returned pid %d, status = 0x%08X\n", ret, status);
		}
	}
	if (verbose) DM ("pthread_create htc_if_thread_waitpid: [%lu]", id);

	return;
}

static void handle_htc_if_fd (void)
{
	htc_if_msg msg;
	pid_t pid;
	int commfd;
	int count;
	char *pdata;
	struct ucred cr;
	struct sockaddr_un addr;
	socklen_t addr_size = sizeof (addr);
	socklen_t cr_size = sizeof (cr);
	int ret;
	int status;

	set_debug_level ();

	if (verbose)
	{
		/* force log level 6, this source won't be built on CRC ROM */
		//klog_set_level (6);
	}

	if ((commfd = accept (if_fd, (struct sockaddr *) &addr, &addr_size)) < 0)
		return;

	/* check socket options here */
	if (getsockopt (commfd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_size) < 0)
	{
		DM ("handle_htc_if_fd: getsockopt: %s\n", strerror (errno));
		close (commfd);
		return;
	}

	/* configure SO_RCVTIMEO to make sure schedule won't pend */
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	setsockopt (commfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));

	count = TEMP_FAILURE_RETRY (recv (commfd, &msg, sizeof (msg), 0));

	if (count != sizeof (msg))
	{
		DM ("handle_htc_if_fd: received wrong size %d (required %zu): %s\n", count, sizeof (msg), strerror (errno));
		close (commfd);
		return;
	}

	if ((msg.magic != (unsigned long) HTC_IF_MAGIC) || (msg.version < (unsigned short) HTC_IF_VERSION))
	{
		DM ("handle_htc_if_fd: invalid data! (0x%08lX, 0x%04X)\n", msg.magic, msg.version);
		close (commfd);
		return;
	}

	/* only accept operations issued by su command */
	if (! caller_is_su (cr.pid))
	{
		DM ("handle_htc_if_fd: not allow! (pid=%u, uid=%u, gid=%u)\n", cr.pid, cr.uid, cr.gid);
		close (commfd);
		return;
	}

	pdata = malloc (msg.datalen);

	if (pdata)
	{
		unsigned long idx;

		for (idx = 0, count = 0; idx < msg.datalen; idx += count)
		{
			count = TEMP_FAILURE_RETRY (recv (commfd, &pdata [idx], msg.datalen - idx, 0));

			/*
			 * recv() would return 0 when the peer has performed an orderly shutdown.
			 *
			 * Per S25 Chenglu Lin's suggestion, also take zero as an abnormal case.
			 */
			if (count <= 0)
			{
				if (errno == EAGAIN)
				{
					count = 0;
					continue;
				}
				DM ("handle_htc_if_fd: recv: %s\n", strerror (errno));
				free (pdata);
				close (commfd);
				return;
			}
		}
	}

	switch (msg.action)
	{
	case HTC_IF_ACTION_EXEC:
		{
		htc_if_thread_arg *parg1 = NULL;
		htc_if_thread_arg *parg2 = NULL;
		int *pipes = NULL;
		char **argv = (char **) pdata;
		int error = 0;

		count = *((int *) pdata);

		if (htc_if_setup_argv (count, argv, pdata + sizeof (int) + (sizeof (char *) * (count + 1 /* pointer count is argc + 1 */))) < 0)
		{
			DM ("handle_htc_if_fd: cannot parse command line!\n");
			free (pdata);
			close (commfd);
			return;
		}

		pipes = htc_if_open_pipes (PIPE_COUNT);

		if (! pipes)
		{
			free (pdata);
			close (commfd);
			return;
		}

		pid = fork ();

		if (pid == 0)  // child
		{
			close (commfd);
			fsync (STDOUT_FILENO);
			fsync (STDERR_FILENO);
			close (STDOUT_FILENO);
			close (STDERR_FILENO);
			dup2 (PIPE_STDOUT_OUT (pipes), STDOUT_FILENO);
			dup2 (PIPE_STDERR_OUT (pipes), STDERR_FILENO);
			htc_if_close_pipes (PIPE_COUNT, pipes);
			//free (pipes);

			execvp (argv [0], argv);  // execute commands
			error = errno;  // error handling for exec failed case.
			//fprintf (stderr, "su: exec failed for %s Error:%s\n", argv [0], strerror (error));

			write (STDERR_FILENO, "su: exec failed for ", 20);
			write (STDERR_FILENO, argv [0], strlen (argv [0]));
			write (STDERR_FILENO, "\n", 1);
			fsync (STDERR_FILENO);

			//free (pdata);
			_exit (-error);
		}
		else if (pid < 0)  // creation of child process failed.
		{
			error = errno;  // keep the errno of fork()
			DM ("handle_htc_if_fd: (pid < 0) fork: %s\n", strerror (error));
			response_error (pid, commfd, "fork: ", error);
			htc_if_close_pipes (PIPE_COUNT, pipes);
			free (pipes);
			free (pdata);
		}
		else	// parent
		{
			pthread_t id;

			close (PIPE_STDOUT_OUT (pipes)); PIPE_STDOUT_OUT (pipes) = -1;
			close (PIPE_STDERR_OUT (pipes)); PIPE_STDERR_OUT (pipes) = -1;
			free (pdata);

			DM ("handle_htc_if_fd: fork: parent got child pid [%d]\n", pid);

			if (((parg1 = malloc (sizeof (htc_if_thread_arg))) == NULL) || ((parg2 = malloc (sizeof (htc_if_thread_arg))) == NULL))
			{
				error = errno;
				DM ("handle_htc_if_fd: malloc htc_if_thread_arg: %s\n", strerror (errno));
				response_error (pid, commfd, "malloc: ", error);
				htc_if_close_pipes (PIPE_COUNT, pipes);
				free (pipes);
				if (parg1) free (parg1);
				if (parg2) free (parg2);
				return;
			}

			parg1->ptr1 = pipes;
			parg1->arg1 = pid;
			parg1->arg2 = commfd;

			if (pthread_create (& id, NULL, htc_if_thread, (void *) parg1) != 0)
			{
				error = errno;
				DM ("handle_htc_if_fd: pthread_create htc_if_thread: %s\n", strerror (errno));
				response_error (pid, commfd, "pthread_create htc_if_thread: ", error);
				htc_if_close_pipes (PIPE_COUNT, pipes);
				free (pipes);
				free (parg1);
				free (parg2);
				return;
			}
			if (verbose) DM ("pthread_create htc_if_thread: [%lu]", id);

			parg2->ptr1 = pipes;
			parg2->arg1 = pid;
			parg2->arg2 = commfd;

			if (pthread_create (& id, NULL, htc_if_thread_wait, (void *) parg2) != 0)
			{
				DM ("handle_htc_if_fd: pthread_create htc_if_thread_wait: %s\n", strerror (errno));

				while (((ret = waitpid (pid, &status, 0)) == -1) && (errno == EINTR));
				if (ret != pid)
				{
					DM ("handle_htc_if_fd: waitpid: %d %s", ret, strerror (errno));
					error = -1;
				}
				else
				{
					DM ("handle_htc_if_fd: waitpid returned pid %d, status = 0x%08X\n", ret, status);
					error = (WIFEXITED (status)) ? (WEXITSTATUS (status)) : -1;
				}

				TEMP_FAILURE_RETRY (write (PIPE_STATUS_OUT (pipes), &error, sizeof (int)));

				free (parg2);
				return;
			}
			if (verbose) DM ("pthread_create htc_if_thread_wait: [%lu]", id);
		}
		}
		break;

	default:
		DM ("handle_htc_if_fd: invalid action! (0x%X)\n", msg.action);
		free (pdata);
		close (commfd);
		break;
	}
}

static int get_htc_if_fd (void)
{
	return if_fd;
}

int htc_if_main (int UNUSED_VAR (server_socket))
{
	struct sigaction act;
	char buffer [PATH_MAX + 16];
	int ret = 0;
	int commfd = -1;
	int i;

	DM ("htc_if " VERSION "\n");

	int htc_if_fd_init = 0;
	struct pollfd ufds [1];
	int fd_count = 0;
	int nr, timeout = -1;

	if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
	{
		DM ("cannot handle SIGPIPE signal!\n");
	}

	htc_if_init ();  // socket initialization with valid htc_if fd

	if ((! htc_if_fd_init) && (get_htc_if_fd () > 0))
	{
		ufds [fd_count].fd = get_htc_if_fd ();
		ufds [fd_count].events = POLLIN;  // check for just normal data
		ufds [fd_count].revents = 0;
		fd_count++;
		htc_if_fd_init = 1;
	}

	while (! done)
	{
		if (verbose) DM ("waiting connection ...\n");

		nr = poll (ufds, fd_count, -1);

		if (nr <= 0)
		{
			continue;
		}

		if (ufds [0].revents & POLLIN)
		{
			if (ufds [0].fd == get_htc_if_fd ())
			{
				handle_htc_if_fd ();
			}
		}

		if (verbose) DM ("connection established.\n");
	}

	DM ("connection done.\n");

	done = 0;
	return ret;
}
