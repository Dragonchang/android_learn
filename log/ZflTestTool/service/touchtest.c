#define	LOG_TAG		"STT:touchtest"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/klog.h>
#include <sys/prctl.h>

#include "common.h"
#include "server.h"

#include "headers/process.h"

#define	VERSION	"1.2"
/*
 * 1.2	: support large dimension and read diag until EOF.
 * 1.1  : return fail item and add test ret to log.
 * 1.0	: delta/ref/ac test and log fail data.
 */

/* custom commands */
#define	LOG_GETPATH	":getpath:"
#define	LOG_SETPATH	":setpath:"
#define	LOG_GETPARAM	":getparam:"
#define	LOG_SETPARAM	":setparam:"
#define	LOG_RUN		":run:"
#define	LOG_STOP	":stop:"
#define	LOG_ISLOGGING	":islogging:"
#define	LOG_SETMODE	":setmode:"
#define	LOG_GETDATA	":getdata:"
#define	TEST_GETRET	":gettestret:"

#define	DATA_LEN	8192	/* 8192 is the socket read buffer in ClientSocket.java, 5396 ((28 * 5 + 2) * 38) is required for T1 */
#define	PARAM_LEN	50
#define	PARAM_NUM	3

//sync with TouchMonitorTest
#define SHOW_DATA       0
#define TEST_DELTA      1
#define TEST_REFERENCE  2
#define TEST_AC         3

static const char *diag_node = "/sys/android_touch/diag";

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t working = (pthread_t) -1;
static pthread_t timegen = (pthread_t) -1;

static int done = 0;
static int logging = 1; //default enable log for fail test ret
static char path [PATH_MAX] = "";
static char log_filename [PATH_MAX] = "";
static char data [DATA_LEN];

static char log_size [12] = "5";
static char log_rotate [12] = "99";

static char test_param [PARAM_LEN] = "0";

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

	prctl (PR_SET_NAME, (unsigned long) "touchtest:timegen", 0, 0, 0);

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

	timegen = (pthread_t) -1;

	DM ("end of timegen thread ...\n");
	return NULL;
}

static int delta_test (int row, int column, int* param, char* buf, char* ret_str)
{
        char *values[row * column];
        int i = 0;

        //LOGD("delta test");
        values[0] = strtok (buf, " ");
        while (values[i]) {
            if (abs(atoi(values[i])) > param[0]) {
                sprintf(ret_str, "Fail item: (%d,%d)", i/column + 1, i%column + 1);
                LOGE("Noise on item %d \n",i);
                return 1;
            }
            values[++i] = strtok (NULL, " ");
        }

        return 0;
}

static int ref_test (int row, int column, int* param, char* buf, char* ret_str)
{
        char *values[row * column];
        int delta;
        int i = 0;
        //LOGE("ref test with p1 %d p2 %d p3 %d", param[0], param[1], param[2]);

        values[0] = strtok (buf, " ");

        //calculate first row
        while (values[i] && (i < column)) {

            //item value out of range
            if ((atoi(values[i]) < param[0]) || (atoi(values[i]) > param[1]))
                goto test_fail;

            //delta with previous item
            if (i > 0) {
                delta = abs(atoi(values[i]) - atoi(values[i-1]));
                if (delta > param[2])
                    goto test_fail;
            }

            values[++i] = strtok (NULL, " ");
        }

        //calculate the followings
        while (values[i]) {

            //item value out of range
            if ((atoi(values[i]) < param[0]) || (atoi(values[i]) > param[1]))
                goto test_fail;

            //delta with previous item
            delta = abs(atoi(values[i]) - atoi(values[i-1]));
            if ((delta > param[2]) && (i % column != 0))
                goto test_fail;

            //delta with item in last row
            delta = abs(atoi(values[i]) - atoi(values[i-column]));
            if (delta > param[2])
                goto test_fail;


            values[++i] = strtok (NULL, " ");
        }

        return 0;

test_fail:;
          LOGE("test may fail on item %d \n",i);
          sprintf(ret_str, "Fail item: (%d,%d)", i/column + 1, i%column + 1);
          return 1;

}

static int ac_test (int row, int column, int* param, char* buf, char* ret_str)
{
        char *values[row * column];
        int ret = 0;
        int delta, i = 0, fail_item = 0;
        int max = -1000000, min = 1000000;
        //LOGE("ac test with p1 %d and p2 %d fail_item %d", param[0], param[2], fail_item);

        values[0] = strtok (buf, " ");
        while (values[i] && ((i / column) < param[0])) {
            delta = atoi(values[i]);
            //get max, min
            max = (delta > max)? delta: max;
            min = (delta < min)? delta: min;

            //check delta value;
            if ((!ret) && (abs(delta) > param[2])) {
                ret = 1;
                fail_item = i;
                LOGE("test may fail on item %d\n", i);
            }

            values[++i] = strtok (NULL, " ");
        }

        sprintf(ret_str, "%d:%d:(%d,%d)", max, min, fail_item/column + 1, fail_item%column + 1);
        return ret;
}

