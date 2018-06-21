#define	LOG_TAG		"STT:str"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>

#include "headers/str.h"

char *strtrim (char *str)
{
	char *ptr;
	if (str)
	{
		for (ptr = str + strlen (str) - 1; (ptr != str) && isspace (*ptr); ptr --);
		if (isspace (*ptr)) *ptr = 0; else *(ptr + 1) = 0;
		for (ptr = str; *ptr && isspace (*ptr); ptr ++);
		if (*ptr && (ptr != str)) memmove (str, ptr, strlen (ptr) + 1);
	}
	return str;
}

static void replace_datetime_tag (char *str)
{
	const char *tag_datetime = TAG_DATETIME;
	char buf [TAG_DATETIME_LEN + 1], *ptr;
	struct tm *ptm;
	time_t t;

	t = time (NULL);

	ptm = localtime (& t);

	for (; str;)
	{
		/* replace date time string */
		ptr = strstr (str, tag_datetime);

		if (! ptr)
			break;

		if (! ptm)
		{
			snprintf (buf, sizeof (buf), "RAND_%10d", rand ());
		}
		else
		{
			snprintf (buf, sizeof (buf), "%04d%02d%02d_%02d%02d%02d",
					ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
					ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
		}

		memcpy (ptr, buf, strlen (tag_datetime));
	}
}

void str_replace_tags (char *str)
{
	replace_datetime_tag (str);
}
