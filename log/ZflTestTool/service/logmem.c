#define	LOG_TAG		"STT:logmem"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include "common.h"
#include "server.h"

#define	VERSION	"1.5"
/*
 * 1.5	: support another kmemleak node "/sys/kernel/debug/kmemleak".
 * 1.4	: append kmemleak logs.
 * 1.3	: save procrank output to another file.
 */

/* custom commands */
#define	LOG_GETINTERVAL	":getinterval:"
#define	LOG_SETINTERVAL	":setinterval:"
#define	LOG_GETPARAM	":getparam:"
#define	LOG_SETPARAM	":setparam:"
#define	LOG_GETPATH	":getpath:"
#define	LOG_SETPATH	":setpath:"
#define	LOG_RUN		":run:"
#define	LOG_STOP	":stop:"
#define	LOG_ISLOGGING	":islogging:"
#define	LOG_GETDATA	":getdata:"

typedef struct {
	long	total;
	long	free;
	long	MemTotal;
	long	SubTotal;
	long	MemFree;
	long	Buffers;
	long	Cached;
	long	Mlocked;
	long	AnonPages;
	long	Shmem;
	long	Slab;
	long	KernelStack;
	long	PageTables;
	long	VmallocAlloc;
	long    ION_Alloc;
} MEM;

#define	FORCE_INTERVAL_PROCRANK	30	/* > 30 seconds */
#define	FORCE_INTERVAL_KMEMLEAK	1800	/* > 30 minutes */

static const char *meminfo = "/proc/meminfo";
static long base = 0;

static int done = 0;
static int interval = 60;
static int unit = 0; // 0 represent KB, 1 represent MB.
static int proc_meminfo = 0;
static int proc_meminfo_full = 0;
static int proc_slabinfo = 0;
static int proc_vmallocinfo = 0;
static int proc_procrank = 0;
static int proc_procrank_interval_counter = FORCE_INTERVAL_PROCRANK;
static int proc_kmemleak = 0;
static int proc_kmemleak_interval_counter = FORCE_INTERVAL_KMEMLEAK;
static char *log_filename = NULL;
static char data [1024];
static char path [PATH_MAX] = LOG_DIR;

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// reference to frameworks/base/cmds/dumpstate/utils.c
int dump_file(const char *title, const char* path, FILE *redirect) {
    char *buffer = NULL;
    char stamp[80];

    buffer = (char *) malloc (sizeof(char) * 32768);
    if ((! buffer))
	{
		DM ("malloc failed!\n");
		return -1;
	}

    int fd = open_nointr(path, O_RDONLY, 0666);
    if (fd < 0) {
        int err = errno;
        if (title)
        {
			sprintf(stamp, "------ %s (%s) ------\n", title, path);
			fwrite(stamp, 1, strlen(stamp), redirect);
		}
        sprintf(stamp, "*** %s: %s\n", path, strerror(err));
        fwrite(stamp, 1, strlen(stamp), redirect);
        if (title)
        {
			sprintf(stamp, "\n");
			fwrite(stamp, 1, strlen(stamp), redirect);
		}
		free (buffer);
        return -1;
    }

    if (title) {
		sprintf(stamp, "------ %s (%s", title, path);
		fwrite(stamp, 1, strlen(stamp), redirect);
        time_t t = time (NULL);
        strftime(stamp, sizeof(stamp), ": %Y-%m-%d %H:%M:%S) ------\n", localtime (& t));
		fwrite(stamp, 1, strlen(stamp), redirect);
    }

    int newline = 0;
    for (;;) {
        int ret = read_nointr(fd, buffer, sizeof(buffer));
        if (ret > 0) {
            newline = (buffer[ret - 1] == '\n');
            ret = fwrite(buffer, ret, 1, redirect);
        }
        if (ret <= 0) break;
    }

    close_nointr(fd);
    free (buffer);
    sprintf(stamp, "\n");
    if (!newline) fwrite(stamp, 1, strlen(stamp), redirect);
    if (title) fwrite(stamp, 1, strlen(stamp), redirect);
    fflush (redirect);
    return 0;
}

