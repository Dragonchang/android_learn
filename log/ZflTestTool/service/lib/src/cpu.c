#define	LOG_TAG	"STT:cpu"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>

#include <cutils/log.h>

#include "headers/cpu.h"

static long long prev [4] = {-1,-1,-1,-1};	/* user, nice, system, idle */

float cpu_usage (void *UNUSED_VAR (reserved))
{
	FILE *fp = fopen ("/proc/stat", "rb");
	float ret = 0;
	char line [128];
	char *ph, *pt;
	int i;
	long long cur [4];	/* user, nice, system, idle */

	if (! fp)
		goto end;

	if (fgets (line, sizeof (line), fp) == NULL)
		goto end;

	if (strncmp (line, "cpu ", 4) != 0)
		goto end;

	for (i = 0, ph = & line [3]; i < 4; i ++)
	{
		cur [i] = -1;
		for (; *ph && isspace (*ph); ph ++);
		for (pt = ph; *pt && isdigit (*pt); pt ++);
		if (*pt) *pt ++ = 0;
		if (*ph) cur [i] = atoll (ph);
		ph = pt;
	}

	if ((prev [0] >= 0) && (prev [1] >= 0) && (prev [2] >= 0) && (prev [3] >= 0))
	{
		float usage, total;
		usage = (float) ((cur [0] - prev [0]) + (cur [1] - prev [1]) + (cur [2] - prev [2]));
		total = usage + (float) (cur [3] - prev [3]);
		ret = usage * 100 / total;
	}

	for (i = 0; i < 4; i ++) prev [i] = cur [i];

end:;
	if (fp) fclose (fp);
	return ret;
}
