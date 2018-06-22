#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "headers/partition.h"

#define	LOG_TAG	"STT:mbsn"

#include <utils/Log.h>

#define	MBSN_LEN	(512)	/* basically 512 bytes is enough */

int get_mb_version (char *version, int len)
{
	PARTITION pn;

	unsigned int datalen;
	char *data = NULL, *ptr;
	int ret = -1;

	memset (& pn, 0, sizeof (pn));

	if ((! version) || (len <= 16))
		goto end;

	version [0] = 0;

	if ((data = malloc (MBSN_LEN)) == NULL)
		goto end;

	if (partition_open (& pn, "mfg") < 0)
		goto end;

	if (partition_read (& pn, 0x8000, MBSN_LEN, data) < 0)
		goto end;

	for (ptr = data; (*ptr != 0x00) && (*ptr != 0xff); ptr += 16)
	{
		ALOGD ("MB VERSION [%s]\n", ptr);

		datalen = (unsigned int) strlen (ptr);

		if (datalen >= (unsigned int) len)
			break;

		strcat (version, ptr);

		len -= datalen;

		if (len <= 1)
			break;

		strcat (version, "\n");
		len --;
	}

	ret = 0;

end:;
	if (data)
	{
		free (data);
	}
	partition_close (& pn);
	return ret;
}
