
#include <string.h>
#include <time.h>

/*
 * Please include this source file instead of build this in makefile.
 * All functions in this file are static.
 */

#ifndef HTC_EXPIRE_DATE
#define HTC_EXPIRE_DATE	"none"
#endif

#ifndef	LOG_TAG
#include "utils/Log.h"
#endif

static int __d_index (const char *date, int *idx)
{
	int i, ret = 0;

	for (i = *idx;; i ++)
	{
		if (date [i] == 0)
		{
			*idx = i;
			break;
		}

		if (date [i] == '/')
		{
			*idx = ++ i;
			break;
		}

		if ((date [i] < '0') || (date [i] > '9'))
			return -1;

		ret = (ret * 10) + (date [i] - '0');
	}

	return ret;
}

static int is_expired (void)
{
	const char *date = HTC_EXPIRE_DATE;
	struct tm tm;
	time_t exp;
	int idx = 0;

	/*
	 * check the string "none" without keeping another copy.
	 */
	if ((date [0] == date [2]) && (date [1] == 'o') && (date [4] == 0) && (date [3] == 'e') && (date [2] == 'n'))
		return 0;

	memset (& tm, 0, sizeof (tm));

	tm.tm_year = __d_index (date, & idx);
	tm.tm_mon = __d_index (date, & idx);
	tm.tm_mday = __d_index (date, & idx);

	//LOGD ("exp: %d/%d/%d\n", tm.tm_year, tm.tm_mon, tm.tm_mday);

	if ((tm.tm_year <= 0) || (tm.tm_mon <= 0) || (tm.tm_mday <= 0))
		return 0;

	tm.tm_year -= 1900;
	tm.tm_mon -= 1;

	exp = mktime (& tm);

	//LOGD ("exp = %ld, cur = %ld", exp, time (NULL));

	if ((exp == (time_t) -1) || (exp <= time (NULL)))
		return 1;

	return 0;
}
