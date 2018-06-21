#define	LOG_TAG		"STT:datatok"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/input.h>

#include "../common.h"

int datatok (char *from, char *to)
{
	char *p;
	int len;

	if ((! from) || (! to))
		return -1;

	for (p = from; *p && (*p != ':'); p ++);

	if (! *p)
	{
		strcpy (to, from);
		from [0] = 0;
		return 0;
	}

	*p ++ = 0;
	strcpy (to, from);

	len = strlen (p);
	memmove (from, p, len);
	from [len] = 0;
	return 0;
}
