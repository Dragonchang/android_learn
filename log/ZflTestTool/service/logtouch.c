#define	LOG_TAG		"STT:logtouch"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/klog.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>

#include "common.h"
#include "server.h"

#include "headers/process.h"

#define	VERSION	"1.3"
/*
 * 1.3	: Do not set thread id to -1 inside thread to prevent timing issue.
 * 1.2	: support large dimension and read diag until EOF.
 * 1.1	: support other tp devices.
 * 1.0	: log touch panel delta values.
 */

/* custom commands */
#define	LOG_GETPATH	":getpath:"
#define	LOG_SETPATH	":setpath:"
#define	LOG_GETPARAM	":getparam:"
#define	LOG_SETPARAM	":setparam:"
#define	LOG_RUN		":run:"
#define	LOG_STOP	":stop:"
#define	LOG_ISLOGGING	":islogging:"
#define	LOG_GETMODES	":getmodes:"
#define	LOG_SETMODE	":setmode:"
#define	LOG_GETDATA	":getdata:"

#define	IOCTL_ANALY_MAGIC	0xD2
#define	IOCTL_ANALY_INIT	_IOR (IOCTL_ANALY_MAGIC, 0x00, char)
#define	IOCTL_ANALY_BASE1	_IOR (IOCTL_ANALY_MAGIC, 0x01, unsigned char)
#define	IOCTL_ANALY_BASE2	_IOR (IOCTL_ANALY_MAGIC, 0x02, unsigned char)
#define	IOCTL_ANALY_BASE3	_IOR (IOCTL_ANALY_MAGIC, 0x03, unsigned char)
#define	IOCTL_ANALY_BASE4	_IOR (IOCTL_ANALY_MAGIC, 0x04, unsigned char)
#define	IOCTL_ANALY_OPFREQ	_IOR (IOCTL_ANALY_MAGIC, 0x05, char)
#define	IOCTL_ANALY_ACTIVE	_IOR (IOCTL_ANALY_MAGIC, 0x06, unsigned char)
#define	IOCTL_ANALY_FINGER	_IOR (IOCTL_ANALY_MAGIC, 0x07, unsigned char)
#define	IOCTL_ANALY_PEN		_IOR (IOCTL_ANALY_MAGIC, 0x08, char)

#define	DATA_LEN	8192	/* 8192 is the socket read buffer in ClientSocket.java, 5396 ((28 * 5 + 2) * 38) is required for T1 */

typedef struct _touch_entry {
	const char *name;
	const char *node;
	const char **modes;
	int mode;
	int h, v;
	void *data;
	int (*init) (struct _touch_entry *);
	int (*destroy) (struct _touch_entry *);
	int (*setmode) (struct _touch_entry *, int mode);
	int (*read) (struct _touch_entry *, char *buffer, int len);
} TOUCH;

#define	TP_ENTRY(n,p,l,i,d,s,r)	{n,p,l,0,-1,-1,NULL,i,d,s,r},
#define	TP_ENTRY_END		{NULL,NULL,NULL,0,-1,-1,NULL,NULL,NULL,NULL,NULL}

/************************************/
/*** Global *************************/
/************************************/

static int get_value (const char *buffer, const char *key, int *value)
{
	char v [64];
	const char *h, *t;
	int keylen = strlen (key);
	int ret = -1;

	for (h = buffer;; h += keylen)
	{
		h = strstr (h, key);

		/* no result */
		if (! h) break;

		/* filter invalid key */
		if (((h > buffer) && isalnum (*(h - 1))) || isalnum (*(h + keylen)))
			continue;

		/* seek to number */
		for (h += keylen; *h && (! isdigit (*h)); h ++);

		/* no number */
		if (! *h) break;

		/* seek to end of number */
		for (t = h; *t && isdigit (*t); t ++);

		/* signed */
		if ((*(h - 1) == '+') || (*(h - 1) == '-'))
			h --;

		/* fix length */
		if ((t - h) >= (int) sizeof (v))
		{
			t = h + sizeof (v) - 1;
		}

		/* copy */
		bzero (v, sizeof (v));
		memcpy (v, h, t - h);

		*value = atoi (v);

		DM ("parsed [%s]=[%d]\n", key, *value);

		ret = 0;
		break;
	}

	return ret;
}

