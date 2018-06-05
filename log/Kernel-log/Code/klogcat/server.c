

#define	LOG_TAG		"STT:server.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "headers/common.h"
#include "headers/server.h"

/*
 * return socket fd
 */
int init_server (int port)
{
	struct sockaddr_in addr;
	int optval = 1;
	int sockfd;

	if ((sockfd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		DM ("create socket failed: %s\n", strerror (errno));
		return -1;
	}

	if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, & optval, sizeof (optval)) < 0)
	{
		DM ("setsockopt SO_REUSEADDR failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	memset (& addr, 0, sizeof (struct sockaddr));
	addr.sin_family = PF_INET;
	addr.sin_port = htons (port);
	//addr.sin_addr.s_addr = htonl (INADDR_ANY);
	//addr.sin_addr.s_addr = INADDR_ANY;
	/*
	 * here we cannot use INADDR_ANY (0.0.0.0) due to Google's rule (CTS, CtsNetTestCases)
	 */
	inet_aton ("127.0.0.1", & addr.sin_addr);

	if (bind (sockfd, (struct sockaddr *) & addr, sizeof (struct sockaddr)) < 0)
	{
		DM ("bind socket failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	if (listen (sockfd, 1) < 0)
	{
		DM ("listen socket failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	return sockfd;
}

int wait_for_connection (int fd)
{
	struct sockaddr_in cliaddr;
	int sockaddr_len = sizeof (struct sockaddr);
	memset (& cliaddr, 0, sizeof (struct sockaddr));
	return accept (fd, (struct sockaddr *) & cliaddr, (socklen_t *) & sockaddr_len);
}

int get_client_info (int sockfd, int *pid, int *uid, int *gid)
{
	struct ucred cr;
	int len = sizeof (cr);

	if (getsockopt (sockfd, SOL_SOCKET, SO_PEERCRED, & cr, (socklen_t *) & len) < 0)
	{
		DM ("getsockopt failed: %s\n", strerror (errno));
		return -1;
	}

	if (pid) *pid = cr.pid;
	if (uid) *uid = cr.uid;
	if (gid) *gid = cr.gid;
	return 0;
}

int get_port (const int sockfd)
{
	struct sockaddr_in addr;
	int ret = sizeof (struct sockaddr);
	if (getsockname (sockfd, (struct sockaddr *) & addr, (socklen_t *) & ret) < 0)
	{
		DM ("getsockname failed!\n");
		ret = -1;
	}
	else
	{
		ret = ntohs (addr.sin_port);
	}
	return ret;
}

static int free_port = -1;

int get_free_port (void)
{
	if (free_port < 0)
	{
		free_port = SOCKET_PORT_SERVICE + 1;
	}
	if (free_port > SOCKET_PORT_LAST)
	{
		return -1;
	}
	return free_port ++;
}

void local_destroy_all_sockets (void)
{
	char buffer [PATH_MAX];

	snprintf (buffer, sizeof (buffer), "rm " SOCKET_SERVER_PATH "*");
	buffer [sizeof (buffer) - 1] = 0;
	DM ("run [%s]\n", buffer);
	system (buffer);

	snprintf (buffer, sizeof (buffer), "rm " SOCKET_CLIENT_PATH "*");
	buffer [sizeof (buffer) - 1] = 0;
	DM ("run [%s]\n", buffer);
	system (buffer);
}

static char *local_build_path (int port, const char *path, char *buffer, int len)
{
	snprintf (buffer, len, "%s%d", path, port);
	buffer [len - 1] = 0;
	return buffer;
}

int local_destroy_server (int port)
{
	char buffer [PATH_MAX];
	return unlink (local_build_path (port, SOCKET_SERVER_PATH, buffer, sizeof (buffer)));
}

int local_init_server (int port)
{
	char buffer [PATH_MAX];

	struct sockaddr_un addr;
	int sockfd, bind_size;

	if (port == 0)
	{
		port = get_free_port ();

		if (port < 0)
		{
			DM ("cannot find a free port!\n");
			return -1;
		}
	}

	if ((sockfd = socket (AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		DM ("create local socket failed: %s\n", strerror (errno));
		return -1;
	}

	memset (& addr, 0, sizeof (addr));
	addr.sun_family = AF_UNIX;
	strncpy (addr.sun_path, local_build_path (port, SOCKET_SERVER_PATH, buffer, sizeof (buffer)), sizeof (addr.sun_path) - 1);

	/* if such a file already exists, remove it */
	unlink (addr.sun_path);

	bind_size = offsetof (struct sockaddr_un, sun_path) + strlen (addr.sun_path);

	if (bind (sockfd, (struct sockaddr *) & addr, bind_size) == -1)
	{
		DM ("bind local socket failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	chmod (addr.sun_path, 0777);

	if (listen (sockfd, 1) < 0)
	{
		DM ("listen local socket failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	return sockfd;
}

int local_wait_for_connection (int sockfd)
{
	struct sockaddr_un cliaddr;
	int sockaddr_len = sizeof (struct sockaddr);
	memset (& cliaddr, 0, sizeof (struct sockaddr));
	return accept (sockfd, (struct sockaddr *) & cliaddr, (socklen_t *) & sockaddr_len);
}

int local_get_client_info (int sockfd, int *pid, int *uid, int *gid)
{
	struct ucred cr;
	int len = sizeof (cr);

	if (getsockopt (sockfd, SOL_SOCKET, SO_PEERCRED, & cr, (socklen_t *) & len) < 0)
	{
		DM ("getsockopt failed: %s\n", strerror (errno));
		return -1;
	}

	if (pid) *pid = cr.pid;
	if (uid) *uid = cr.uid;
	if (gid) *gid = cr.gid;
	return 0;
}

