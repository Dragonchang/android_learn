
#define	LOG_TAG		"logkmsg"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/klog.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include <cutils/sockets.h>

#include "headers/sem.h"
#include "headers/fio.h"
#include "headers/dir.h"
#include "headers/pollbase.h"
#include "headers/process.h"

#include "headers/common.h"
#include "headers/server.h"

#define	FILE_PREFIX	"kernel_" LOG_FILE_TAG

#define SHOW_DUMPDMESG_SIZE (0)

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t working = (pthread_t) -1;
static sem_t timed_lock;

static int knlfd = -1;
static int use_htc_dk_service = 0;
static int use_htc_dlk_service = 0;
static char kbuf [192000]; /* the size must > KLOG_BUF_LEN */
static char last [1024];

int m_nDone = 0;
char m_szLogFilename [PATH_MAX] = "";

int logkmsg_rotate_logs (void)
{
	int ret = -1;
	int err = 0;
	char bufferfile1 [PATH_MAX];
	char bufferfile2 [PATH_MAX];

	memset (bufferfile1, 0, sizeof(bufferfile1));
	memset (bufferfile2, 0, sizeof(bufferfile2));

	int i = m_nCount;

	for (; i > 0; --i)
	{
		snprintf (bufferfile2, sizeof (bufferfile2), "%s.%d", m_szLogFilename, i);

		if (i - 1 == 0)
		{
			snprintf (bufferfile1, sizeof (bufferfile1), "%s", m_szLogFilename);

			if (chmod (bufferfile1, LOG_FILE_MODE) != 0)
			{
				file_log (LOG_TAG ": chmod [%s] (errno=%d) : %s\n", bufferfile1, errno, strerror (errno));
			}
		}
		else
		{
			snprintf (bufferfile1, sizeof (bufferfile1), "%s.%d", m_szLogFilename, i - 1);

			FILE* fp = fopen (bufferfile1, "r");

			if (fp)
				fclose(fp);
			else
				continue;
		}

		err = rename (bufferfile1, bufferfile2);		// rename from file1 to file2

		file_log (LOG_TAG ": rotate logs rename from file1[%s] to file2[%s], err[%d].\n", bufferfile1, bufferfile2, err);

		if (err < 0 && errno != ENOENT)
		{
			file_log (LOG_TAG ": while rotating log files (errno=%d): %s.\n", errno, strerror (errno));
		}
	}

end:;

	return ret;
}

void logger_common_logdata_filename (const char *path_with_slash, const char *file_prefix, char *buffer, int len)
{
	SAFE_SPRINTF (buffer, len, "%s.%sdata." LOG_DATA_EXT, path_with_slash, file_prefix);
}

