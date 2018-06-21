#define	LOG_TAG	"STT:board"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/properties.h>

#include "libcommon.h"
#include "headers/board.h"
#include "headers/fio.h"

#define IS_VALID_NAME_CHAR(c)	(isalnum (c) || (c == '_') || (c == '-') || (c == ' '))

int read_proc_cmdline (const char *key, char *value, int len, const char *default_value)
{
	char buffer [128];
	char c;
	int fd, i;
	int klen;

	if ((! key) || (! value) || (len <= 0))
		return -1;

	memset (value, 0, len);

	klen = strlen (key);

	if (klen >= (int) sizeof (buffer))
	{
		fLOGE ("read_proc_cmdline: given key is too long! (%d)", klen);
		strncpy (value, default_value, len - 1);
		value [len - 1] = 0;
		return -1;
	}

	if ((fd = open_nointr ("/proc/cmdline", O_RDONLY, DEFAULT_FILE_MODE)) < 0)
	{
		fLOGE ("open_nointr: /proc/cmdline: %s", strerror (errno));
		strncpy (value, default_value, len - 1);
		value [len - 1] = 0;
		return -1;
	}

	for (i = 0; i < (int) sizeof (buffer);)
	{
		int count = read_nointr (fd, & c, 1);

		if ((count == 1) && (c != ' '))
		{
			buffer [i ++] = c;
			continue;
		}

		buffer [i] = 0;
		i = 0;

		//fLOGD ("read_proc_cmdline: [%s] <-> [%s]", key, buffer);

		if ((strncmp (buffer, key, klen) == 0) && (buffer [klen] == '='))
		{
			strncpy (value, & buffer [klen + 1], len - 1);
			value [len - 1] = 0;
			break;
		}

		if (count != 1)
		{
			break;
		}
	}

	close_nointr (fd);

	return strlen (value);
}

char *get_board_name (char *buf, int len)
{
	const char *unknown = "unknown";

	char data [PROPERTY_VALUE_MAX];
	int dlen;
	int i;

	if (len > (int) sizeof (data))
	{
		len = sizeof (data);
	}

	memset (data, 0, sizeof (data));

	dlen = property_get ("ro.product.device", data, "");

	fLOGD ("get_board_name: read prop ro.product.device [%s]", data);

	if ((! data [0]) || (strcmp (data, unknown) == 0))
	{
		memset (data, 0, sizeof (data));

		dlen = property_get ("ro.build.product", data, "");

		fLOGD ("get_board_name: read prop ro.build.product [%s]", data);
	}

	if ((! data [0]) || (strcmp (data, unknown) == 0))
	{
		memset (data, 0, sizeof (data));

		dlen = read_proc_cmdline ("androidboot.hardware", data, sizeof (data), "");

		fLOGD ("get_board_name: read cmdline androidboot.hardware [%s]", data);
	}

	if ((! data [0]) || (strcmp (data, unknown) == 0))
	{
		fLOGD ("get_board_name: default %s", unknown);

		strncpy (data, unknown, sizeof (data) - 1);
		data [sizeof (data) - 1] = 0;

		dlen = strlen (data);
	}

	strncpy (buf, data, len - 1);
	buf [len - 1] = 0;

	for (i = 0; buf [i]; i ++)
	{
		if (IS_VALID_NAME_CHAR (buf [i]))
			continue;

		buf [i] = 0;
		break;
	}

	fLOGD ("get_board_name: [%s] (%d)", buf, dlen);
	return buf;
}