static void get_modes (TOUCH *pt, char *buffer, int len)
{
	char *ptr;
	int i, mlen = 0;

	buffer [0] = 0;

	for (ptr = buffer, i = 0; pt->modes && (pt->modes [i] != NULL); ptr += mlen, i ++)
	{
		mlen = strlen (pt->modes [i]) + 1;

		if ((ptr + mlen - buffer) >= len)
			break;

		snprintf (ptr, len - (ptr - buffer), "%s\n", pt->modes [i]);
	}

	if (buffer [0] == 0)
	{
		snprintf (buffer, len, "Show Values\n");
	}
}

/************************************/
/*** DIAG ***************************/
/************************************/

static const char *diag_modes [] = {
	"Show Delta Values",	// 0
	"Show Reference Values",// 1
	NULL
};

static int diag_init (TOUCH *UNUSED_VAR (pt))
{
	return 0;
}

static int diag_destroy (TOUCH *UNUSED_VAR (pt))
{
	return 0;
}

static int diag_setmode (TOUCH *pt, int mode)
{
	char buffer [4];
	switch (mode)
	{
	default:
	case 0:
		pt->mode = 0;
		buffer [0] = '1'; // delta
		break;
	case 1:
		pt->mode = 1;
		buffer [0] = '2'; // reference
		break;
	case -1:
		/* a special case to reset touch diag, normally called before closing tool so we don't care pt->mode here */
		pt->mode = 0;
		buffer [0] = '0'; // reset
		break;
	}
	buffer [1] = '\n';
	buffer [2] = '\0';
	return file_mutex_write ((char *) pt->node, buffer, 2, O_WRONLY);
}

static int diag_read (TOUCH *pt, char *buffer, int len)
{
	int fd, idx, count, rlen;

	if ((fd = open_nointr (pt->node, O_RDONLY, DEFAULT_FILE_MODE)) < 0)
	{
		DM ("open_nointr [%s]: %s\n", pt->node, strerror (errno));
		return -1;
	}

	for (idx = 0, rlen = len; rlen > 0; idx += count, rlen = len - idx)
	{
		count = read_nointr (fd, & buffer [idx], rlen);

		if (count <= 0)
		{
			if (count < 0)
			{
				DM ("read_nointr [%s]: %s\n", pt->node, strerror (errno));

				if (idx == 0)
					idx = -1;
			}
			break;
		}
	}

	close_nointr (fd);

	//DM ("diag_read() return %d\n", idx);
	return idx;
}

/************************************/
/*** ANALYSIS ***********************/
/************************************/

static const char *analysis_modes [] = {
	"Show Base 1",	// 0
	"Show Base 2",	// 1
	"Show Base 3",	// 2
	"Show Base 4",	// 3
	"Show Active",	// 4
	"Show Finger",	// 5
	NULL
};

static int analysis_destroy (TOUCH *pt)
{
	if (pt->data)
	{
		free (pt->data);
		pt->data = NULL;
	}
	return 0;
}

static int analysis_init (TOUCH *pt)
{
	char buffer [128];

	int fd = open (pt->node, O_RDWR);

	if (fd < 0)
	{
		DM ("open %s: %s\n", pt->node, strerror (errno));
		return -1;
	}

	if (ioctl (fd, IOCTL_ANALY_INIT, buffer) < 0)
	{
		DM ("ioctl %s: %s\n", pt->node, strerror (errno));
		close (fd);
		return -1;
	}

	DM ("init: [%s]\n", buffer);

	/*
	 * the format is:
	 * "Fn=4, HAN=24, VAN=40\n"
	 */
	if (get_value (buffer, "HAN", & pt->h) < 0)
	{
		pt->h = -1;
	}
	if (get_value (buffer, "VAN", & pt->v) < 0)
	{
		pt->v = -1;
	}

	if ((pt->h <= 0) || (pt->v <= 0))
	{
		DM ("invalid value HAN=[%d], VAN=[%d]!\n", pt->h, pt->v);
		close (fd);
		return -1;
	}

	pt->data = malloc (pt->h * pt->v);

	if (! pt->data)
	{
		DM ("failed to allocate %dx%d=%d bytes!\n", pt->h, pt->v, pt->h * pt->v);
		close (fd);
		return -1;
	}

	close (fd);
	return 0;
}