int logger_common_logdata_open (const char *name, const char *path_with_slash, const char *file_prefix)
{
	char datafile [PATH_MAX];
	char logfile [PATH_MAX];
	struct dirent *entry;
	DIR *dir;
	int fd;
	unsigned long i;

	LOGDATA_HEADER hdr;
	LOGDATA data;

	logger_common_logdata_filename (path_with_slash, file_prefix, datafile, sizeof (datafile));

	file_log (LOG_TAG ": datafile[%s].\n", datafile);

	if ((fd = open_nointr (datafile, O_RDWR | O_CREAT, LOG_FILE_MODE)) < 0)
	{
		file_log (LOG_TAG ": logdata open [%s]: %s\n", datafile, strerror (errno));
		return -1;
	}

	memset (& hdr, 0, sizeof (LOGDATA_HEADER));

	if (read_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		file_log (LOG_TAG ": logdata read header [%s]: %s\n", datafile, strerror (errno));
		goto fixdata;
	}

	if (hdr.magic != LOGDATA_MAGIC)
	{
		file_log (LOG_TAG ": logdata wrong magic [%s][%08lX] (not %08lX)! corrupted?\n", datafile, hdr.magic, (unsigned long) LOGDATA_MAGIC);
		goto fixdata;
	}

	if (hdr.entry_count != LOGDATA_COUNT)
	{
		file_log (LOG_TAG ": logdata invalid entry count [%s][%lu] (not %lu)! corrupted?\n", datafile, hdr.entry_count, (unsigned long) LOGDATA_COUNT);
		goto fixdata;
	}

	if (hdr.entry_size != sizeof (LOGDATA))
	{
		file_log (LOG_TAG ": logdata invalid entry size [%s][%lu] (not %lu)! corrupted?\n", datafile, hdr.entry_size, (unsigned long) sizeof (LOGDATA));
		goto fixdata;
	}

	if (hdr.header_size != sizeof (LOGDATA_HEADER))
	{
		file_log (LOG_TAG ": logdata invalid header size [%s][%lu] (not %lu)! corrupted?\n", datafile, hdr.header_size, (unsigned long) sizeof (LOGDATA_HEADER));
		goto fixdata;
	}

	if (hdr.index_head >= hdr.entry_count)
	{
		file_log (LOG_TAG ": logdata invalid index_head [%s][%lu] (> count=%lu)! corrupted?\n", datafile, hdr.index_head, hdr.entry_count);
		goto fixdata;
	}

	hdr.magic = (hdr.entry_count * hdr.entry_size) + hdr.header_size; // expected file size
	hdr.entry_size = (unsigned long) lseek (fd, 0, SEEK_END); //real file size
	hdr.header_size = sizeof (LOGDATA_HEADER);

	if (hdr.magic != hdr.entry_size)
	{
		file_log (LOG_TAG ": logdata invalid file size [%s][%lu] (not %lu)! corrupted?\n", datafile, hdr.entry_size, hdr.magic);
		goto fixdata;
	}

	goto end;

fixdata:;
	hdr.magic = LOGDATA_MAGIC;
	hdr.entry_count = LOGDATA_COUNT;
	hdr.entry_size = sizeof (LOGDATA);
	hdr.header_size = sizeof (LOGDATA_HEADER);
	hdr.total_size = 0;
	hdr.index_head = 0;
	hdr.index_tail = 0;

	file_log (LOG_TAG ": %s logdata create [%s], entry count [%lu], entry size [%lu], header size [%lu], expected file size [%lu]\n",
		name, datafile, hdr.entry_count, hdr.entry_size, hdr.header_size, (hdr.entry_count * hdr.entry_size) + hdr.header_size);

	lseek (fd, 0, SEEK_SET);

	if (ftruncate (fd, 0) < 0)
	{
		file_log (LOG_TAG ": logdata ftruncate [%s]: %s\n", datafile, strerror (errno));

		 // going on even failed to truncate file...
	}

	if (write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		file_log (LOG_TAG ": logdata write header [%s]: %s\n", datafile, strerror (errno));
		close (fd);
		return -1;
	}

	if (! file_prefix)
	{
		i = 0;
	}
	else
	{
		int len = strlen (file_prefix);

		if ((dir = opendir (path_with_slash)) == NULL)
		{
			file_log (LOG_TAG ": logdata opendir [%s]: %s\n", path_with_slash, strerror (errno));
			close (fd);
			return -1;
		}

		while ((entry = readdir (dir)) != NULL)
		{
			if ((strcmp (entry->d_name, ".") == 0) || (strcmp (entry->d_name, "..") == 0))
				continue;

			// if file_prefix is given, only the files start with file_prefix will be included
			if ((strncmp (file_prefix, entry->d_name, len) != 0) || (! IS_STORAGE_CODE (entry->d_name [len])))
				continue;

			SAFE_SPRINTF (logfile, sizeof (logfile), "%s%s", path_with_slash, entry->d_name);

			if (hdr.index_head >= hdr.entry_count)   // reach maximum count, ignore all rest files
			{
				errno = 0;
				unlink (logfile);
				file_log (LOG_TAG ": logdata unlink scanned file [%s][%s]\n", entry->d_name, strerror (errno));
				continue;
			}

			SAFE_SPRINTF (data.file, sizeof (data.file), "%s", entry->d_name);
			data.size = (unsigned long long) file_size (logfile);

			file_log (LOG_TAG ": logdata scanned [%s][%llu]\n", data.file, data.size);

			if (write_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
			{
				file_log (LOG_TAG ": logdata write data [%s]: %s\n", datafile, strerror (errno));
				close (fd);
				return -1;
			}

			hdr.total_size += data.size;
			hdr.index_head ++;
		}

		closedir (dir);

		i = hdr.index_head;
	}

	memset (& data, 0, sizeof (LOGDATA));

	for (; i < hdr.entry_count; i ++)
	{
		if (write_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
		{
			file_log (LOG_TAG ": logdata write empty data [%s]: %s\n", datafile, strerror (errno));
			close (fd);
			return -1;
		}
	}

	file_log (LOG_TAG ": logdata last offset = %lu\n", (unsigned long) lseek (fd, 0, SEEK_CUR));

	if (hdr.index_head >= hdr.entry_count)
		hdr.index_head = hdr.entry_count - 1;

	lseek (fd, 0, SEEK_SET);

	if (write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		file_log (LOG_TAG ": logdata write header [%s]: %s\n", datafile, strerror (errno));
		close (fd);
		return -1;
	}

end:;

	lseek (fd, 0, SEEK_SET);
	return fd;
}

int logger_common_logdata_add_file (int fd, const char *name, const char *path_with_slash, const char *log_filename)
{
	file_log (LOG_TAG ": log_filename[%s].\n", log_filename);

	LOGDATA_HEADER hdr;
	LOGDATA data;
	char logfile [PATH_MAX], *ptr;
	unsigned long offset;
	int ret = -1;

	if ((! log_filename) || (! log_filename [0]))
	{
		goto end;
	}

	ptr = strrchr (log_filename, '/');

	if (! ptr)
	{
		goto end;
	}

	ptr ++;

	lseek (fd, 0, SEEK_SET);

	if (read_nointr (fd, & hdr, sizeof (LOGDATA_HEADER)) != sizeof (LOGDATA_HEADER))
	{
		file_log (LOG_TAG ": logdata read header: %s\n", strerror (errno));
		goto end;
	}

	if (hdr.magic != LOGDATA_MAGIC)
	{
		file_log (LOG_TAG ": logdata invalid magic [0x%08lX]!\n", hdr.magic);
		goto end;
	}

	offset = hdr.index_head * sizeof (LOGDATA) + sizeof (LOGDATA_HEADER);

	if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
	{
		file_log (LOG_TAG ": logdata lseek offset=%lu: %s\n", offset, strerror (errno));
		goto end;
	}

	if (read_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
	{
		file_log (LOG_TAG ": logdata read data: %s\n", strerror (errno));
		goto end;
	}

	if (data.file [0])
	{
		file_log (LOG_TAG ": data.file[%d]\n", data.file [0]);

		hdr.index_head ++;

		if (hdr.index_head >= hdr.entry_count)
			hdr.index_head = 0;

		if (hdr.index_head == hdr.index_tail)
			hdr.index_tail ++;

		if (hdr.index_tail >= hdr.entry_count)
			hdr.index_tail = 0;

		offset = hdr.index_head * sizeof (LOGDATA) + sizeof (LOGDATA_HEADER);

		if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
		{
			file_log (LOG_TAG ": logdata lseek offset=%lu: %s\n", offset, strerror (errno));
			goto end;
		}

		if (read_nointr (fd, & data, sizeof (LOGDATA)) != sizeof (LOGDATA))
		{
			file_log (LOG_TAG ": logdata read data: %s\n", strerror (errno));
			goto end;
		}

		if (data.file [0])
		{
			if (hdr.total_size > data.size)
				hdr.total_size -= data.size;
			else
				hdr.total_size = 0;

			SAFE_SPRINTF (logfile, sizeof (logfile), "%s%s", path_with_slash, data.file);

			file_log (LOG_TAG ": logfile[%s]\n", logfile);

			if (access (logfile, F_OK) == 0)
			{
				errno = 0;
				unlink (logfile);
				file_log (LOG_TAG ": logdata unlink [%s] due to logdata full (head=%lu)! [%s]\n", logfile, hdr.index_head, strerror (errno));
			}
		}
	}

	data.size = (unsigned long long) file_size (log_filename);
	SAFE_SPRINTF (data.file, sizeof (data.file), "%s", ptr);

	file_log (LOG_TAG ": data.file[%s]\n", data.file);

	lseek (fd, (off_t) offset, SEEK_SET);
	write_nointr (fd, & data, sizeof (LOGDATA));

	hdr.total_size += data.size;

	file_log (LOG_TAG ": hdr.total_size[%lld]\n", hdr.total_size);

	lseek (fd, 0, SEEK_SET);
	write_nointr (fd, & hdr, sizeof (LOGDATA_HEADER));

	ret = 0;
end:;

	return ret;
}

int logger_common_generate_new_file (const char *name, const char *file_prefix, char *buffer, int len)
{
	char path_with_slash [PATH_MAX];
	char timestamp [TAG_DATETIME_LEN + 1];
	char c;
	int fd;

	snprintf (buffer, len, "%s", m_szPath);

	buffer [len - 1] = 0;

	file_log (LOG_TAG ": create new file, buffer[%s]\n", buffer);

	if ((fd = open_nointr (buffer, O_RDWR | O_CREAT, LOG_FILE_MODE)) < 0)
	{
		file_log (LOG_TAG ": open new file [%s] failed: %s\n", buffer, strerror (errno));
		return errno;
	}

	close_nointr (fd);

	char* dir = dirname(strdup(buffer));

	SAFE_SPRINTF (path_with_slash, sizeof (path_with_slash), "%s/", dir);

	file_log (LOG_TAG ": path_with_slash[%s].\n", path_with_slash);

	if ( (fd = logger_common_logdata_open (name, path_with_slash, file_prefix) ) < 0)
	{
		file_log (LOG_TAG ": open logdata failed! try going on ...\n");
	}
	else
	{
		logger_common_logdata_add_file (fd, name, path_with_slash, buffer);
		close_nointr (fd);
	}
	return 0;
}

static int read_from_htc_dk_service (char *buffer, int len)
{
	int socket_fd, count, rlen;

	for (count = 1; count <= 10; count ++)
	{
		// read htc_dk for kmsg
		if ((socket_fd = socket_local_client ("htc_dk", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM)) >= 0)
			break;

		file_log (LOG_TAG ": failed to connect to htc_dk service (%d)! %s\n", count, strerror (errno));

		sleep (1);
	}

	count = -1;

	if (socket_fd >= 0)
	{
		count = 0;

		// it may need multiple times to read data from socket
		while (1)
		{
			if ((rlen = read_nointr (socket_fd, & buffer [count], len - count)) < 0)
			{
				file_log (LOG_TAG ": failed to read data from htc_dk service! %s\n", strerror (errno));
				count = -1;
				break;
			}

			if (rlen == 0) /* no more data */
				break;

		#if SHOW_DUMPDMESG_SIZE
			file_log (LOG_TAG ": htc_dk read_nointr = %d\n", rlen);
		#endif

			count += rlen;

			if (count >= len)
			{
				file_log (LOG_TAG ": htc_dk read not enough buffer!\n");
				break;
			}
		}

		close_nointr (socket_fd);
	}

#if SHOW_DUMPDMESG_SIZE
	file_log (LOG_TAG ": htc_dk read_nointr total = %d\n", count);
#endif

	if (count > 0)
	{
		char *ptr;

		/*
		 * remove tailing useless new lines
		 */
		if (buffer [count - 1] == '\n')
		{
			for (ptr = & buffer [count - 1], rlen = 0; (ptr != buffer) && ((*ptr == '\r') || (*ptr == '\n')); ptr --, rlen ++);

			if ((*(ptr + 1) == '\n') && ((*(ptr + 2) == '\r') || (*(ptr + 2) == '\n')))
			{
				count -= rlen - 1;
			}
		}
		else
		{
			for (ptr = & buffer [count - 1], rlen = 0; (ptr != buffer) && (*ptr != '\n'); ptr --, rlen ++);

			if (*ptr == '\n')
			{
				count -= rlen;
			}
		}

	#if 0
		// remove incomplete header data
		if ((buffer [0] != '<') || (buffer [2] != '>'))
		{
			*head = strchr (buffer, '\n');

			if (*head) *head ++;
		}
	#endif

	#if SHOW_DUMPDMESG_SIZE
		file_log (LOG_TAG ": htc_dk count fixed = %d\n", count);
	#endif
	}

	return count;
}

static int read_from_htc_dlk_service (char *buffer, int len)
{
	int socket_fd, count, rlen;

	for (count = 1; count <= 10; count ++)
	{
		// read htc_dlk for lastkmsg
		if ((socket_fd = socket_local_client ("htc_dlk", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM)) >= 0)
			break;

		file_log (LOG_TAG ": failed to connect to htc_dlk service (%d)! %s\n", count, strerror (errno));

		sleep (1);
	}

	count = -1;

	if (socket_fd >= 0)
	{
		count = 0;

		// it may need multiple times to read data from socket
		while (1)
		{
			if ((rlen = read_nointr (socket_fd, & buffer [count], len - count)) < 0)
			{
				DM ("failed to read data from htc_dlk service! %s\n", strerror (errno));
				count = -1;
				break;
			}

			if (rlen == 0) /* no more data */
				break;

		#if SHOW_DUMPDMESG_SIZE
			file_log (LOG_TAG ": htc_dlk read_nointr = %d\n", rlen);
		#endif

			count += rlen;

			if (count >= len)
			{
				file_log (LOG_TAG ": htc_dlk read not enough buffer!\n");
				break;
			}
		}

		close_nointr (socket_fd);
	}

#if SHOW_DUMPDMESG_SIZE
	file_log (LOG_TAG ": htc_dlk read_nointr total = %d\n", count);
#endif

	if (count > 0)
	{
		char *ptr;

		// remove tailing useless new lines
		if (buffer [count - 1] == '\n')
		{
			for (ptr = & buffer [count - 1], rlen = 0; (ptr != buffer) && ((*ptr == '\r') || (*ptr == '\n')); ptr --, rlen ++);

			if ((*(ptr + 1) == '\n') && ((*(ptr + 2) == '\r') || (*(ptr + 2) == '\n')))
			{
				count -= rlen - 1;
			}
		}
		else
		{
			for (ptr = & buffer [count - 1], rlen = 0; (ptr != buffer) && (*ptr != '\n'); ptr --, rlen ++);

			if (*ptr == '\n')
			{
				count -= rlen;
			}
		}

	#if 0
		// remove incomplete header data
		if ((buffer [0] != '<') || (buffer [2] != '>'))
		{
			*head = strchr (buffer, '\n');

			if (*head) *head ++;
		}
	#endif

	#if SHOW_DUMPDMESG_SIZE
		file_log (LOG_TAG ": htc_dlk count fixed = %d\n", count);
	#endif
	}

	return count;
}

static void *thread_main (void *arg)
{
	const char *name = arg;

	int count;
	int fd = -1;
	char *head;
	char *ptr;
	int do_fsync = 1;
	int write_error = 0;

	struct stat filestat;

	prctl (PR_SET_NAME, (unsigned long) "klogcat:logger", 0, 0, 0);

	sem_init (& timed_lock, 0, 0);

	property_get ("debugtool.fsync", kbuf, "1");

	do_fsync = (kbuf [0] == '1');

	file_log (LOG_TAG ": file[%s], use_htc_dk_service[%d], use_htc_dlk_service[%d].\n", m_szLogFilename, use_htc_dk_service, use_htc_dlk_service);

	last [0] = 0;

	for (; ! m_nDone;)
	{
		if ( m_nFileToSave && (fd < 0) )
		{
			fd = open_nointr (m_szLogFilename, O_CREAT | O_RDWR | O_APPEND, LOG_FILE_MODE);

			if (fd < 0)
			{
				file_log (LOG_TAG ": failed to open [%s]: %s\n", m_szLogFilename, strerror (errno));
				DM ("failed to open [%s]: %s\n", m_szLogFilename, strerror (errno));
				m_szLogFilename [0] = 0;
				break;
			}
		}

		// read kernel log
		if (use_htc_dk_service)
		{
			count = read_from_htc_dk_service (kbuf, sizeof (kbuf) - 1);

			file_log (LOG_TAG ": use htc_dk service, count[%d].\n", count);

			if (count < 0)
			{
				file_log (LOG_TAG ": failed to read from htc_dk service\n");
				break;
			}
		}
		else if (use_htc_dlk_service)
		{
			count = read_from_htc_dlk_service (kbuf, sizeof (kbuf) - 1);

			file_log (LOG_TAG ": use htc_dlk service, count[%d].\n", count);

			if (count < 0)
			{
				file_log (LOG_TAG ": failed to read from htc_dlk service\n");
				break;
			}
		}
		else
		{
			if (m_nDumpType == 1)		// lastkmsg
			{
				count = read_nointr (knlfd, kbuf, sizeof (kbuf));

				file_log (LOG_TAG ": read lastkmsg, count[%d].\n", count);

			}
			else 						// kmsg
			{
				count = klogctl (KLOG_READ_ALL, kbuf, sizeof (kbuf) - 1);

				//file_log (LOG_TAG ": read kmsg, count[%d].\n", count);
			}

			if (count < 0)
			{
				file_log (LOG_TAG ": read failed: %s\n", strerror (errno));
				break;
			}
		}

		kbuf [count] = 0;

		//file_log (LOG_TAG ": scan new records, last[%d] line[%s].\n", last [0], last);

		// scan new records by last line
		head = kbuf;

		if (last [0])
		{
			// find the last line
			ptr = strstr (kbuf, last);

			if (ptr)
			{
				for (; *ptr && (*ptr != '\n') && (*ptr != '\r'); ptr ++);
				for (; *ptr && ((*ptr == '\n') || (*ptr == '\r')); ptr ++);

				if (! *ptr)
				{
					// no new log and wait 3 seconds
					timed_wait (& timed_lock, 3000);
					continue;
				}

				count -= (int) ((unsigned long) ptr - (unsigned long) kbuf);
				head = ptr;

				// buffer the last line for checking
				ptr += (count - 1);
			}
			else
			{
				file_log (LOG_TAG ": dmesg %d bytes, missing last[%s]\n", count, last);

				if (count == 0)
				{
					// it's possible that nothing read
					file_log (LOG_TAG ": it's possible that nothing read.\n");
					timed_wait (& timed_lock, 3000);
					continue;
				}

				ptr = head + count - 1;
			}
		}
		else
		{
			ptr = head + count - 1;
		}

		// buffer the last line for checking
		for (; (ptr != head) && ((*ptr == '\n') || (*ptr == '\r')); ptr --);
		for (; (ptr != head) && (*ptr != '\n') && (*ptr != '\r'); ptr --);

		if (ptr != head) ptr ++;

		memset (last, 0, sizeof (last));
		strncpy (last, ptr, sizeof (last) - 1);
		strtrim (last);

		if ( m_nFileToSave )
		{
			if ( access(m_szLogFilename, F_OK) == -1 )
			{
				file_log (LOG_TAG ": file cannot access.\n");
				fd = -1;
				continue;
			}

			count = write_nointr (fd, head, count);

			if (count < 0)
			{
				file_log (LOG_TAG ": write_nointr: %s\n", strerror (errno));
				write_error = 1;
				file_log (LOG_TAG ": use write_nointr, break, count < 0.\n");
				break;
			}

			fstat (fd, &filestat);

			if (filestat.st_size >=  m_nKBytes * 1024)
			{
				close_nointr (fd);
				fd = -1;

				logkmsg_rotate_logs ();
			}
		}
		else
		{
			printf("%s", head);

			if (m_nDumpAndExit)
			{
				file_log (LOG_TAG ": dump the log and then exit (don't block).\n");
				break;
			}
		}

		if (do_fsync)
		{
			fsync (fd);
		}

		timed_wait (& timed_lock, 3000);

	}// for (; ! m_nDone;)

end:;

	if (fd >= 0)
	{
		close_nointr (fd);
		fd = -1;
	}

	pthread_mutex_lock (& data_lock);
	m_nDone = 1;
	m_szLogFilename [0] = 0;
	pthread_mutex_unlock (& data_lock);

	working = (pthread_t) -1;

	file_log (LOG_TAG ": stop logging kernel messages ...\n");

	return NULL;
}

int logkmsg_start (const char *name)
{
	int ret = 0;

	if (working == (pthread_t) -1)
	{
		if (m_nDumpType == 0) 		// For kmsg
		{
			int count = klogctl (KLOG_READ_ALL, kbuf, sizeof (kbuf) - 1);

			file_log (LOG_TAG ": klogctl: count[%d].\n", count);

			if (count < 0)
			{
				file_log (LOG_TAG ": klogctl failed: %s, use htc_dk service instead.\n", strerror (errno));

				use_htc_dk_service = 1;
			}

			if (m_nFileToSave == 1)
			{
				if (logger_common_generate_new_file (name, FILE_PREFIX, m_szLogFilename, sizeof (m_szLogFilename)) != 0)
				{
					file_log (LOG_TAG ": failed to generate new file!\n");
					ret = -1;
				}
			}
		}
		else 						// For lastkmsg file node check
		{
			if (m_nFileToSave == 1)
			{
				snprintf (m_szLogFilename, sizeof (m_szLogFilename), "%s", m_szPath);
				m_szLogFilename [sizeof (m_szLogFilename) - 1] = 0;
			}

			if ( ( knlfd = open_nointr (LAST_KMSG_SOURCE, O_RDONLY, 0440) ) < 0 )
			{
				file_log (LOG_TAG ": failed to open node[%s]!\n", LAST_KMSG_SOURCE);

				if ( ( knlfd = open_nointr (LAST_KMSG_SOURCE_2, O_RDONLY, 0440) ) < 0 )
				{
					file_log (LOG_TAG ": failed to open node[%s], use use_htc_dlk_service instead!\n", LAST_KMSG_SOURCE_2);

					use_htc_dlk_service = 1;
				}
				else
				{
					file_log (LOG_TAG ": open_nointr: ready to open[%s].\n", LAST_KMSG_SOURCE_2);
				}
			}
			else
			{
				file_log (LOG_TAG ": open_nointr: ready to open[%s].\n", LAST_KMSG_SOURCE);
			}
		}

		if (pthread_create (& working, NULL, thread_main, (void *) name) < 0)
		{
			file_log (LOG_TAG ": pthread_create: %s\n", strerror (errno));
			ret = -1;
		}
	}

	return ret;
}

int logkmsg_stop (const char *UNUSED_VAR (name))
{
	pthread_mutex_lock (& data_lock);
	m_nDone = 1;
	pthread_mutex_unlock (& data_lock);

	if (working != (pthread_t) -1)
	{
		/* quit thread */
		sem_post (& timed_lock);
		pthread_join (working, NULL);
		working = (pthread_t) -1;
	}

	return 0;
}