static int mem_read (MEM *pmem)
{
	FILE *fp = NULL;
	char *buf = NULL, *p = NULL;
	static unsigned int buf_size = 2048;
	int i = 0;
	size_t count = 0;

	typedef struct mem_table_struct {
		const char *name;
		long *slot;
	} mem_table_struct;
	mem_table_struct mem_table[] = {
		{ "MemTotal:",		&pmem->MemTotal },
		{ "MemFree:",		&pmem->MemFree },
		{ "Buffers:",		&pmem->Buffers },
		{ "Cached:",		&pmem->Cached },
		{ "Mlocked:",		&pmem->Mlocked },
		{ "AnonPages:",		&pmem->AnonPages },
		{ "Shmem:",		&pmem->Shmem },
		{ "Slab:",		&pmem->Slab },
		{ "KernelStack:",	&pmem->KernelStack },
		{ "PageTables:",	&pmem->PageTables },
		{ "VmallocAlloc:",	&pmem->VmallocAlloc },
		{ "ION_Alloc:",		&pmem->ION_Alloc }
	};
	const int mem_table_count = sizeof(mem_table)/sizeof(mem_table_struct);

	buf = (char *) malloc (sizeof(char) * buf_size);
	if ((! buf))
	{
		DM ("malloc failed!\n");
		return -1;
	}
	memset (buf, 0, buf_size);

	fp = fopen_nointr (meminfo, "rb");
	if (! fp)
	{
		DM ("%s: %s\n", meminfo, strerror (errno));
		return -1;
	}

	while (! feof (fp))
	{
		count = fread (buf, 1, buf_size, fp);

		if (ferror (fp))
		{
			DM ("%s: %s\n", meminfo, strerror (errno));
			fclose_nointr (fp);
			free (buf);
			return -1;
		}
		if ( count < buf_size )
		{
			for ( i = 0; i < mem_table_count; ++i )
			{
				p = strstr(buf, mem_table[i].name);
				if (! p) {
					DM ("%s:%s wrong content!\n", meminfo, mem_table[i].name);
					*(mem_table[i].slot) = -1l;
					continue;
				}
				p += strlen(mem_table[i].name);
				sscanf (p, "%ld", mem_table[i].slot);
			}
			if ((pmem->MemTotal == 0) || (pmem->MemTotal == -1))
			{
				DM ("%s: total memory is zero!\n", meminfo);
				goto fileerr;
			}
			pmem->total = pmem->MemTotal;
			pmem->free = pmem->MemFree + pmem->Buffers + pmem->Cached;
			for ( pmem->SubTotal = 0, i = 1; i < mem_table_count; ++i )
			{
				if (*(mem_table[i].slot) != -1)
				{
					pmem->SubTotal += *(mem_table[i].slot);
				}
			}
		}
		else
		{
			fseek (fp , 0 , SEEK_SET);
			free (buf);
			buf_size *= 2;
			buf = (char*) malloc (sizeof(char)*buf_size);
			if ((! buf))
			{
				DM ("malloc failed!\n");
				fclose_nointr (fp);
				return -1;
			}
			memset (buf, 0, buf_size);
		}
	}

	fclose_nointr (fp);
	free (buf);
	return 0;

fileerr:;
	fclose_nointr (fp);
	free (buf);
	return -1;
}

