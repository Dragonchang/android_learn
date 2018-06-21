#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define	LOG_TAG	"STT:pclink"

#include <utils/Log.h>

#include "libcommon.h"
#include "headers/pclink.h"

static int sockfd = -1;

int service_init_client (int port)
{
	struct sockaddr_in addr;
	int fd;

	if ((fd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		LOGE ("service_init_client: create socket failed: %s\n", strerror (errno));
		return -1;
	}

	memset (& addr, 0, sizeof (struct sockaddr));
	addr.sin_family = PF_INET;
	addr.sin_port = htons (port);
	//addr.sin_addr.s_addr = htonl (INADDR_ANY);
	addr.sin_addr.s_addr = inet_addr ("127.0.0.1");

	if (connect (fd, (struct sockaddr *) & addr, sizeof (struct sockaddr)) < 0)
	{
		LOGE ("service_init_client: connect failed: %s\n", strerror (errno));
		close (fd);
		return -1;
	}

	LOGD ("service_init_client: connection established.\n");

	return fd;
}

int service_send_command (int fd, const char *cmd, char *buf, int len)
{
	strcpy (buf, cmd);

	LOGD ("service_send_command: [%s]\n", buf);

	if (write (fd, buf, strlen (buf)) != (ssize_t) strlen (buf))
	{
		LOGE ("service_send_command: write error: %s\n", strerror (errno));
		return -1;
	}

	memset (buf, 0, len);

	if (read (fd, buf, len) < 0)
	{
		LOGE ("service_send_command: read error: %s\n", strerror (errno));
		return -1;
	}

	buf [len - 1] = 0;

	LOGD ("service_send_command: return [%s]\n", buf);

	return 0;
}

static int extract_addr (const char *str, char *addr, int len)
{
	const char *ptr = strrchr (str, ':');

	if ((! ptr) || (ptr == str))
	{
		LOGE ("extract_addr: no address found!");
		return -1;
	}

	if (len <= (ptr - str))
	{
		LOGE ("extract_addr: buffer underrun!");
		return -1;
	}

	len = ptr - str;
	strncpy (addr, str, len);
	addr [len] = 0;
	return 0;
}

static int extract_port (const char *str, int *port)
{
	const char *ptr = strrchr (str, ':');

	if (! ptr)
	{
		ptr = str;
	}
	else
	{
		ptr ++;
	}

	if (*ptr == 0)
	{
		LOGE ("extract_port: no port number found!");
		*port = -1;
		return -1;
	}

	*port = atoi (ptr);
	return 0;
}

int pclink_socket_accept (pclink_socket_info *psi)
{
	struct sockaddr_in cliaddr;
	int sockaddr_len = sizeof (struct sockaddr);
	int err = 0;

	if ((! psi) || (psi->server_fd < 0))
	{
		LOGE ("pclink_socket_accept: server socket was not opened!");
		return EBADF;
	}

	if (psi->comm_fd >= 0)
	{
		shutdown (psi->comm_fd, SHUT_RDWR);
		close (psi->comm_fd);
	}

	psi->is_connected = 0;

	memset (& cliaddr, 0, sizeof (struct sockaddr));

	if ((psi->comm_fd = accept (psi->server_fd, (struct sockaddr *) & cliaddr, (socklen_t *) & sockaddr_len)) < 0)
	{
		LOGE ("pclink_socket_accept: accept: %s!", strerror (errno));
		err = errno;
	}
	else
	{
		LOGD ("pclink_socket_accept: client %s:%d\n", inet_ntoa (cliaddr.sin_addr), cliaddr.sin_port);
		psi->is_connected = 1;
	}

	return err;
}

int pclink_socket_connect (pclink_socket_info *psi)
{
	struct sockaddr_in addr;

	int optval = 1;
	int err = 0;

	if (! psi)
	{
		LOGE ("pclink_socket_connect: client socket was not opened!");
		return EBADF;
	}

	if (psi->comm_fd >= 0)
	{
		shutdown (psi->comm_fd, SHUT_RDWR);
		close (psi->comm_fd);
	}

	psi->is_connected = 0;

	if ((psi->comm_fd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		LOGE ("pclink_socket_connect: cannot create socket: %s!", strerror (errno));
		return errno;
	}

	if (setsockopt (psi->comm_fd, SOL_SOCKET, SO_REUSEADDR, & optval, sizeof (optval)) < 0)
	{
		LOGE ("pclink_socket_connect: setsockopt SO_REUSEADDR: %s!", strerror (errno));
		return errno;
	}

	memset (& addr, 0, sizeof (struct sockaddr));
	addr.sin_family = PF_INET;
	addr.sin_port = htons (psi->port);

	//addr.sin_addr.s_addr = inet_addr (psi->ip);
	inet_aton (psi->ip, & addr.sin_addr);

	if (connect (psi->comm_fd, (struct sockaddr *) & addr, sizeof (struct sockaddr)) < 0)
	{
		LOGE ("pclink_socket_connect: connect: %s!", strerror (errno));
		err = errno;
	}
	else
	{
		psi->is_connected = 1;
	}

	return err;
}

int pclink_socket_in_use (pclink_socket_info *psi)
{
	if (psi)
	{
		return psi->in_use;
	}
	return 0;
}

pclink_type pclink_socket_type (pclink_socket_info *psi)
{
	if (psi)
	{
		return psi->type;
	}
	return PCLINK_NONE;
}

int pclink_socket_is_connected (pclink_socket_info *psi)
{
	if (psi)
	{
		return psi->is_connected;
	}
	return 0;
}

void pclink_socket_init (pclink_socket_info *psi)
{
	if (psi)
	{
		psi->in_use = 0;
		psi->is_connected = 0;
		psi->type = PCLINK_NONE;
		psi->ip = NULL;
		psi->port = -1;
		psi->server_fd = -1;
		psi->comm_fd = -1;
	}
}

int pclink_socket_close (pclink_socket_info *psi)
{
	if (psi)
	{
		if (psi->ip)
		{
			free (psi->ip);
		}
		if (psi->comm_fd >= 0)
		{
			shutdown (psi->comm_fd, SHUT_RDWR);
			close (psi->comm_fd);
		}
		if ((psi->comm_fd != psi->server_fd) && (psi->server_fd >= 0))
		{
			shutdown (psi->server_fd, SHUT_RDWR);
			close (psi->server_fd);
		}
		pclink_socket_init (psi);
	}
	return 0;
}

int pclink_socket_open (pclink_socket_info *psi, pclink_mode UNUSED_VAR (mode), pclink_type type, const char *hostinfo /* xx.xx.xx.xx:xxxx */)
{
	const char *default_ip = "127.0.0.1";
	struct sockaddr_in addr;
	size_t len;
	int optval = 1;

	if ((! psi) || (! hostinfo))
	{
		goto end;
	}

	if (psi->in_use)
	{
		pclink_socket_close (psi);
	}

	psi->in_use = 1;
	psi->is_connected = 0;
	psi->type = type;
	psi->ip = NULL;
	psi->port = -1;
	psi->server_fd = -1;
	psi->comm_fd = -1;

	len = strlen (hostinfo);

	if (len < strlen (default_ip))
	{
		len = strlen (default_ip);
	}

	if ((psi->ip = malloc (++ len)) == NULL)
	{
		LOGE ("pclink_socket_open: alloc memory error!");
		goto end;
	}

	if (extract_addr (hostinfo, psi->ip, len) < 0)
	{
		LOGI ("pclink_socket_open: use localhost.");
		strncpy (psi->ip, default_ip, len);
		psi->ip [len - 1] = 0;
	}

	if (extract_port (hostinfo, & psi->port) < 0)
	{
		LOGE ("pclink_socket_open: parse port number error!");
		goto end;
	}

	if ((type != PCLINK_SOCKET_SERVER) && (type != PCLINK_SOCKET_CLIENT))
	{
		LOGE ("pclink_socket_open: not a socket type (%d)!", type);
		goto end;
	}

	if (type == PCLINK_SOCKET_SERVER)
	{
		if ((psi->server_fd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		{
			LOGE ("pclink_socket_open: cannot create socket: %s!", strerror (errno));
			goto end;
		}

		if (setsockopt (psi->server_fd, SOL_SOCKET, SO_REUSEADDR, & optval, sizeof (optval)) < 0)
		{
			LOGE ("pclink_socket_open: setsockopt SO_REUSEADDR: %s!", strerror (errno));
			goto end;
		}

		memset (& addr, 0, sizeof (struct sockaddr));
		addr.sin_family = PF_INET;
		addr.sin_port = htons (psi->port);

		//addr.sin_addr.s_addr = htonl (INADDR_ANY);
		addr.sin_addr.s_addr = INADDR_ANY;

		if (bind (psi->server_fd, (struct sockaddr *) & addr, sizeof (struct sockaddr)) < 0)
		{
			LOGE ("pclink_socket_open: bind: %s!", strerror (errno));
			goto end;
		}

		if (listen (psi->server_fd, 1) < 0)
		{
			LOGE ("pclink_socket_open: listen: %s!", strerror (errno));
			goto end;
		}
	}

	return 0;

end:;
	pclink_socket_close (psi);
	return -1;
}

int pclink_open_ex (pclink_mode mode, pclink_type type, const char *devpath)
{
	char command [256];
	char buffer [256];
	int data_fd = -1;
	int port;

	if (sockfd != -1)
	{
		LOGE ("pclink_open: cannot open connection twice!");
		goto end;
	}

	/* connect to server */
	sockfd = service_init_client (SOCKET_PORT_SERVICE);

	if (sockfd < 0)
		goto end;

	/* run iotty service */
	if (service_send_command (sockfd, "iotty", buffer, sizeof (buffer)) < 0)
		goto end;

	port = atoi (buffer);

	if (port <= 0)
	{
		LOGE ("pclink_open: got error return (%d) from server!\n", port);
		goto end;
	}

	shutdown (sockfd, SHUT_RDWR);
	close (sockfd);

	/* connect to service */
	LOGD ("pclink_open: connect to service at port (%d).\n", port);

	sockfd = service_init_client (port);

	if (sockfd < 0)
		goto end;

	/* get io port */
	if (service_send_command (sockfd, ":getioport:", buffer, sizeof (buffer)) < 0)
		goto end;

	port = atoi (buffer);

	if (port <= 0)
	{
		LOGE ("pclink_open: got error return (%d) from service!\n", port);
		goto end;
	}

	/* set device path */
	snprintf (command, sizeof (command), ":setpath:%s", devpath ? devpath : "auto");
	command [sizeof (command) - 1] = 0;

	if (service_send_command (sockfd, command, buffer, sizeof (buffer)) < 0)
		goto end;

	/* set tty mode */
	snprintf (command, sizeof (command), ":setmode:%d", mode);
	command [sizeof (command) - 1] = 0;

	if (service_send_command (sockfd, command, buffer, sizeof (buffer)) < 0)
		goto end;

	/* set tty type */
	snprintf (command, sizeof (command), ":settype:%d", type);
	command [sizeof (command) - 1] = 0;

	if (service_send_command (sockfd, command, buffer, sizeof (buffer)) < 0)
		goto end;

	/* open data service */
	if (service_send_command (sockfd, ":open:", buffer, sizeof (buffer)) < 0)
		goto end;

	/* create data socket */
	data_fd = service_init_client (port);

	if (data_fd < 0)
		goto end;

	/* return */
	return data_fd;

end:;
	if (sockfd >= 0) close (sockfd);
	if (data_fd >= 0) close (data_fd);
	sockfd = -1;
	return -1;
}

int pclink_open (pclink_mode mode)
{
	return pclink_open_ex (mode, PCLINK_UART, "auto");
}

int pclink_close (int fd)
{
	char buffer [8];

	if (fd >= 0)
	{
		shutdown (fd, SHUT_RDWR);
		close (fd);
	}

	if (sockfd < 0)
		return -1;

	service_send_command (sockfd, ":close:", buffer, sizeof (buffer));

	shutdown (sockfd, SHUT_RDWR);
	close (sockfd);
	sockfd = -1;
	return 0;
}

int pclink_read (int fd, char *buffer, int len)
{
	int ret;
	if ((ret = read (fd, buffer, len)) < 0)
	{
		LOGE ("pclink_read: %s\n", strerror (errno));
	}
	return ret;
}

int pclink_write (int fd, char *buffer, int len)
{
	int ret;
	if ((ret = write (fd, buffer, len)) < 0)
	{
		LOGE ("pclink_write: %s\n", strerror (errno));
	}
	return ret;
}
