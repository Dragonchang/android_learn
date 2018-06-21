#define	LOG_TAG		"STT:client_gettime"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include "common.h"
#include "client.h"

int main (void)
{
	char buffer [MAX_NAME_LEN + 16];
	int sockfd = -1;
	int port;

	sockfd = init_client (LOCAL_IP, SOCKET_PORT_SERVICE);

	if (sockfd < 0)
		goto end;

	/* run service */
	strcpy (buffer, "gettime");

	DM ("request service [%s].\n", buffer);

	if (write (sockfd, buffer, strlen (buffer)) != strlen (buffer))
	{
		DM ("send service [%s] to server failed!\n", buffer);
	}

	/* read port */
	memset (buffer, 0, sizeof (buffer));

	if (read (sockfd, buffer, sizeof (buffer)) < 0)
	{
		DM ("read command error! close connection!\n");
	}

	buffer [sizeof (buffer) - 1] = 0;

	port = atoi (buffer);

	if (port <= 0)
	{
		DM ("got error return (%d) from server!\n", port);
		goto end;
	}

	shutdown (sockfd, SHUT_RDWR);
	close (sockfd);

	DM ("connect to service at port (%d).\n", port);

	sockfd = init_client (LOCAL_IP, port);

	if (sockfd < 0)
		goto end;

	/* read time */
	memset (buffer, 0, sizeof (buffer));

	if (read (sockfd, buffer, sizeof (buffer)) < 0)
	{
		DM ("read command error! close connection!\n");
	}

	buffer [sizeof (buffer) - 1] = 0;

	DM ("get time [%s].\n", buffer);

	/* stop server */
	strcpy (buffer, CMD_ENDSERVER);

	if (write (sockfd, buffer, strlen (buffer)) != strlen (buffer))
	{
		DM ("send command [%s] to service failed!\n", buffer);
	}

end:;
	if (sockfd >= 0)
		close (sockfd);
	return 0;
}