static int analysis_setmode (TOUCH *pt, int mode)
{
	int max;

	for (max = 0; pt->modes && (pt->modes [max] != NULL); max ++);

	if ((mode < 0) || (mode >= max))
		mode = 0;

	pt->mode = mode;
	return 0;
}

/*
 * init:
 * 	"Fn=4, HAN=24, VAN=40\n"
 * opfreq:
 * 	"FreqID=1, Freq=31250\n"
 * pen:
 * 	"PenState:0,Th:6103,Mag:80,Pressure:0,X:0,Y:0,Btn:0\n"
 * --
 * info:
 * 	"24x40 Base1:\n"
 * 	"FreqID=1, Freq=31250\n"
 * 	"PenState:0,Th:6103,Mag:80,Pressure:0,X:0,Y:0,Btn:0\n"
 */
#define	INFO_LEN	128

static int analysis_read (TOUCH *pt, char *buffer, int len)
{
	const char *key;
	unsigned char *pdat;
	char *pbuf;
	int fd, func;
	int h, v;
	int dlen = pt->h * pt->v;
	int minlen = dlen * 4 + INFO_LEN;

	if (len < minlen)
	{
		DM ("not enough buffer length! (current=%d, needed %dx%dx4+%d=%d)\n", len, pt->h, pt->v, INFO_LEN, minlen);
		return -1;
	}

	switch (pt->mode)
	{
	default:
	case 0:
		func = IOCTL_ANALY_BASE1;
		key = "Base1";
		break;
	case 1:
		func = IOCTL_ANALY_BASE2;
		key = "Base2";
		break;
	case 2:
		func = IOCTL_ANALY_BASE3;
		key = "Base3";
		break;
	case 3:
		func = IOCTL_ANALY_BASE4;
		key = "Base4";
		break;
	case 4:
		func = IOCTL_ANALY_ACTIVE;
		key = "Active";
		break;
	case 5:
		func = IOCTL_ANALY_FINGER;
		key = "Finger";
		break;
	}

	fd = open (pt->node, O_RDWR);

	if (fd < 0)
	{
		DM ("open %s: %s\n", pt->node, strerror (errno));
		return -1;
	}

	sprintf (buffer, "%dx%d %s:\n", pt->h, pt->v, key);

	if (ioctl (fd, IOCTL_ANALY_OPFREQ, pt->data) < 0)
	{
		DM ("ioctl %s: %s\n", pt->node, strerror (errno));
		close (fd);
		return -1;
	}

	strcat (buffer, pt->data);

	if (ioctl (fd, IOCTL_ANALY_PEN, pt->data) < 0)
	{
		DM ("ioctl %s: %s\n", pt->node, strerror (errno));
		close (fd);
		return -1;
	}

	strcat (buffer, pt->data);

	if (ioctl (fd, func, pt->data) < 0)
	{
		DM ("ioctl %s: %s\n", pt->node, strerror (errno));
		close (fd);
		return -1;
	}

	close (fd);

	pdat = (unsigned char *) pt->data;
	pbuf = & buffer [strlen (buffer)];

#if 0
	/*
	 * v h h h h h
	 * v
	 * v
	 */
	for (v = 0; v < pt->v; v ++)
	{
		for (h = 0; h < pt->h; h ++)
		{
			sprintf (pbuf, "%03d ", *pdat);

			pbuf += 4;
			pdat ++;
		}

		*(pbuf - 1) = '\n';
	}
#endif
#if 0
	/*
	 * h v v v v v
	 * h
	 * h
	 */
	for (v = 0; v < pt->v; v ++)
	{
		for (h = 0; h < pt->h; h ++)
		{
			sprintf (pbuf, "%03d ", *(pdat + (h * pt->v) + v));

			pbuf += 4;
		}

		*(pbuf - 1) = '\n';
	}
#endif
#if 1
	/*
	 * h
	 * h
	 * h v v v v v
	 */
	for (v = 0; v < pt->v; v ++)
	{
		for (h = 0; h < pt->h; h ++)
		{
			sprintf (pbuf, "%03d ", *(pdat + ((pt->h - h - 1) * pt->v) + v));

			pbuf += 4;
		}

		*(pbuf - 1) = '\n';
	}
#endif
#if 0
	/*
	 * v v v v v h
	 *           h
	 *           h
	 */
	for (v = 0; v < pt->v; v ++)
	{
		for (h = 0; h < pt->h; h ++)
		{
			sprintf (pbuf, "%03d ", *(pdat + (h * pt->v) + (pt->v - v - 1)));

			pbuf += 4;
		}

		*(pbuf - 1) = '\n';
	}
#endif
#if 0
	/*
	 *           h
	 *           h
	 * v v v v v h
	 */
	for (v = 0; v < pt->v; v ++)
	{
		for (h = 0; h < pt->h; h ++)
		{
			sprintf (pbuf, "%03d ", *(pdat + ((pt->h - h - 1) * pt->v) + (pt->v - v - 1)));

			pbuf += 4;
		}

		*(pbuf - 1) = '\n';
	}
#endif

	return strlen (buffer);
}

