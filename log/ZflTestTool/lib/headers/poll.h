#ifndef	__SSD_POLL_H__
#define	__SSD_POLL_H__

#ifdef __cplusplus
extern "C" {
#endif

#define	POLL_PIPE_READ	0
#define	POLL_PIPE_WRITE	1
#define	POLL_INITIAL	{{-1,-1}}

typedef struct {
	int pipefds [2];
} POLL;

extern int poll_check_data (int fd);
extern int poll_is_opened (POLL *pl);
extern int poll_open (POLL *pl);
extern int poll_close (POLL *pl);
extern int poll_wait (POLL *pl, int fd, int timeout_ms);
extern int poll_break (POLL *pl);

extern int poll_multiple_wait (POLL *pl, int timeout_ms, int *fd, int count);

#ifdef __cplusplus
}
#endif

#endif
