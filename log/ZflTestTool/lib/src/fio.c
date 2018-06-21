#define	LOG_TAG	"STT:fileio"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

#include <cutils/log.h>

#include "libcommon.h"
#include "headers/str.h"
#include "headers/dir.h"
#include "headers/fio.h"
#include "headers/poll.h"

int open_nointr (const char *pathname, int flags, mode_t mode)
{
	int ret = -1;

	for (;;)
	{
		ret = open (pathname, flags, mode);

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry open [%s]\n", strerror (errno), pathname);
				usleep (10000);
				continue;
			}

			fLOGE ("open [%s]: %s\n", pathname, strerror (errno));
		}

		break;
	}

	return ret;
}

int close_nointr (int fd)
{
	int ret = -1;

	for (;;)
	{
		ret = close (fd);

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry close fd [%d]\n", strerror (errno), fd);
				usleep (10000);
				continue;
			}

			fLOGE ("close fd %d: %s\n", fd, strerror (errno));
		}

		break;
	}

	return ret;
}

ssize_t read_nointr (int fd, void *buf, size_t count)
{
	ssize_t ret = -1;

	for (;;)
	{
		ret = read (fd, buf, count);

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry read fd [%d]\n", strerror (errno), fd);
				usleep (10000);
				continue;
			}

			fLOGE ("read fd %d: %s\n", fd, strerror (errno));
		}

		break;
	}

	return ret;
}

ssize_t read_timeout (int fd, void *buf, size_t count, int timeout_ms)
{
	POLL poller;

	ssize_t ret = -1;

	if (poll_open (& poller) < 0)
		return ret;

	for (;;)
	{
		ret = poll_wait (& poller, fd, timeout_ms);

		fLOGI_IF ("read_timeout: poll return %d", (int) ret);

		if (ret == 0)
		{
			fLOGW ("read_timeout: poll timeout\n");
			break;
		}

		if (ret < 0)
		{
			break;
		}

		ret = read (fd, buf, count);

		if (ret == 0)
		{
			fLOGW ("read_timeout: read empty, retry\n");
			continue;
		}

		if (ret < 0)
		{
			fLOGE ("read_timeout: read fd %d: %s\n", fd, strerror (errno));
			break;
		}

		break;
	}

	poll_close (& poller);
	return ret;
}

ssize_t write_nointr (int fd, const void *buf, size_t count)
{
	ssize_t ret = -1;

	for (;;)
	{
		ret = write (fd, buf, count);

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry write fd [%d]\n", strerror (errno), fd);
				usleep (10000);
				continue;
			}

			fLOGE ("write fd %d: %s\n", fd, strerror (errno));
		}

		break;
	}

	return ret;
}

FILE *fopen_nointr (const char *path, const char *mode)
{
	FILE *fp = NULL;

	for (;;)
	{
		fp = fopen (path, mode);

		if (fp == NULL)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry fopen [%s]\n", strerror (errno), path);
				usleep (10000);
				continue;
			}

			fLOGE ("fopen [%s]: %s\n", path, strerror (errno));
		}

		break;
	}

	return fp;
}

int fclose_nointr (FILE *fp)
{
	int ret = -1;

	for (;;)
	{
		ret = fclose (fp);

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry fclose fd [%d]\n", strerror (errno), fileno (fp));
				usleep (10000);
				continue;
			}

			fLOGE ("fclose fd %d: %s\n", fileno (fp), strerror (errno));
		}

		break;
	}

	return ret;
}

int statfs_nointr (const char *path, struct statfs *buf)
{
	int ret = -1;

	for (;;)
	{
		ret = statfs (path, buf);

		if (ret < 0)
		{
			if (errno == EINTR)
			{
				fLOGW ("%s, retry statfs [%s]\n", strerror (errno), path);
				usleep (10000);
				continue;
			}

			fLOGE ("statfs [%s]: %s\n", path, strerror (errno));
		}

		break;
	}

	return ret;
}

void file_close (FILEIO *pfio)
{
	fLOGI_IF ("file_close (0x%lX)", (long) pfio);

	if (pfio)
	{
		poll_close (& pfio->poller);
		if (pfio->fd >= 0) close_nointr (pfio->fd);
		free (pfio);
	}
}

FILEIO *file_open (const char *filename)
{
	FILEIO *ret = NULL;

	fLOGI_IF ("file_open (%s)", filename);

	ret = (FILEIO *) malloc (sizeof (FILEIO));

	fLOGI_IF ("file_open -> fd = 0x%lX", (long) ret);

	if (! ret)
		goto failed;

	memset (ret, 0xff, sizeof (FILEIO));

	if (poll_open (& ret->poller) < 0)
		goto failed;

	ret->fd = open_nointr (filename, O_RDWR, DEFAULT_FILE_MODE);

	if (ret->fd < 0)
		goto failed;

	goto end;

failed:;
	if (ret)
	{
		file_close (ret);
		ret = NULL;
	}
end:;
	return ret;
}