/************************************/
/*** Supported Devices **************/
/************************************/

static TOUCH tp_devices [] = {
	TP_ENTRY ("diag", "/sys/android_touch/diag", diag_modes, diag_init, diag_destroy, diag_setmode, diag_read)
	TP_ENTRY ("analysis", "/dev/ntrig_analysis", analysis_modes, analysis_init, analysis_destroy, analysis_setmode, analysis_read)
	TP_ENTRY_END
};

static TOUCH *tp_device = NULL;

static TOUCH *probe_tp_device (void)
{
	TOUCH *ret;

	for (ret = tp_devices; ret && (ret->name != NULL); ret ++)
	{
		if (chmod (ret->node, LOG_FILE_MODE) < 0)
			continue;

		if (access (ret->node, R_OK) == 0)
			break;
	}

	if (ret && (ret->name != NULL))
		return ret;

	return NULL;
}

/************************************/
/*** Main ***************************/
/************************************/

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t working = (pthread_t) -1;
static pthread_t timegen = (pthread_t) -1;

static int done = 0;
static int logging = 0;
static char path [PATH_MAX] = "";
static char log_filename [PATH_MAX] = "";
static char data [DATA_LEN];

static char log_size [12] = "5";
static char log_rotate [12] = "99";

static int rotate_file (int rotate)
{
	char *pf1, *pf2;
	int i, fd;

	if (log_filename [0] == 0)
	{
		DM ("no file name set!\n");
		return -1;
	}

	pf1 = (char *) malloc (strlen (log_filename) + 16);
	pf2 = (char *) malloc (strlen (log_filename) + 16);

	if ((! pf1) || (! pf2))
	{
		DM ("malloc failed!\n");
		if (pf1) free (pf1);
		if (pf2) free (pf2);
		return -1;
	}

	for (i = rotate; i > 0; i --)
	{
		sprintf (pf1, "%s.%d", log_filename, i);

		if (i == 1)
		{
			sprintf (pf2, "%s", log_filename);
		}
		else
		{
			sprintf (pf2, "%s.%d", log_filename, i - 1);
		}

		if ((rename (pf2, pf1) < 0) && (errno != ENOENT))
		{
			DM ("cannot rename [%s] to [%s]: %s\n", pf2, pf1, strerror (errno));
			break;
		}
	}

	free (pf1);
	free (pf2);

	fd = open (log_filename, O_CREAT | O_RDWR | O_TRUNC, LOG_FILE_MODE);

	if (fd < 0)
	{
		DM ("failed to open log file [%s]: %s\n", log_filename, strerror (errno));
	}

	return fd;
}

static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

static char curtime [24] = "";
static char prevtime [24] = "";