static int read_diag (char *buf, int len)
{
	int fd, idx, count, rlen;

	if ((fd = open_nointr (diag_node, O_RDONLY, DEFAULT_FILE_MODE)) < 0)
	{
		DM ("open_nointr [%s]: %s\n", diag_node, strerror (errno));
		return -1;
	}

	for (idx = 0, rlen = len; rlen > 0; idx += count, rlen = len - idx)
	{
		count = read_nointr (fd, & buf [idx], rlen);

		if (count <= 0)
		{
			if (count < 0)
			{
				DM ("read_nointr [%s]: %s\n", diag_node, strerror (errno));

				if (idx == 0)
					idx = -1;
			}
			break;
		}
	}

	close_nointr (fd);

	//DM ("read_diag() return %d\n", idx);
	return idx;
}

static void *thread_main (void *UNUSED_VAR (null))
{
	char buf [DATA_LEN];
	int count;
	long f_size, f_size_count = 0;
	int f_rotate;
	int dolog = 1;
	int fd = -1;

	int i = 0;
	int mode, row, column;
	int ret = 0;
	char parambuf [PARAM_LEN];
	int param [PARAM_NUM];

	prctl (PR_SET_NAME, (unsigned long) "touchtest:logger", 0, 0, 0);

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

		count = read_diag (buf, sizeof (buf));

		if (count < 0)
			break;

		if (! buf [0])
			continue;

		buf [count] = 0;

		pthread_mutex_lock (& data_lock);
		memcpy (data, buf, sizeof (data));

		// get test mode
		datatok (test_param, parambuf);
		mode = atoi (parambuf);

		if (mode == SHOW_DATA)
		{
			pthread_mutex_unlock (& data_lock);
			continue;
		}

		// get param
		for (i = 0; i < PARAM_NUM; i ++)
		{
			datatok (test_param, parambuf);
			param [i] = atoi (parambuf);
		}

		pthread_mutex_unlock (& data_lock);

		if (sscanf (buf, "Channel: %d * %d\n %[^]]", & row, & column, buf) != 3)
		{
			DM ("cannot read raw and column! no channel header? (sample: Channel: 20x32)\n");
			continue;
		}

		// DM ("get %d count %d * %d and mode %d p1 %d p2 %d p3 %d\n", count, row, column, mode, param [0], param [1], param [2]);

		switch (mode)
		{
		case TEST_DELTA:
			ret = delta_test (row, column, param, buf, parambuf);
			break;
		case TEST_REFERENCE:
			ret = ref_test (row, column, param, buf, parambuf);
			break;
		case TEST_AC:
			ret = ac_test (row, column, param, buf, parambuf);
			break;
		default:
			continue;
		}

		pthread_mutex_lock (& data_lock);
		sprintf (& data [count], ";%d;%d;:%d:%d:%d;%s", ret, mode, param [0], param [1], param [2], parambuf);
		pthread_mutex_unlock (& data_lock);

		if (ret && dolog)
		{
			/*
			 * write timestamp
			 */
			buf [0] = 0;

			pthread_mutex_lock (& time_lock);

			if (strcmp (curtime, prevtime) != 0)
			{
				sprintf (buf, "\n\nUpdate: [%s]\n%s\n", curtime, parambuf);
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
				i = write (fd, buf, strlen (buf));

				if (i < 0)
				{
					DM ("write time: %s\n", strerror (errno));
					break;
				}
			}

			/*
			 * log data
			 */
			pthread_mutex_lock (& data_lock);
			count = write (fd, data, count);
			pthread_mutex_unlock (& data_lock);
			f_size_count += count;

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

	working = (pthread_t) -1;

	DM ("end of touch thread ...\n");
	return NULL;
}

int touchtest_main (int server_socket)
{
	char buffer [DATA_LEN];
	char *ptr;
	char param [12] = "0";

	int ret = 0;
	int commfd = -1;

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
				strcpy (test_param, "0");
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
			else if (CMP_CMD (buffer, LOG_SETMODE))
			{
				MAKE_DATA (buffer, LOG_SETMODE);
				buffer [1] = '\n';
				buffer [2] = '\0';
				ret = file_mutex_write ((char *) diag_node, buffer, 2, O_WRONLY);
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, TEST_GETRET))
			{
				MAKE_DATA (buffer, TEST_GETRET);
				pthread_mutex_lock (& data_lock);
				strcpy (test_param, buffer);
				strcpy (buffer, data);
				pthread_mutex_unlock (& data_lock);

				if (! buffer [0])
				{
					strcpy (buffer, "no data.");
				}
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

	/* reset done flag */
	done = 0;
	logging = 1;

	return ret;
}