int file_read (FILEIO *pfio, char *buffer, int buffer_length, int timeout_ms)
{
	int nr, count;

	fLOGI_IF ("file_read (0x%lX)", (long) pfio);

	if ((! pfio) || (! buffer))
		return -1;

	memset (buffer, 0, buffer_length);

	for (;;)
	{
		nr = poll_wait (& pfio->poller, pfio->fd, timeout_ms);

		fLOGI_IF ("file_read -> poll return %d", nr);

		if (nr <= 0)
			break;

		count = read_nointr (pfio->fd, buffer, buffer_length);

		fLOGI_IF ("file_read -> (%d)[%s]", count, buffer);

		if (count <= 0)
		{
			nr = -1;
			break;
		}

		return count;
	}

	buffer [0] = 0;
	return nr;
}

int file_write (FILEIO *pfio, char *buffer, int buffer_length)
{
	fLOGI_IF ("file_write (0x%lX, \"%s\", %d)", (long) pfio, buffer, buffer_length);

	if (! pfio)
		return -1;

	return write_nointr (pfio->fd, buffer, buffer_length);
}

void file_interrupt (FILEIO *pfio)
{
	fLOGI_IF ("file_interrupt (0x%lX)", (long) pfio);

	if (! pfio)
		return;

	poll_break (& pfio->poller);
}

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int file_mutex_lock (void)
{
	return pthread_mutex_lock (& lock);
}

int file_mutex_unlock (void)
{
	return pthread_mutex_unlock (& lock);
}

int file_mutex_trylock (void)
{
	return pthread_mutex_trylock (& lock);
}

int file_mutex_write (char *path, const char *buffer, int len, int flags)
{
	int fd, ret = -1;

	if ((! path) || (! buffer))
		return ret;

	pthread_mutex_lock (& lock);

	fd = open_nointr (path, flags, DEFAULT_FILE_MODE);

	if (fd < 0)
	{
		fLOGD_IF ("open_nointr %s: %s\n", path, strerror (errno));
	}
	else
	{
		//fLOGD_IF ("write_nointr [%s]: [%s]\n", path, buffer);

		if (write_nointr (fd, buffer, len) < 0)
		{
			fLOGD_IF ("write_nointr %s: %s\n", path, strerror (errno));
		}
		else
		{
			fsync (fd);
			ret = 0;
		}

		close_nointr (fd);
	}

	pthread_mutex_unlock (& lock);
	return ret;
}

int file_mutex_read (char *path, char *buffer, int len)
{
	int fd, ret = -1;

	if ((! path) || (! buffer) || (len <= 0))
		return ret;

	pthread_mutex_lock (& lock);

	fd = open_nointr (path, O_RDONLY, DEFAULT_FILE_MODE);

	if (fd < 0)
	{
		fLOGD_IF ("open_nointr %s: %s\n", path, strerror (errno));
	}
	else
	{
		//fLOGD_IF ("read_nointr [%s]: buffer %d bytes\n", path, len);

		ret = read_nointr (fd, buffer, len);

		if (ret < 0)
		{
			fLOGD_IF ("read_nointr %s: %s\n", path, strerror (errno));
		}

		close_nointr (fd);
	}

	pthread_mutex_unlock (& lock);
	return ret;
}

long long file_size (const char *path)
{
	long long size = 0;
	int fd;

	if (! path)
		return size;

#if 0
	fd = open_nointr (path, O_RDONLY, DEFAULT_FILE_MODE);

	if (fd < 0)
	{
		fLOGD_IF ("open_nointr %s: %s\n", path, strerror (errno));
	}
	else
	{
		size = (long long) lseek64 (fd, 0, SEEK_END);

		if (size < 0)
		{
			fLOGD_IF ("lseek64 %s: %s\n", path, strerror (errno));
			size = 0;
		}

		close_nointr (fd);
	}
#else
	struct stat st;

	if (stat (path, & st) < 0)
	{
		fLOGD_IF ("stat %s: %s\n", path, strerror (errno));
	}
	else
	{
		size = (long long) st.st_blocks /* the number of blocks allocated to the file */ * 512 /* one block 512 bytes */;

		//fLOGD_IF ("%s: size=%lld, st.st_size=%lld\n", path, size, st.st_size);
	}
#endif

	return size;
}

long long file_mutex_length (const char *path)
{
	long long size;
	pthread_mutex_lock (& lock);
	size = file_size (path);
	pthread_mutex_unlock (& lock);
	return size;
}

static void try_rotate (const char *logpath, char *buffer, int len)
{
	struct stat st;

	if (stat (logpath, & st) < 0)
	{
		fLOGD_IF ("stat %s: %s\n", logpath, strerror (errno));
	}
	else if (st.st_size > 5000000 /* 5MB */)
	{
		snprintf (buffer, len, "%s.1", logpath);
		buffer [len - 1] = 0;

		if (rename (logpath, buffer) < 0)
		{
			fLOGD_IF ("rename %s to %s: %s\n", logpath, buffer, strerror (errno));
		}
	}
}

