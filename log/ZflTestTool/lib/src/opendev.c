#define	LOG_TAG	"STT:input"

#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <linux/input.h>
#include <cutils/log.h>

#include "libcommon.h"

#define	MAX_DEVICES	32

int find_input_device (const char *keyword, char *buffer, int len)
{
	char devname [80];
	char dev [20] = "/dev/input/event";
	int fd, i;

	if (buffer && (len >= (int) sizeof (dev))) for (i = 0; i < MAX_DEVICES; i ++)
	{
		sprintf (& dev [16], "%d", i);

		fd = open (dev, O_RDWR);

		if (fd < 0)
		{
			fLOGE ("%s: %s", dev, strerror (errno));
			break;
		}

		memset (devname, 0, sizeof (devname));

		if (ioctl (fd, EVIOCGNAME (sizeof (devname) - 1), & devname) < 1)
		{
			fLOGE ("get device name of %s: %s", dev, strerror (errno));
			close (fd);
			continue;
		}

		fLOGV ("%s: [%s] <-> [%s]", dev, devname, keyword);

		if (strstr (devname, keyword) != NULL)
		{
			strcpy (buffer, dev);
			close (fd);
			return 0;
		}

		close (fd);
	}

	return -1;
}

int open_input_device (const char *keyword)
{
	char devname [80];
	char dev [20] = "/dev/input/event";
	int fd, i;

	for (i = 0; i < MAX_DEVICES; i ++)
	{
		sprintf (& dev [16], "%d", i);

		fd = open (dev, O_RDWR);

		if (fd < 0)
		{
			fLOGE ("%s: %s", dev, strerror (errno));
			break;
		}

		memset (devname, 0, sizeof (devname));

		if (ioctl (fd, EVIOCGNAME (sizeof (devname) - 1), & devname) < 1)
		{
			fLOGE ("get device name of %s: %s", dev, strerror (errno));
			close (fd);
			continue;
		}

		fLOGV ("%s: [%s] <-> [%s]", dev, devname, keyword);

		if (strstr (devname, keyword) != NULL)
			return fd;

		close (fd);
	}

	return -1;
}

static int inlist (const char **list, const char *name)
{
	int i;
	for (i = 0; list [i] != NULL; i ++)
		if (strstr (name, list [i]) != NULL)
			return 1;
	return 0;
}

/*
 * Open all input devices.
 *
 * Return an integer array contains fds, the first integer is the fd count.
 */
int *open_input_devices (const char **white_list, const char **black_list)
{
	char devname [80];
	char dev [20] = "/dev/input/event";
	int fds [MAX_DEVICES];
	int i, idx, *ret = NULL;

	for (i = 0, idx = 0; i < MAX_DEVICES; i ++)
	{
		sprintf (& dev [16], "%d", i);

		fds [idx] = open (dev, O_RDWR);

		if (fds [idx] < 0)
			break;

		memset (devname, 0, sizeof (devname));

		if (ioctl (fds [idx], EVIOCGNAME (sizeof (devname) - 1), & devname) < 1)
		{
			fLOGE ("ioctl %s: %s\n", dev, strerror (errno));
			close (fds [idx]);
			continue;
		}

		if (white_list && black_list)
		{
			if (! inlist (white_list, devname))
				goto skipped;

			if (inlist (black_list, devname))
				goto skipped;
		}
		else if (white_list)
		{
			if (! inlist (white_list, devname))
				goto skipped;
		}
		else if (black_list)
		{
			if (inlist (black_list, devname))
				goto skipped;
		}

		fLOGD ("opened [%s][%s][%d]", dev, devname, fds [idx]);

		idx ++;
		continue;

	skipped:;
		close (fds [idx]);
		fLOGD ("filter [%s][%s]", dev, devname);
	}

	if (idx == 0)
	{
		fLOGE ("cannot open any input event node!");
		goto end;
	}

	ret = (int *) malloc (sizeof (int) * (idx + 1));

	if (ret)
	{
		ret [0] = idx;

		for (i = 0; i < idx; i ++)
			ret [i + 1] = fds [i];
	}

end:;
	return ret;
}

void close_input_devices (int *list)
{
	if (list)
	{
		int i;

		for (i = 1; i <= list [0]; i ++)
		{
			close (list [i]);
			fLOGD ("closed [%d]", list [i]);
		}

		free (list);
	}
}
