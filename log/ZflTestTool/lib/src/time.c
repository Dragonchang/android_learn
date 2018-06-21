#define LOG_TAG	"STT:time"

#include <cutils/log.h>

#include <sys/time.h>
#include <time.h>

int get_native_time (int *yr, int *mh, int *dy, int *hr, int *me, int *sd, int *weekday)
{
	struct tm *ptm;
	time_t t;

	t = time (NULL);

	ptm = localtime (& t);

	if (! ptm)
	{
		ALOGE ("cannot convert to localtime!");
		return -1;
	}

	if (yr)	*yr = ptm->tm_year + 1900;
	if (mh)	*mh = ptm->tm_mon + 1;
	if (dy)	*dy = ptm->tm_mday;
	if (hr)	*hr = ptm->tm_hour;
	if (me)	*me = ptm->tm_min;
	if (sd)	*sd = ptm->tm_sec;

	if (weekday) *weekday = ptm->tm_wday;

	return 0;
}
