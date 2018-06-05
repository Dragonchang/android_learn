
#define	LOG_TAG	"STT:poll"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <cutils/log.h>

//#include "headers/pollbase.h"
#include "headers/libcommon.h"
#include "headers/pollbase.h"
#include "headers/fio.h"


/*
 * return 1 on true, 0 on false.
 */
int poll_is_opened (POLL *pl)
{
	if ((pl != NULL) && (pl->pipefds [0] >= 0))
		return 1;
	return 0;
}

/*
 * return -1 on error, 0 on success.
 */
int poll_open (POLL *pl)
{
	if (! pl) return -1;
	pl->pipefds [0] = -1;
	pl->pipefds [1] = -1;
	if (pipe (pl->pipefds) != 0) return -1;
	return 0;
}

/*
 * return -1 on error, 0 on success.
 */
int poll_close (POLL *pl)
{
	if (! pl) return -1;
	if (pl->pipefds [0] != -1) close_nointr (pl->pipefds [0]);
	if (pl->pipefds [1] != -1) close_nointr (pl->pipefds [1]);
	pl->pipefds [0] = -1;
	pl->pipefds [1] = -1;
	return 0;
}

/*
 * return -1 on error, 0 on success.
 */
int poll_break (POLL *pl)
{
	if (! pl) return -1;
	if (pl->pipefds [POLL_PIPE_WRITE] == -1) return -1;
	if (write_nointr (pl->pipefds [POLL_PIPE_WRITE], " ", 1) != 1) return -1;
	return 0;
}

/*
 * return 1 on true, 0 on false.
 */
int poll_check_data (int fd)
{
	struct pollfd fds [1];
	int nr;

	if (fd < 0)
	{
		return 0;
	}

	fds [0].fd = fd;
	fds [0].events = POLLIN;
	fds [0].revents = 0;

	for (;;)
	{
		nr = poll (fds, 1, 10);

		if ((nr < 0) && (errno == EINTR))
		{
			fLOGD ("%s, retry poll in poll_check_data [%d]\n", strerror (errno), fds [0].fd);
			usleep (10000);
			continue;
		}

		break;
	}

	if ((nr > 0) && (fds [0].revents & POLLIN))
	{
		/* have data */
		return 1;
	}

	if (nr < 0)
	{
		fLOGE ("poll: %d: %s\n", nr, strerror (errno));
	}
	return 0;
}

/*
 * return < 0 on error, 0 on user break or timeout, 1 on data detect.
 */
int poll_wait (POLL *pl, int fd, int timeout_ms)
{
	struct pollfd fds [2];
	int nr;
	if (! pl) return -1;
	for (;;)
	{
		fds [0].fd = fd;
		fds [0].events = POLLIN;
		fds [0].revents = 0;

		fds [1].fd = pl->pipefds [POLL_PIPE_READ];
		fds [1].events = POLLIN;
		fds [1].revents = 0;

		for (;;)
		{
			nr = poll (fds, 2, timeout_ms);

			if ((nr < 0) && (errno == EINTR))
			{
				fLOGD ("%s, retry poll in poll_wait [%d][%d]\n", strerror (errno), fds [0].fd, fds [1].fd);
				usleep (10000);
				continue;
			}

			break;
		}

		if (nr <= 0)
		{
			if (nr < 0)
			{
				fLOGE ("poll: %d: %s\n", nr, strerror (errno));
			}
			break;
		}

		if (fds [1].revents & POLLIN)
		{
			/* user break */
			char buffer [1];
			read_nointr (fds [1].fd, buffer, 1);
			nr = 0;
			break;
		}

		if (fds [0].revents & POLLIN)
		{
			/* have data */
			nr = 1;
			break;
		}

		/* nr > 0 but no valid data found */
		for (nr = 0; nr < 2; nr ++)
		{
			if (fds [nr].revents)
			{
				fLOGD ("poll no valid data! fd[%d]=%d revents=0x%04X\n", nr, fds [nr].fd, fds [nr].revents);
			}
		}
		nr = -1;
		break;
	}
	return nr;
}

/*
 * return < 0 on error, 0 on user break or timeout, others for fd index (1 base).
 */
int poll_multiple_wait (POLL *pl, int timeout_ms, int *fd, int count)
{
	struct pollfd *fds;
	int nr;
	int debug;
	if ((! pl) || (! fd) || (count <= 0)) return -1;
	fds = (struct pollfd *) malloc ((count + 1) * sizeof (struct pollfd));
	if (! fds) return -1;
	for (;;)
	{
		debug = (access (DAT_DIR ".debug.poll", F_OK) == 0);

		if (debug) fLOGD ("count = %d (+1)\n", count);

		for (nr = 0; nr < count; nr ++)
		{
			if (debug) fLOGD ("  device fd %d = %d\n", nr, fd [nr]);
			fds [nr].fd = fd [nr];
			fds [nr].events = POLLIN;
			fds [nr].revents = 0;
		}

		if (debug) fLOGD ("    pipe fd %d = %d\n", nr, pl->pipefds [POLL_PIPE_READ]);
		fds [nr].fd = pl->pipefds [POLL_PIPE_READ];
		fds [nr].events = POLLIN;
		fds [nr].revents = 0;

		for (;;)
		{
			nr = poll (fds, count + 1, timeout_ms);

			if ((nr < 0) && (errno == EINTR))
			{
				fLOGD ("%s, retry poll in poll_multiple_wait\n", strerror (errno));
				usleep (10000);
				continue;
			}

			break;
		}

		if (debug) fLOGD ("  poll() got %d\n", nr);

		if (nr <= 0)
		{
			if (nr < 0)
			{
				fLOGE ("poll: %d: %s\n", nr, strerror (errno));
			}
			break;
		}

		if (fds [count].revents & POLLIN)
		{
			/* user break */
			char buffer [1];
			read_nointr (fds [count].fd, buffer, 1);
			if (debug) fLOGD ("  user break, return 0\n");
			nr = 0;
			break;
		}

		for (nr = 0; nr < count; nr ++)
		{
			if (fds [nr].revents & POLLIN)
			{
				/* have data */
				if (debug) fLOGD ("  data return %d (+1)\n", nr);
				free (fds);
				return (nr + 1);
			}
		}

		if (debug) fLOGD ("  no valid data!\n");

		/* nr > 0 but no valid data found */
		for (nr = 0; nr <= count; nr ++)
		{
			if (fds [nr].revents)
			{
				fLOGD ("poll no valid data! fd[%d]=%d revents=0x%04X\n", nr, fds [nr].fd, fds [nr].revents);
			}
		}
		nr = -1;
		break;
	}
	free (fds);
	if (debug) fLOGD ("  return %d\n", nr);
	return nr;
}