static int log_main (void)
{
	static char buf [PATH_MAX];
	FILE *fplog = NULL;
	struct tm *ptm;
	time_t t;
	MEM mem;

	t = time (NULL);

	ptm = localtime (& t);

	if (! log_filename)
	{
		pthread_mutex_lock (& data_lock);
		if (! ptm)
		{
			sprintf (buf, "%smemlog_NA_%d.csv", path, getpid ());
		}
		else
		{
			sprintf (buf, "%smemlog_%04d%02d%02d_%02d%02d%02d.csv", path,
				ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
		}
		pthread_mutex_unlock (& data_lock);

		pthread_mutex_lock (& data_lock);
		log_filename = strdup (buf);
		pthread_mutex_unlock (& data_lock);

		fplog = fopen_nointr (buf, "wb");

		if (! fplog)
		{
			DM ("%s: %s\n", buf, strerror (errno));
			return -1;
		}

		/* csv title */
		if (unit == 0)
		{
			fprintf (fplog,
				"Time,"
				"Mem Total (kB),"
				"Mem Free (kB),"
				"Mem Free (%%),"
				"Mem Diff (kB),"
				"Mem Diff (%%),"
				"MemTotal (kB),"
				"SubTotal (kB),"
				"MemFree (kB),"
				"Buffers (kB),"
				"Cached (kB),"
				"Mlocked (kB),"
				"AnonPages (kB),"
				"Shmem (kB),"
				"Slab (kB),"
				"KernelStack (kB),"
				"PageTables (kB),"
				"VmallocAlloc (kB),"
				"ION_Alloc (kB)\n"
			);
		}
		else if (unit == 1)
		{
			fprintf (fplog,
				"Time,"
				"Mem Total (MB),"
				"Mem Free (MB),"
				"Mem Free (%%),"
				"Mem Diff (MB),"
				"Mem Diff (%%),"
				"MemTotal (MB),"
				"SubTotal (MB),"
				"MemFree (MB),"
				"Buffers (MB),"
				"Cached (MB),"
				"Mlocked (MB),"
				"AnonPages (MB),"
				"Shmem (MB),"
				"Slab (MB),"
				"KernelStack (MB),"
				"PageTables (MB),"
				"VmallocAlloc (MB),"
				"ION_Alloc (MB)\n"
			);
		}
	}
	else
	{
		fplog = fopen_nointr (log_filename, "a+b");

		if (! fplog)
		{
			DM ("%s: %s\n", log_filename, strerror (errno));
			return -1;
		}

		fseek (fplog, 0, SEEK_END);
	}

	if (! ptm)
	{
		strcpy (buf, "N/A");
	}
	else
	{
		sprintf (buf, "%02d%02d-%02d:%02d:%02d", ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
	}

	if (mem_read (& mem) < 0)
	{
		fclose_nointr (fplog);
		return -1;
	}

	if (base == 0)
		base = mem.free;

	/* csv data */
	if (unit == 0)
	{
		sprintf (& buf [strlen (buf)],
			","
			"%ld,"
			"%ld,"
			"%.2f,"
			"%ld,"
			"%.2f,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld,"
			"%ld\n",
			mem.total,
			mem.free,
			((float) mem.free / mem.total) * 100,
			mem.free - base,
			(((float) mem.free - base) / mem.total) * 100,
			mem.MemTotal,
			mem.SubTotal,
			mem.MemFree,
			mem.Buffers,
			mem.Cached,
			mem.Mlocked,
			mem.AnonPages,
			mem.Shmem,
			mem.Slab,
			mem.KernelStack,
			mem.PageTables,
			mem.VmallocAlloc,
			mem.ION_Alloc );
	}
	else if (unit == 1)
	{
		sprintf (& buf [strlen (buf)],
			","
			"%.1f,"
			"%.1f,"
			"%.2f,"
			"%.1f,"
			"%.2f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f,"
			"%.1f\n",
			(float) mem.total / 1024.0f,
			(float) mem.free / 1024.0f,
			((float) mem.free / mem.total) * 100,
			((float) mem.free - base) / 1024.0f,
			(((float) mem.free - base) / mem.total) * 100,
			(float) mem.MemTotal / 1024.0f,
			(float) mem.SubTotal / 1024.0f,
			(float) mem.MemFree / 1024.0f,
			(float) mem.Buffers / 1024.0f,
			(float) mem.Cached / 1024.0f,
			(float) mem.Mlocked / 1024.0f,
			(float) mem.AnonPages / 1024.0f,
			(float) mem.Shmem / 1024.0f,
			(float) mem.Slab / 1024.0f,
			(float) mem.KernelStack / 1024.0f,
			(float) mem.PageTables / 1024.0f,
			(mem.VmallocAlloc != -1) ? ((float) mem.VmallocAlloc / 1024.0f) : -1.0f,
			(mem.ION_Alloc != -1) ? ((float) mem.ION_Alloc / 1024.0f) : -1.0f);
	}

	fprintf (fplog, "%s", buf);

	pthread_mutex_lock (& data_lock);
	if ( unit == 0 )
	{
		sprintf (data, "Total: %8ld kB\nFree: %9ld kB (%.2f%%)\nDiff: %+9ld kB (%+.2f%%)\n\n"
			"MemTotal:     %9ld kB\n"
			"SubTotal:     %9ld kB\n"
			"MemFree:      %9ld kB\n"
			"Buffers:      %9ld kB\n"
			"Cached:       %9ld kB\n"
			"Mlocked:      %9ld kB\n"
			"AnonPages:    %9ld kB\n"
			"Shmem:        %9ld kB\n"
			"Slab:         %9ld kB\n"
			"KernelStack:  %9ld kB\n"
			"PageTables:   %9ld kB\n"
			"VmallocAlloc: %9ld kB\n"
			"ION_Alloc:    %9ld kB",
			mem.total,
			mem.free,
			((float) mem.free / mem.total) * 100,
			mem.free - base,
			(((float) mem.free - base) / mem.total) * 100,
			mem.MemTotal,
			mem.SubTotal,
			mem.MemFree,
			mem.Buffers,
			mem.Cached,
			mem.Mlocked,
			mem.AnonPages,
			mem.Shmem,
			mem.Slab,
			mem.KernelStack,
			mem.PageTables,
			mem.VmallocAlloc,
			mem.ION_Alloc);
	}
	else if ( unit == 1 )
	{
		sprintf (data, "Total: %5.1f MB\nFree: %6.1f MB (%.2f%%)\nDiff: %+6.1f MB (%+.2f%%)\n\n"
			"MemTotal:     %6.1f MB\n"
			"SubTotal:     %6.1f MB\n"
			"MemFree:      %6.1f MB\n"
			"Buffers:      %6.1f MB\n"
			"Cached:       %6.1f MB\n"
			"Mlocked:      %6.1f MB\n"
			"AnonPages:    %6.1f MB\n"
			"Shmem:        %6.1f MB\n"
			"Slab:         %6.1f MB\n"
			"KernelStack:  %6.1f MB\n"
			"PageTables:   %6.1f MB\n"
			"VmallocAlloc: %6.1f MB\n"
			"ION_Alloc:    %6.1f MB",
			(float) mem.total / 1024.0f,
			(float) mem.free / 1024.0f,
			((float) mem.free / mem.total) * 100,
			((float) mem.free - base) / 1024.0f,
			(((float) mem.free - base) / mem.total) * 100,
			(float) mem.MemTotal / 1024.0f,
			(float) mem.SubTotal / 1024.0f,
			(float) mem.MemFree / 1024.0f,
			(float) mem.Buffers / 1024.0f,
			(float) mem.Cached / 1024.0f,
			(float) mem.Mlocked / 1024.0f,
			(float) mem.AnonPages / 1024.0f,
			(float) mem.Shmem / 1024.0f,
			(float) mem.Slab / 1024.0f,
			(float) mem.KernelStack / 1024.0f,
			(float) mem.PageTables / 1024.0f,
			(mem.VmallocAlloc != -1) ? ((float) mem.VmallocAlloc / 1024.0f) : -1.0f,
			(mem.ION_Alloc != -1) ? ((float) mem.ION_Alloc / 1024.0f) : -1.0f);
	}
	pthread_mutex_unlock (& data_lock);

	if (proc_procrank)
	{
		proc_procrank_interval_counter += interval;

		if (proc_procrank_interval_counter > FORCE_INTERVAL_PROCRANK)
		{
			proc_procrank_interval_counter = 0;

			snprintf (buf, sizeof (buf), "procrank >> %s", log_filename);
			buf [sizeof (buf) - 1] = 0;

			if ((sizeof (buf) - strlen (buf)) > 16)
			{
				sprintf (& buf [strlen (buf) - 4], "_procrank.txt");
			}
			else
			{
				/* just replace "csv" with "prk" */
				buf [strlen (buf) - 3] = 'p';
				buf [strlen (buf) - 2] = 'r';
				buf [strlen (buf) - 1] = 'k';
			}

			char *pf = strchr (buf, '/');
			FILE *fpw = NULL;

			if (pf)
			{
				fpw = fopen_nointr (pf, "a+b" /* append */);
			}

			if (! fpw)
			{
				if (pf)
				{
					DM ("fopen_nointr [%s]: %s\n", pf, strerror (errno));
				}
				else
				{
					DM ("fopen_nointr [%s]: failed to parse filename!\n", buf);
				}
			}
			else
			{
				char stamp [80];
				time_t t = time (NULL);
				fseek (fpw, 0, SEEK_END);
				strftime (stamp, sizeof (stamp), "------ PROCRANK (%Y-%m-%d %H:%M:%S) ------\n", localtime (& t));
				stamp [sizeof (stamp) - 1] = 0;
				fwrite (stamp, 1, strlen (stamp), fpw);
				fclose_nointr (fpw);

				system (buf);
			}
		}
	}

	if (proc_kmemleak)
	{
		proc_kmemleak_interval_counter += interval;

		if (proc_kmemleak_interval_counter > FORCE_INTERVAL_KMEMLEAK)
		{
			proc_kmemleak_interval_counter = 0;

			strncpy (buf, log_filename, sizeof (buf) - 1);
			buf [sizeof (buf) - 1] = 0;

			if ((sizeof (buf) - strlen (buf)) > 16)
			{
				sprintf (& buf [strlen (buf) - 4], "_kmemleak.txt");
			}
			else
			{
				/* just replace "csv" with "kml" */
				buf [strlen (buf) - 3] = 'k';
				buf [strlen (buf) - 2] = 'm';
				buf [strlen (buf) - 1] = 'l';
			}

			FILE *fpw = fopen_nointr (buf, "a+b" /* append */);

			if (! fpw)
			{
				DM ("fopen_nointr %s: %s\n", buf, strerror (errno));
			}
			else
			{
				fseek (fpw, 0, SEEK_END);

				dump_file ("KERNEL MEMORY LEAK", (access ("/proc/kmemleak", R_OK) == 0) ? "/proc/kmemleak" : "/sys/kernel/debug/kmemleak", fpw);

				fclose_nointr (fpw);
			}
		}
	}

	if ((proc_meminfo == 1) || (proc_slabinfo == 1) || (proc_vmallocinfo == 1))
	{
		strcpy(buf, log_filename);
		buf [strlen(buf) - 3] = 't';
		buf [strlen(buf) - 2] = 'x';
		buf [strlen(buf) - 1] = 't';

		FILE *fpw;
		if ((fpw = fopen_nointr (buf, "a+b")) == NULL)
		{
			DM ("%s: %s\n", buf, strerror (errno));
			fflush (fplog);
			fclose_nointr (fplog);
			return -1;
		}
		fseek (fpw, 0, SEEK_END);

		if (proc_meminfo == 1)
		{
			if (proc_meminfo_full == 1)
			{
				dump_file("MEMORY INFO", "/proc/meminfo", fpw);
			}
			else
			{
				strftime(buf, sizeof(buf), "------ MEMORY INFO (Partial) (/proc/meminfo: %Y-%m-%d %H:%M:%S) ------\n", localtime (& t));
				fwrite(buf, 1, strlen(buf), fpw);
				fwrite(data, 1, strlen(data), fpw);
				sprintf(buf, "\n\n");
				fwrite(buf, 1, strlen(buf), fpw);
			}
		}
		if (proc_slabinfo == 1)		{ dump_file("SLAB INFO", "/proc/slabinfo", fpw); }
		if (proc_vmallocinfo == 1)	{ dump_file("VMALLOC INFO", "/proc/vmallocinfo", fpw); }

		fflush (fpw);
		fclose_nointr (fpw);
	}

	//base = mem.free;

	fflush (fplog);
	fclose_nointr (fplog);
	return 0;
}

#if 0
static void thread_cleanup (void *null)
{
	pthread_mutex_unlock (& data_lock);	/* ensure unlocked */

	pthread_mutex_lock (& data_lock);

	if (log_filename)
	{
		free (log_filename);
		log_filename = NULL;
	}

	pthread_mutex_unlock (& data_lock);
}
#endif

static void *thread_main (void *arg)
{
	struct timespec ts;

#if 0	// Bionic cannot use pthread_cleanup_pop() and pthread_cancel()
	pthread_cleanup_push (thread_cleanup, NULL);
#endif

	proc_procrank_interval_counter = FORCE_INTERVAL_PROCRANK;
	proc_kmemleak_interval_counter = FORCE_INTERVAL_KMEMLEAK;

	do
	{
		/* do log */
		if (log_main () < 0)
			break;

		/* set next timeout */
		pthread_mutex_lock (& data_lock);
		clock_gettime (CLOCK_REALTIME, & ts);
		ts.tv_sec += interval;
		pthread_mutex_unlock (& data_lock);

		pthread_cond_timedwait (& cond, & time_lock, & ts);
	}
	while (! done);

#if 0
#ifdef BUILD_AND
	pthread_exit (NULL);
#else
	pthread_cleanup_pop (1);
#endif
#endif

	pthread_mutex_lock (& data_lock);
	if (log_filename)
	{
		free (log_filename);
		log_filename = NULL;
	}
	data [0] = 0;
	pthread_mutex_unlock (& data_lock);

	/* reset base */
	base = 0;
	return NULL;
}

int logmem_main (int server_socket)
{
	pthread_t working = (pthread_t) -1;

	char buffer [PATH_MAX + 16];
	char tmp [PATH_MAX + 16];
	int commfd = -1;
	int ret = 0;

	pthread_mutex_lock (& time_lock);

	data [0] = 0;

	while (! done)
	{
		DM ("waiting connection ...\n");

		commfd = wait_for_connection (server_socket);

		if (commfd < 0)
		{
			DM ("accept client connection failed!\n");
			continue;
		}

		DM ("connection established.\n");

		for (;;)
		{
			memset (buffer, 0, sizeof (buffer));

			ret = read (commfd, buffer, sizeof (buffer));

			if (ret <= 0)
			{
				DM ("read command error (%d)! close connection!\n", ret);
				break;
			}

			buffer [sizeof (buffer) - 1] = 0;

			ret = 0;

			DM ("read command [%s].\n", buffer);

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				pthread_mutex_lock (& data_lock);
				done = 1;
				pthread_mutex_unlock (& data_lock);
				break;
			}
			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_GETDATA))
			{
				pthread_mutex_lock (& data_lock);
				strcpy (buffer, data);
				pthread_mutex_unlock (& data_lock);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				ret = (working == (pthread_t) -1) ? 0 : 1;
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, LOG_GETINTERVAL))
			{
				ret = interval;
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, LOG_GETPATH))
			{
				pthread_mutex_lock (& data_lock);
				if (! log_filename)
				{
					strcpy (buffer, path);
				}
				else
				{
					strcpy (buffer, log_filename);

					if ((proc_meminfo == 1) || (proc_slabinfo == 1) || (proc_vmallocinfo == 1))
					{
						strcat (buffer, "\n");
						strcat (buffer, log_filename);
						buffer [strlen(buffer) - 3] = 't';
						buffer [strlen(buffer) - 2] = 'x';
						buffer [strlen(buffer) - 1] = 't';
					}
				}
				pthread_mutex_unlock (& data_lock);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_SETINTERVAL))
			{
				/* change log interval */
				MAKE_DATA (buffer, LOG_SETINTERVAL);

				ret = atoi (buffer);

				if (ret > 0)
				{
					pthread_mutex_lock (& data_lock);
					interval = ret;
					pthread_mutex_unlock (& data_lock);
					ret = 0;
				}
				else
				{
					DM ("bad interval value (%d)!\n", ret);
					ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_SETPATH))
			{
				/* change log path */
				if (working == (pthread_t) -1)
				{
					MAKE_DATA (buffer, LOG_SETPATH);

					if (buffer [strlen (buffer) - 1] != '/')
						strcat (buffer, "/");

					if (access (buffer, R_OK | W_OK) < 0)
					{
						DM ("%s: %s\n", buffer, strerror (errno));
						ret = -1;
					}
					else
					{
						pthread_mutex_lock (& data_lock);
						strcpy (path, buffer);
						pthread_mutex_unlock (& data_lock);
					}
				}
				else
				{
					DM ("cannot change path while logging!\n");
					ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				/* start log */
				if (working == (pthread_t) -1)
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
						ret = -1;
				}
			}
			else if (CMP_CMD (buffer, LOG_STOP))
			{
				if (working != (pthread_t) -1)
				{
					/* quit thread */
					//pthread_cancel (working);
					pthread_mutex_lock (& data_lock);
					done = 1;
					pthread_mutex_unlock (& data_lock);
					pthread_cond_signal (& cond);
					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
				}
			}
			else if (CMP_CMD (buffer, LOG_GETPARAM))
			{
				pthread_mutex_lock (& data_lock);
				if (unit == 1) { strcpy (buffer, "1"); }
				else { strcpy (buffer, "0"); }
				strcat (buffer, ":");
				if (proc_meminfo == 1) { strcat (buffer, "1"); }
				else { strcat (buffer, "0"); }
				strcat (buffer, ":");
				if (proc_meminfo_full == 1) { strcat (buffer, "1"); }
				else { strcat (buffer, "0"); }
				strcat (buffer, ":");
				if (proc_slabinfo == 1) { strcat (buffer, "1"); }
				else { strcat (buffer, "0"); }
				strcat (buffer, ":");
				if (proc_vmallocinfo == 1) { strcat (buffer, "1"); }
				else { strcat (buffer, "0"); }
				strcat (buffer, ":");
				strcat (buffer, (proc_procrank == 1) ? "1" : "0");
				strcat (buffer, ":");
				strcat (buffer, (proc_kmemleak == 1) ? "1" : "0");
				pthread_mutex_unlock (& data_lock);
				ret = 1;
			}
			else if (CMP_CMD (buffer, LOG_SETPARAM))
			{
				MAKE_DATA (buffer, LOG_SETPARAM);
				datatok (buffer, tmp);
				if (tmp [0])
				{
					unit = atoi (tmp);
				}
				datatok (buffer, tmp);
				if (tmp [0])
				{
					proc_meminfo = atoi (tmp);
				}
				datatok (buffer, tmp);
				if (tmp [0])
				{
					proc_meminfo_full = atoi (tmp);
				}
				datatok (buffer, tmp);
				if (tmp [0])
				{
					proc_slabinfo = atoi (tmp);
				}
				datatok (buffer, tmp);
				if (tmp [0])
				{
					proc_vmallocinfo = atoi (tmp);
				}
				datatok (buffer, tmp);
				if (tmp [0])
				{
					proc_procrank = atoi (tmp);
				}
				datatok (buffer, tmp);
				if (tmp [0])
				{
					proc_kmemleak = atoi (tmp);
				}
				buffer [0] = 0;
			}
			else
			{
				DM ("unknown command [%s]!\n", buffer);
				ret = -1;
			}

			/* command response */
			if (ret != 1)
				sprintf (buffer, "%d", ret);

			DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close_nointr (commfd);
		commfd = -1;
	}

	pthread_mutex_unlock (& time_lock);

	if (working != (pthread_t) -1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	return 0;
}
