#ifndef _UEVENT_H_
#define _UEVENT_H_

#if __cplusplus
extern "C" {
#endif

#include "poll.h"

typedef struct {
	int fd;
	POLL poller;
} UEVENT;

extern UEVENT *uevent_open (void);
extern void uevent_close (UEVENT *ue);
extern int uevent_read (UEVENT *ue, char *buffer, int buffer_length, int timeout_ms);
extern void uevent_interrupt (UEVENT *ue);

/* simply get the uevent socket fd  */
extern int open_uevent_socket (void);

#if __cplusplus
}
#endif

#endif
