#ifndef	__SSD_SEM_H__
#define	__SSD_SEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <semaphore.h>

extern int timed_wait (sem_t *plock, int ms);

#ifdef __cplusplus
}
#endif

#endif
