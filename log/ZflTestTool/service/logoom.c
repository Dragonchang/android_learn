#define	LOG_TAG		"STT:logoom"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/inotify.h>
#include <pthread.h>
#include <semaphore.h>
#include <utils/Log.h>

#include "common.h"
#include "server.h"
#include "headers/sem.h"

/* for serviced */
#define	VERSION	"1.0"
/* custom commands */
#define	CMD_RUN		    ":run:"
#define	CMD_STOP	    ":stop:"
#define	CMD_ISRUNNING	":isrunning:"
#define CMD_RECONNECT   ":reconnect:"
#define	SOCKET_BUFFER_SIZE	(512)

#define OOM_ATTR "/sys/module/oom_kill/parameters/is_oom"
#define PROCRANK_CMD "procrank"
#define LOG_FILE_NAME "logoom"

static int done = 0;
static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
sem_t polling_clock;

static void *thread_main (void *UNUSED_VAR (null))
{
	size_t i;
	char buffer [2048];
	size_t count;
	struct inotify_event *ievent;
	FILE *fd = NULL;
	char log_filename [PATH_MAX] = "";

	prctl (PR_SET_NAME, (unsigned long) "logoom:monitor", 0, 0, 0);

	sem_init (&polling_clock, 0, 0);

	while (!done) {
		//read from attr
		if ((fd = fopen(OOM_ATTR, "r+")) == NULL) {
			LOGE ("open(%s) failed\n", OOM_ATTR);
			goto read_oom_attr_fail;
		}

		memset (buffer, 0, sizeof (buffer));
		if (fread(&buffer, 1, 1, fd) != 1) {
			LOGE ("read(%s) failed\n", OOM_ATTR);
			goto read_oom_attr_fail;
		}

		if (memcmp (buffer, "1", 1) != 0) {
			fclose(fd);
		} else {
			rewind(fd);
			if (fwrite("0", 1, 1, fd) != 1) {
				LOGE ("write(%s) failed error(%s)\n", OOM_ATTR, strerror(errno));
				goto read_oom_attr_fail;
			}
			fclose(fd);

			//dump procrank to log
			snprintf (buffer, sizeof (buffer), "%s", TAG_DATETIME);
			str_replace_tags(buffer);

			snprintf (log_filename, sizeof (log_filename), "%s" LOG_FILE_NAME "_%s.txt", LOG_DIR, buffer);

			snprintf (buffer, sizeof (buffer), PROCRANK_CMD " > %s", log_filename);
			buffer [sizeof (buffer) - 1] = 0;

			DM ("run [%s]\n", buffer);

			system (buffer);
		}
		timed_wait (& polling_clock, 60000); //1 min
	}

	return 0;

read_oom_attr_fail:;
		   if (fd != NULL) {
			   fclose(fd);
			   fd = NULL;
		   }

		   pthread_mutex_lock (&data_lock);
		   done = 1;
		   pthread_mutex_unlock (&data_lock);

	return 0;
}

int logoom_main (int server_socket)
{
	pthread_t working = (pthread_t) - 1;

	char buffer[SOCKET_BUFFER_SIZE + 15];
	int commfd = -1;
	int ret = 0;

	while (!done)
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
			DM ("test sizeof buffer %d", (int) sizeof (buffer));

			if (ret <= 0)
			{
				DM ("read command error (%d)! close connection!\n", ret);
				break;
			}

			buffer[sizeof (buffer) - 1] = 0;

			ret = 0;

			DM ("read command [%s].\n", buffer);

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{

                pthread_mutex_lock (&data_lock);
                done = 1;
                pthread_mutex_unlock (&data_lock);

                sem_post(&polling_clock);

				buffer[0] = 0;
				break;
			}
			else if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, CMD_RUN))
			{

				/* start logoom */
				if (working == (pthread_t) - 1)
				{
					if (pthread_create (&working, NULL, thread_main, NULL) != 0)
						ret = -1;
				}
				else
				{
					DM ("the oom monitor is already running");
				}

				buffer[0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_STOP))
			{
				if (working != (pthread_t) - 1)
				{

					/* quit thread */
					pthread_mutex_lock (&data_lock);
					done = 1;
					pthread_mutex_unlock (&data_lock);

					sem_post(&polling_clock);

					pthread_join (working, NULL);
					working = (pthread_t) - 1;
					buffer[0] = 0;
				}
			}
			else if (CMP_CMD (buffer, CMD_ISRUNNING))
			{
				ret = (working == (pthread_t) - 1) ? 0 : 1;
				sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, CMD_RECONNECT))
			{
				close (commfd);
				commfd = -1;

				commfd = wait_for_connection (server_socket);

				if (commfd < 0)
				{
					DM ("accept client connection failed!\n");
					continue;
				}

				DM ("reeonnection established.\n");
				buffer[0] = 0;

			}
			else
			{
				DM ("unknown command [%s]!\n", buffer);
				ret = -1;
				buffer[0] = 0;
			}

			/* command response */
			if (buffer[0] == 0)
				sprintf (buffer, "%d", ret);

			DM ("send response [%s]!\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) !=
					(ssize_t) strlen (buffer))
				DM ("send response [%s] to client failed!\n", buffer);

		}

		close (commfd);
		commfd = -1;
	}

	if (working != (pthread_t) - 1) {
		pthread_join (working, NULL);
		working = (pthread_t) - 1;
	}

	/* reset done flag */
	done = 0;

	return 0;
}
