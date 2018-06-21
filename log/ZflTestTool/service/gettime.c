#define	LOG_TAG		"STT:gettime"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "common.h"
#include "server.h"

int gettime_main (int server_socket)
{
	char buffer [MAX_NAME_LEN + 16];
	int commfd;
	time_t tm;

	DM ("waiting connection ...\n");

	commfd = wait_for_connection (server_socket);

	if (commfd < 0)
	{
		DM ("accept client connection failed!\n");
	}

	DM ("connection established.\n");

	/* send time */
	time (& tm);
	strcpy (buffer, ctime (& tm));
	buffer [strlen (buffer) - 1] = 0; /* remove '\n' */

	if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
	{
		DM ("send time to client failed!\n");
	}

	for (;;)
	{
		memset (buffer, 0, sizeof (buffer));

		if (read (commfd, buffer, sizeof (buffer)) < 0)
		{
			DM ("read command error! close connection!\n");
			break;
		}

		buffer [sizeof (buffer) - 1] = 0;

		if (CMP_CMD (buffer, CMD_ENDSERVER))
		{
			/* end server */
			break;
		}
	}
	close (commfd);
	return 0;
}
