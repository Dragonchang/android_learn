

#define	LOG_TAG	"STT:sem"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <utils/Log.h>

#include "headers/sem.h"

int timed_wait (sem_t *plock, int ms)
{
	struct timespec abs_timeout;

	time_t sec;
	unsigned long nsec;

	for (sec = 0; ms >= 1000; ms -= 1000, sec ++);

	ms *= 1000000;

	clock_gettime (CLOCK_REALTIME, & abs_timeout);

	for (nsec = ms + (unsigned long) abs_timeout.tv_nsec; nsec >= 1000000000; nsec -= 1000000000, sec ++);

	abs_timeout.tv_sec += sec;
	abs_timeout.tv_nsec = (long) nsec;

	return sem_timedwait (plock, & abs_timeout);
}