static void *thread_timegen (void *UNUSED_VAR (null))
{
	struct tm *ptm;
	time_t t;

	prctl (PR_SET_NAME, (unsigned long) "logtouch:timegen", 0, 0, 0);

	for (; ! done;)
	{
		t = time (NULL);

		ptm = localtime (& t);

		pthread_mutex_lock (& time_lock);

		snprintf (curtime, sizeof (curtime), "%04d/%02d/%02d %02d:%02d:%02d",
				ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

		pthread_mutex_unlock (& time_lock);

		sleep (1);
	}

	DM ("end of timegen thread ...\n");
	return NULL;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	char buf [DATA_LEN];
	int count;
	long f_size, f_size_count = 0;
	int f_rotate;
	int dolog = 0;
	int fd = -1;

	prctl (PR_SET_NAME, (unsigned long) "logtouch:logger", 0, 0, 0);

	f_size = atol (log_size) * 1000000;
	f_rotate = atoi (log_rotate);

	if ((f_size <= 0) || (f_rotate <= 0))
	{
		DM ("invalid parameters! size = %ld, rotate = %d\n", f_size, f_rotate);
		return NULL;
	}

	for (; ! done;)
	{
		usleep (100 * 1000);

		pthread_mutex_lock (& data_lock);
		dolog = logging;
		pthread_mutex_unlock (& data_lock);

		if (dolog)
		{
			if ((fd < 0) || (f_size_count > f_size))
			{
				if (fd < 0)
				{
					/*
					 * create timestamp tag
					 */
					strcpy (buf, TAG_DATETIME);
					str_replace_tags (buf);

					/*
					 * make log file name
					 */
					pthread_mutex_lock (& data_lock);
					if (path [0])
					{
						snprintf (log_filename, sizeof (log_filename), "%stouch_%s.txt", path, buf);
					}
					else
					{
						if (dir_select_log_path (path, sizeof (path)) < 0)
						{
							strcpy (path, LOG_DIR);
						}

						snprintf (log_filename, sizeof (log_filename), "%stouch_%s.txt", path, buf);

						path [0] = 0;
					}
					log_filename [sizeof (log_filename) - 1] = 0;
					pthread_mutex_unlock (& data_lock);
				}
				else
				{
					close (fd);
				}

				fd = rotate_file (f_rotate);

				if (fd < 0)
				{
					log_filename [0] = 0;
					break;
				}

				f_size_count = 0;
			}

			/*
			 * write timestamp
			 */
			buf [0] = 0;

			pthread_mutex_lock (& time_lock);

			if (strcmp (curtime, prevtime) != 0)
			{
				sprintf (buf, "Update: [%s]\n", curtime);
				strcpy (prevtime, curtime);
			}

			pthread_mutex_unlock (& time_lock);

			/*
			 * if curtime is "", this will also be ""
			 */
			if (prevtime [0] == 0)
				continue;

			/*
			 * curtime and prevtime are different
			 */
			if (buf [0])
			{
				count = write (fd, buf, strlen (buf));

				if (count < 0)
				{
					DM ("write time: %s\n", strerror (errno));
					break;
				}
			}
		}
		else
		{
			if (fd >= 0)
			{
				close (fd);
				fd = -1;
			}
		}

		/*
		 * read touch data
		 */
		buf [0] = 0;

		if ((count = tp_device->read (tp_device, buf, sizeof (buf))) < 0)
			break;

		if (! buf [0])
			continue;

		buf [count] = 0;

		f_size_count += count;

		pthread_mutex_lock (& data_lock);
		memcpy (data, buf, sizeof (data));
		pthread_mutex_unlock (& data_lock);

		if (dolog)
		{
			count = write (fd, buf, count);

			if (count < 0)
			{
				DM ("write: %s\n", strerror (errno));
				break;
			}

			fsync (fd);
		}
	}

end:;
	if (fd >= 0)
	{
		close (fd);
		fd = -1;
	}

	pthread_mutex_lock (& data_lock);
	done = 1;
	log_filename [0] = 0;
	pthread_mutex_unlock (& data_lock);

	DM ("end of touch thread ...\n");
	return NULL;
}

int logtouch_main (int server_socket)
{
	char buffer [DATA_LEN];
	char *ptr;

	int ret = 0;
	int commfd = -1;

	tp_device = probe_tp_device ();

	if (! tp_device)
	{
		DM ("cannot find valid tp device!\n");
		return -1;
	}

	DM ("use [%s] node=[%s]\n", tp_device->name, tp_device->node);

	if (tp_device->init (tp_device) < 0)
	{
		DM ("failed to init tp device!\n");
		return -1;
	}

	bzero (data, sizeof (data));

	if (pthread_create (& timegen, NULL, thread_timegen, NULL) < 0)
	{
		DM ("pthread_create timegen: %s\n", strerror (errno));
		return -1;
	}

	if (pthread_create (& working, NULL, thread_main, NULL) < 0)
	{
		DM ("pthread_create: %s\n", strerror (errno));
		return -1;
	}

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

			////DM ("read command [%s].\n", buffer);

			if (! is_thread_alive (working))
			{
				working = (pthread_t) -1;
			}

			if (! is_thread_alive (timegen))
			{
				timegen = (pthread_t) -1;
			}

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				pthread_mutex_lock (& data_lock);
				done = 1;
				pthread_mutex_unlock (& data_lock);

				if (timegen != (pthread_t) -1)
				{
					/* quit thread */
					pthread_join (timegen, NULL);
					timegen = (pthread_t) -1;
					curtime [0] = 0;
					prevtime [0] = 0;
				}

				if (working != (pthread_t) -1)
				{
					/* quit thread */
					pthread_join (working, NULL);
					working = (pthread_t) -1;
				}
				break;
			}

			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, LOG_ISLOGGING))
			{
				pthread_mutex_lock (& data_lock);
				sprintf (buffer, "%d", logging);
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_GETDATA))
			{
				pthread_mutex_lock (& data_lock);
				strcpy (buffer, data);
				pthread_mutex_unlock (& data_lock);

				if (! buffer [0])
				{
					strcpy (buffer, "no data.");
				}
			}
			else if (CMP_CMD (buffer, LOG_GETPATH))
			{
				pthread_mutex_lock (& data_lock);
				if (log_filename [0])
				{
					strcpy (buffer, log_filename);
				}
				else if (path [0])
				{
					strcpy (buffer, path);
				}
				else
				{
					strcpy (buffer, LOG_DIR);
				}
				pthread_mutex_unlock (& data_lock);
			}
			else if (CMP_CMD (buffer, LOG_GETPARAM))
			{
				sprintf (buffer, "%s:%s", log_size, log_rotate);
			}
			else if (CMP_CMD (buffer, LOG_GETMODES))
			{
				get_modes (tp_device, buffer, sizeof (buffer));
			}
			else if (CMP_CMD (buffer, LOG_SETMODE))
			{
				MAKE_DATA (buffer, LOG_SETMODE);
				ret = atoi (buffer);
				ret = tp_device->setmode (tp_device, ret);
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_SETPATH))
			{
				/* change log path */
				if (! logging)
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
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_SETPARAM))
			{
				/* change parameters */
				if (! logging)
				{
					MAKE_DATA (buffer, LOG_SETPARAM);
					datatok (buffer, log_size);
					datatok (buffer, log_rotate);
				}
				else
				{
					DM ("cannot change parameters while logging!\n");
					ret = -1;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_RUN))
			{
				/* start log */
				pthread_mutex_lock (& data_lock);
				logging = 1;
				pthread_mutex_unlock (& data_lock);
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, LOG_STOP))
			{
				/* stop log */
				pthread_mutex_lock (& data_lock);
				logging = 0;
				pthread_mutex_unlock (& data_lock);
				buffer [0] = 0;
			}
			else
			{
				DM ("unknown command [%s]!\n", buffer);
				ret = -1;
				buffer [0] = 0;
			}

			/* command response */
			if (buffer [0] == 0)
				sprintf (buffer, "%d", ret);

			////DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close (commfd);
		commfd = -1;

		ret = 0;
	}

	if (working != (pthread_t) -1)
	{
		pthread_join (working, NULL);
	}

	if (timegen != (pthread_t) -1)
	{
		pthread_join (timegen, NULL);
	}

	curtime [0] = 0;
	prevtime [0] = 0;

	tp_device->destroy (tp_device);

	/* reset done flag */
	done = 0;
	logging = 0;

	return ret;
}
