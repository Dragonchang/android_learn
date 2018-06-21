#define	LOG_TAG		"STT:client_cmd"

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

static int do_read = 0;

static int do_test (int port, char *cmd)
{
	char buffer [MAX_NAME_LEN + 16];

	int sockfd = -1;

	if (! cmd)
	{
		DM ("no command given!\n");
		return -1;
	}

	DM ("-- Port: %d\n", port);
	DM ("-- Command: [%s]\n", cmd);

	sockfd = init_client (LOCAL_IP, port);

	if (sockfd < 0)
		goto end;

	/* run service */
	strcpy (buffer, cmd);

	DM ("send command [%s].\n", buffer);

	if (write (sockfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
	{
		DM ("send command [%s] to server failed!\n", buffer);
		goto end;
	}

	if (do_read)
	{
		/* read data */
		memset (buffer, 0, sizeof (buffer));

		if (read (sockfd, buffer, sizeof (buffer)) < 0)
		{
			DM ("read data error! close connection!\n");
		}

		buffer [sizeof (buffer) - 1] = 0;

		DM ("read [%s].\n", buffer);
	}

end:;
	if (sockfd >= 0)
		close (sockfd);

	return 0;
}

int main (int argc, char **argv)
{
	char buffer [256];
	char *cmd = NULL;
	int port = -1;
	int i;

	for (i = 1; i < argc; i ++)
	{
		if (! strcmp (argv [i], "-h"))
		{
			DM ("usage: %s [-r] [-p port] [command]\n", argv [0]);
			return 1;
		}
		else if (! strcmp (argv [i], "-r"))
		{
			do_read = 1;
		}
		else if (! strcmp (argv [i], "-p"))
		{
			if (++ i == argc)
				break;

			port = atoi (argv [i]);

			if (port == 0)
			{
				DM ("invalid port (%s)!\n", argv [-- i]);
			}
		}
		else
		{
			cmd = argv [i];
		}
	}

	if (port <= 0)
	{
		port = SOCKET_PORT_SERVICE;
	}

	/* do single command */
	if (cmd)
		return do_test (port, cmd);

	/* show menu */
	for (i = 0; i != 1;)
	{
		printf ("============================\n"
			" Current port: %d\n"
			"============================\n"
			"  1. Run service\n"
			"  2. List services\n"
			"  3. Get server version\n"
			"  4. End server and quit\n"
			"  ------------------------\n"
			"  5. Change current port\n"
			"  6. Send " CMD_GETVER "\n"
			"  7. Send " CMD_ENDSERVER "\n"
			"  8. Send custom command\n"
			"  ------------------------\n"
			"  q. Quit\n"
			"============================\n"
			"Choose: ", port);

		fscanf (stdin, "%s", buffer);

		switch (buffer [0])
		{
		case '1':
			printf ("Service name: ");
			fscanf (stdin, "%s", buffer);
			do_test (SOCKET_PORT_SERVICE, buffer);
			break;
		case '2':
			do_test (SOCKET_PORT_SERVICE, CMD_LISTSERVICES);
			break;
		case '3':
			do_test (SOCKET_PORT_SERVICE, CMD_GETVER);
			break;
		case '4':
			do_test (SOCKET_PORT_SERVICE, CMD_ENDSERVER);
			i = 1;
			break;
		case '5':
			printf ("Input port: ");
			fscanf (stdin, "%s", buffer);
			port = atoi (buffer);
			continue;
		case '6':
			do_test (port, CMD_GETVER);
			break;
		case '7':
			do_test (port, CMD_ENDSERVER);
			break;
		case '8':
			printf ("Input command: ");
			fscanf (stdin, "%s", buffer);
			do_test (port, buffer);
			break;
		case 'q':
			i = 1;
			continue;
		}

		sleep (1);
	}

	return 0;
}
