#define	LOG_TAG	"STT:attr_table_switch"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>

#include "headers/board.h"
#include "headers/attr_table_switch.h"

int table_get (int *ref_count, ATTR_TABLE *table)
{
	ATTR_TABLE *uc;

	FILE *fp;
	char buffer [PROPERTY_VALUE_MAX];
	char board [16];

	ALOGD ("get ref = %d", *ref_count);

	for (uc = table; uc->node [0]; uc ++)
	{
		if (uc->node [0] != '/' /* take as property */)
		{
			property_get (uc->node, buffer, "");

			if (uc->save [0] == 0)
			{
				strncpy (uc->save, buffer, sizeof (uc->save));
				uc->save [sizeof (uc->save) - 1] = 0;
			}

			if (strstr (buffer, uc->on) == NULL)
			{
				int len = sizeof (buffer) - strlen (buffer);

				if ((buffer [0]) && (len > 0))
				{
					strncat (buffer, ",", len --);
				}

				strncat (buffer, uc->on, len);

				buffer [sizeof (buffer) - 1] = 0;

				property_set (uc->node, buffer);
			}

			ALOGD ("property: [%s][+:%s][s:%s][%s]", uc->node, uc->on, uc->save, buffer);
			continue;
		}

		if (uc->need_board_param)
		{
			snprintf (buffer, sizeof (buffer), "%s%s", uc->node, get_board_name (board, sizeof (board)));
			buffer [sizeof (buffer) - 1] = 0;
			fp = fopen (buffer, "w+");
			ALOGD ("attr: [%s][%s]", buffer, uc->on);
		}
		else
		{
			fp = fopen (uc->node, "w+");
			ALOGD ("attr: [%s][%s]", uc->node, uc->on);
		}

		if (! fp)
		{
			ALOGE ("  %s", strerror (errno));
			continue;
		}

		if (uc->save [0] == 0)
		{
			fread (& uc->save, 1, sizeof (uc->save), fp);
			uc->save [sizeof (uc->save) - 1] = 0;
		}

		if (fwrite (uc->on, 1, sizeof (uc->on), fp) <= 0)
		{
			ALOGE ("  %s", strerror (errno));
		}
		fclose (fp);
	}

	return  (++ *ref_count);
}

int table_put (int *ref_count, ATTR_TABLE *table)
{
	ATTR_TABLE *uc;

	FILE *fp;
	char buffer [PROPERTY_VALUE_MAX];
	char board [16];
	char *ptr;

	*ref_count --;

	ALOGD ("put ref = %d", *ref_count);

	if (*ref_count == 0) for (uc = table; uc->node [0]; uc ++)
	{
		if (uc->node [0] != '/' /* take as property */)
		{
			property_get (uc->node, buffer, "");

			if ((ptr = strstr (buffer, uc->on)) != NULL)
			{
				char *ptr2 = ptr + strlen (uc->on);

				if ((ptr > buffer) && (*(ptr - 1) == ','))
					ptr --;

				if (*ptr2 == 0)
				{
					*ptr = 0;
				}
				else if (*ptr2 == ',')
				{
					ptr2 ++;

					memmove (ptr, ptr2, strlen (ptr2) + 1);
				}

				buffer [sizeof (buffer) - 1] = 0;

				property_set (uc->node, buffer);
			}

			ALOGD ("property: [%s][-:%s][s:%s][%s]", uc->node, uc->on, uc->save, buffer);
			uc->save [0] = 0;
			continue;
		}

		if (uc->need_board_param)
		{
			snprintf (buffer, sizeof (buffer), "%s%s", uc->node, get_board_name (board, sizeof (board)));
			buffer [sizeof (buffer) - 1] = 0;
			fp = fopen (buffer, "wb");
			ALOGD ("attr: [%s][%s]", buffer, uc->save);
		}
		else
		{
			fp = fopen (uc->node, "wb");
			ALOGD ("attr: [%s][%s]", uc->node, uc->save);
		}

		if (! fp)
		{
			ALOGE ("  %s", strerror (errno));
			continue;
		}

		if (fwrite (uc->save, 1, sizeof (uc->save) - 1, fp) <= 0)
		{
			ALOGE ("  %s", strerror (errno));
		}
		uc->save [0] = 0;
		fclose (fp);
	}

	return *ref_count;
}
