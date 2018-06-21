#define	LOG_TAG		"STT:displaytest"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "common.h"
#include "server.h"

#include "headers/poll.h"

/* custom commands */
#define	TEST_RUN	":run:"
#define	TEST_STOP	":stop:"
#define	TEST_GETDATA	":getdata:"

#define	SOCKET_BUFFER_SIZE	(512)

static int done = 0;

int displaytest_main (int server_socket)
{
	POLL poll_uevent = POLL_INITIAL;

	char buffer [SOCKET_BUFFER_SIZE + 16];
	int commfd = -1;
	int ufd = -1;
	int ret = 0;

	if (poll_open (& poll_uevent) < 0)
	{
		DM ("cannot create pipe!\n");
		done = 1;
	}

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
				ret = 1;
				buffer [0] = 0;
				break;
			}
			else if (CMP_CMD (buffer, TEST_RUN))
			{
				ret = -1;
				if (ufd < 0)
				{
					ret = ((ufd = local_uevent_register ("displaytest")) < 0) ? -1 : 0;
				}
				sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, TEST_GETDATA))
			{
				if (ufd >= 0)
				{
					buffer [0] = 0;

					if (poll_wait (& poll_uevent, ufd, 50) > 0)
					{
						read (ufd, buffer, sizeof (buffer) - 1);

						if (local_uevent_is_ended (buffer))
						{
							/* ignore the end string */
							buffer [0] = 0;
						}
					}
				}
			}
			else if (CMP_CMD (buffer, TEST_STOP))
			{
				local_uevent_unregister ("displaytest");
				ufd = -1;
				ret = 1;
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
	}

	poll_close (& poll_uevent);

	/* reset done flag */
	done = 0;

	return 0;
}
