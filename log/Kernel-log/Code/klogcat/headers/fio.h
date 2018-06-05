

#ifndef _HTC_FILEIO_H_
#define _HTC_FILEIO_H_

#include "headers/pollbase.h"

#if __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statfs.h>

extern int open_nointr (const char *pathname, int flags, mode_t mode);
extern int close_nointr (int fd);
extern ssize_t read_nointr (int fd, void *buf, size_t count);
extern ssize_t write_nointr (int fd, const void *buf, size_t count);
extern FILE *fopen_nointr (const char *path, const char *mode);
extern int fclose_nointr (FILE *fp);
extern int statfs_nointr (const char *path, struct statfs *buf);
extern ssize_t read_timeout (int fd, void *buf, size_t count, int timeout_ms);

extern long long file_size (const char *path);

typedef struct {
	int	fd;
	POLL	poller;
} FILEIO;

extern FILEIO	*file_open	(const char *filename);
extern void	file_close	(FILEIO *pfio);
extern int	file_read	(FILEIO *pfio, char *buf, int len, int timeout_ms);
extern int	file_write	(FILEIO *pfio, char *buf, int len);
extern void	file_interrupt	(FILEIO *pfio);

extern int file_mutex_lock (void);
extern int file_mutex_unlock (void);
extern int file_mutex_trylock (void);
extern int file_mutex_write (char *path, const char *buffer, int len, int flags);
extern int file_mutex_read (char *path, char *buffer, int len);
extern long long file_mutex_length (const char *path);

extern void file_log (const char *format, ...);
extern void file_log_command_output (const char *format, ...);

#define	FILE_COPY_SUCCEEDED		(0)
#define	FILE_COPY_ERROR_SOURCE		(-1)
#define	FILE_COPY_ERROR_DESTINATION	(-2)

extern int file_copy (const char *path_from, const char *path_to, const char *filename);

#if __cplusplus
}
#endif

#endif

