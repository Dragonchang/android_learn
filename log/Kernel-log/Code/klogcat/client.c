#define	LOG_TAG		"STT:client.c"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "headers/common.h"
#include "headers/client.h"
#include "headers/libcommon.h"

/*
 * return socket fd
 */
int init_client (const char *ip, int port)
{
	struct sockaddr_in addr;
	int sockfd;

	if (! ip)
	{
		DM ("null ip!\n");
		return -1;
	}

	if ((sockfd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		DM ("create socket failed!\n");
		return -1;
	}

	memset (& addr, 0, sizeof (struct sockaddr));
	addr.sin_family = PF_INET;
	addr.sin_port = htons (port);
	//addr.sin_addr.s_addr = inet_addr (ip);
	inet_aton (ip, & addr.sin_addr);

	if (connect (sockfd, (struct sockaddr *) & addr, sizeof (struct sockaddr)) < 0)
	{
		DM ("connect failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	DM ("connection established.\n");

	return sockfd;
}

static char *local_build_path (int port, const char *path, char *buffer, int len)
{
	snprintf (buffer, len, "%s%d", path, port);
	buffer [len - 1] = 0;
	return buffer;
}

int local_destroy_client (int port)
{
	char buffer [PATH_MAX];
	return unlink (local_build_path (port, SOCKET_CLIENT_PATH, buffer, sizeof (buffer)));
}

int local_init_client (int port)
{
	char buffer [PATH_MAX];

	struct sockaddr_un addr;
	int sockfd, bind_size;

	if ((sockfd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
	{
		DM ("create local socket failed!\n");
		return -1;
	}

	memset (& addr, 0, sizeof (addr));
	addr.sun_family = AF_UNIX;
	strncpy (addr.sun_path, local_build_path (port, SOCKET_CLIENT_PATH, buffer, sizeof (buffer)), sizeof (addr.sun_path) - 1);

	bind_size = offsetof (struct sockaddr_un, sun_path) + strlen (addr.sun_path);

	if (bind (sockfd, (struct sockaddr *) & addr, bind_size) == -1)
	{
		DM ("bind local socket failed: %s\n", strerror (errno));
		close (sockfd);
		return -1;
	}

	chmod (addr.sun_path, 0777);

	memset (& addr, 0, sizeof (addr));
	addr.sun_family = AF_UNIX;
	strncpy (addr.sun_path, local_build_path (port, SOCKET_SERVER_PATH, buffer, sizeof (buffer)), sizeof (addr.sun_path) - 1);

	bind_size = offsetof (struct sockaddr_un, sun_path) + strlen (addr.sun_path);

	//if (connect (sockfd, (struct sockaddr *) & addr, sizeof (addr)) == -1)
	if (connect (sockfd, (struct sockaddr *) & addr, bind_size) == -1)
	{
		DM ("connect failed: %s\n", strerror (errno));
		unlink (SOCKET_CLIENT_PATH);
		return -1;
	}

	DM ("local connection established.\n");

	return sockfd;
}

