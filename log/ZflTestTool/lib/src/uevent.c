#define	LOG_TAG	"STT:uevent"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>

#include <cutils/log.h>

#include "libcommon.h"
#include "headers/fio.h"
#include "headers/poll.h"
#include "headers/uevent.h"

int open_uevent_socket (void)
{
	struct sockaddr_nl addr;
	int sz = 64 * 1024;
	int s;

	memset (& addr, 0, sizeof (addr));

	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid ();
	addr.nl_groups = 0xffffffff;

	s = socket (PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);

	if (s < 0)
	{
		return -1;
	}

	setsockopt (s, SOL_SOCKET, SO_RCVBUFFORCE, & sz, sizeof (sz));

	if (bind (s, (struct sockaddr *) & addr, sizeof (addr)) < 0)
	{
		close_nointr (s);
		return -1;
	}

	return s;
}

void uevent_close (UEVENT *ue)
{
	if (ue)
	{
		poll_close (& ue->poller);
		if (ue->fd >= 0) close_nointr (ue->fd);
		free (ue);
	}
}

UEVENT *uevent_open (void)
{
	UEVENT *ue = (UEVENT *) malloc (sizeof (UEVENT));

	if (ue)
	{
		if (poll_open (& ue->poller) < 0)
		{
			fLOGE ("poll_open() failed!\n");
			free (ue);
			ue = NULL;
		}
		else if ((ue->fd = open_uevent_socket ()) < 0)
		{
			fLOGE ("open_uevent_socket() failed!\n");
			uevent_close (ue);
			ue = NULL;
		}
	}

	return ue;
}

int uevent_read (UEVENT *ue, char *buffer, int buffer_length, int timeout_ms)
{
	int nr, count = 0;
	if (ue)
	{
		for (;;)
		{
			count = poll_wait (& ue->poller, ue->fd, timeout_ms);

			if (count <= 0)
				break;

			count = recv (ue->fd, buffer, buffer_length, 0);

			if ((count < 0) && (errno == EINTR))
			{
				fLOGD ("%s, retry recv [%d]\n", strerror (errno), ue->fd);
				usleep (10000);
				continue;
			}

			if (count <= 0)
			{
				if (count < 0)
				{
					fLOGE ("recv: %s\n", strerror (errno));
				}
				break;
			}

			return count;
		}
		buffer [0] = 0;
	}
	return count;
}

void uevent_interrupt (UEVENT *ue)
{
	if (ue)
	{
		poll_break (& ue->poller);
	}
}