void file_log (const char *format, ...)
{
	const char *logpath = LOG_DIR LOG_DATA_EXT ".txt";

	va_list ap;
	char *ptr;
	int count, size = 1024;

	if ((ptr = malloc (size)) == NULL)
		return;

	if (access (LOG_DIR, X_OK) != 0)
	{
		dir_create_recursive (LOG_DIR);
	}

	snprintf (ptr, size, TAG_DATETIME " %-5d %-5d %-5d: ", getpid (), gettid (), getuid ());
	ptr [size - 1] = 0;
	str_replace_tags (ptr);
	count = strlen (ptr);

	va_start (ap, format);
	count = vsnprintf (& ptr [count], size - count, format, ap);
	ptr [size - 1] = 0;
	va_end (ap);

	if ((count > -1) && (count < size))
	{
		file_mutex_write ((char *) logpath, ptr, strlen (ptr), O_CREAT | O_RDWR | O_APPEND);
	}

	fLOGD_IF ("%s", ptr);

	try_rotate (logpath, ptr, size);

	free (ptr);
}

void file_log_command_output (const char *format, ...)
{
	const char *logpath = LOG_DIR LOG_DATA_EXT ".txt";

	va_list ap;
	char *ptr;
	int count, size = 1024;

	if ((ptr = malloc (size)) == NULL)
		return;

	if (access (LOG_DIR, X_OK) != 0)
	{
		dir_create_recursive (LOG_DIR);
	}

	snprintf (ptr, size, TAG_DATETIME " %-5d %-5d %-5d: RUN: ", getpid (), gettid (), getuid ());
	ptr [size - 1] = 0;
	str_replace_tags (ptr);
	count = strlen (ptr);

	va_start (ap, format);
	if (vsnprintf (& ptr [count], size - count, format, ap) < 0)
	{
		va_end (ap);
		free (ptr);
		return;
	}
	ptr [size - 2 /* reserve one char for newline */] = 0;
	strcat (ptr, "\n");
	va_end (ap);

	file_mutex_write ((char *) logpath, ptr, strlen (ptr), O_CREAT | O_RDWR | O_APPEND);

	memmove (ptr, & ptr [count], strlen (& ptr [count]) + 1);
	ptr [size - 1] = 0;
	count = strlen (ptr);
	if ((count > 0) && (ptr [count - 1] == '\n'))
	{
		ptr [-- count] = 0;
	}
	snprintf (& ptr [count], size - count, " 2>&1 >> %s", logpath);
	ptr [size - 1] = 0;

	pthread_mutex_lock (& lock);
	system (ptr);
	pthread_mutex_unlock (& lock);

	fLOGD_IF ("%s", ptr);

	try_rotate (logpath, ptr, size);

	free (ptr);
}

int file_copy (const char *path_from, const char *path_to, const char *filename)
{
	char buf [PATH_MAX];
	FILE *fpr, *fpw;
	size_t count, total;
	int ret, err;

	fLOGD_IF ("copy [%s%s] to [%s%s] ...\n", path_from, filename, path_to, filename);

	ret = FILE_COPY_SUCCEEDED;
	err = 0;

	snprintf (buf, sizeof (buf) - 1, "%s%s", path_from, filename);
	buf [sizeof (buf) - 1] = 0;

	if ((fpr = fopen (buf, "rb")) == NULL)
	{
		ret = FILE_COPY_ERROR_SOURCE;
		err = errno;
		fLOGE ("copy from %s: %s\n", buf, strerror (errno));
		goto end;
	}

	snprintf (buf, sizeof (buf) - 1, "%s%s", path_to, filename);
	buf [sizeof (buf) - 1] = 0;

	if ((fpw = fopen (buf, "wb")) == NULL)
	{
		ret = FILE_COPY_ERROR_DESTINATION;
		err = errno;
		fLOGE ("copy to %s: %s\n", buf, strerror (errno));
		fclose (fpr);
		goto end;
	}

	total = 0;

	while (! feof (fpr))
	{
		count = fread (buf, 1, sizeof (buf), fpr);

		if (ferror (fpr))
		{
			ret = FILE_COPY_ERROR_SOURCE;
			err = errno;
			fLOGE ("read from %s%s: %s\n", path_from, filename, strerror (errno));
			break;
		}

		if ((count > 0) && (count <= sizeof (buf)))
		{
			if (fwrite (buf, 1, count, fpw) != count)
			{
				ret = FILE_COPY_ERROR_DESTINATION;
				err = errno;
				fLOGE ("write to %s%s: %s\n", path_to, filename, strerror (errno));
				break;
			}
			total += count;
		}
	}

	/*
	 * make others accessible
	 */
	fchmod (fileno (fpw), DEFAULT_FILE_MODE);

	fclose (fpr);
	fclose (fpw);

	fLOGD_IF ("copied %u bytes.\n", (unsigned int) total);

end:;
	errno = err;
	return ret;
}
