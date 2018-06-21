#define LOG_TAG "STT:thrputclnt"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#include <errno.h>
#include <semaphore.h>
#include <utils/Log.h>

#include "common.h"
#include "server.h"

/* commands as java layer */
#define CMD_RUN		":run:"
#define CMD_STOP	":stop:"
#define CMD_SETPARAM	":setparam:"
#define CMD_ISRUNNING	":isrunning:"

//do next write before reading from server
#define SINGLE_THREAD 1

static int read_fd, write_fd;
static unsigned long long read_len, write_len;
static int end = 0;
static int done = 0; //service done

//sync init result, begine time, report delay time
static sem_t time_mutex;
//sync test end
static sem_t thread_end_mutex;
//send result back to ui layer
pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;

static int package_size = 1460;
static int report_delay_time = 10; //secs

static pthread_t working = (pthread_t) -1;

static char server_ip[15];
static int portno;

static int commfd = -1;

void* parseParameters (char* param)
{
	char temp[15];
	datatok (param, server_ip);

	datatok (param, temp);
	portno = atoi(temp);

	datatok (param, temp);
	package_size = atoi(temp);

	datatok (param, temp);
	report_delay_time = atoi(temp);

	return NULL;
}

void* readSocket (void* UNUSED_VAR (null))
{
	unsigned int received = 0;
	int stop = 0;
	char buf[package_size];

	while (1)
	{
		stop = end;

		if (stop)
			break;
		else {
			if ((received = read (read_fd, buf, sizeof(buf))) <= 0) {
				LOGE ("read error: %s\n", strerror(errno));
				break;
			}

			read_len += received;
		}
	}

	end = 1;
	sem_post (&thread_end_mutex);
	DM ("read socket thread end");
	return NULL;
}

void* writeSocket (void* UNUSED_VAR (null))
{
	int count = 0;
	int stop = 0;
	char buf[package_size];
	memset (buf, 9, sizeof(buf));
#if SINGLE_THREAD
	int received = 0;
#endif

	//notify reportRhread to record start time
	sem_post (&time_mutex);

	while (1)
	{
		stop = end;

		if (stop)
			break;
		else {
			//LOGE ("before write\n");
			if ((count = write (write_fd, buf, sizeof(buf))) <= 0) {
				LOGE ("write error: %s\n", strerror(errno));
				break;
			}

			write_len += count;
			//LOGE ("after write %d\n", count);

#if SINGLE_THREAD
			while ((read_len < write_len) && !end) {
				//LOGE ("before read\n");
				if ((received = read (read_fd, buf, sizeof(buf))) <= 0) {
					LOGE ("read error: %s\n", strerror(errno));
					end = 1;
					break;
				}

				read_len += received;
				//LOGE ("after read %d\n", received);
			}
#endif
		}
	}

	end = 1;

	sem_post (&thread_end_mutex);
	DM ("write socket thread end");
	return NULL;
}

void* reportThroughput (void* UNUSED_VAR (null))
{
	time_t start_time, end_time;
	struct tm ptm_start, ptm_end;
	int throughput = 0;
	unsigned long long report_len = 0;
	char buffer[1024] = "0";
	struct timespec ts;

	//wait connect result
	sem_wait (&time_mutex);
	if (!end) {
		DM ("report connected");
		sprintf (buffer, "%s", "cmd:connect ok");
		pthread_mutex_lock (&report_mutex);
		write (commfd, buffer, strlen(buffer));
		pthread_mutex_unlock (&report_mutex);
	}else {
		DM ("report connect fail");
		sprintf (buffer, "%s", "cmd:connect fail");
		pthread_mutex_lock (&report_mutex);
		write (commfd, buffer, strlen(buffer));
		pthread_mutex_unlock (&report_mutex);
		return NULL;
	}

	//wait writeThread ready
	sem_wait (&time_mutex);

	time (&start_time);
	localtime_r (&start_time, &ptm_start);
	DM ("Start test");

	ts.tv_sec = start_time;

	//record to file
	sprintf (buffer, DAT_DIR "RndisTp_%04d%02d%02d_%02d%02d%02d.txt",
			ptm_start.tm_year + 1900, ptm_start.tm_mon + 1, ptm_start.tm_mday,
			ptm_start.tm_hour, ptm_start.tm_min, ptm_start.tm_sec);
	FILE *tp_fd = fopen(buffer, "w");

	fprintf (tp_fd, "Start test at %04d%02d%02d %02d:%02d:%02d\n",
			ptm_start.tm_year + 1900, ptm_start.tm_mon + 1, ptm_start.tm_mday,
			ptm_start.tm_hour, ptm_start.tm_min, ptm_start.tm_sec );

	sprintf (buffer, "%s", "");

	while (1)
	{
		if (end)
			break;

		ts.tv_sec += report_delay_time;
		if (sem_timedwait (&time_mutex, &ts) == 0)
			break;

		time (&end_time);
		localtime_r (&end_time, &ptm_end);

		//ignore the increase of read_len during cal throughput
		throughput = (int) ((read_len - report_len) / ((float)report_delay_time / 2));
		report_len = read_len;

		sprintf (buffer, "[%02d:%02d:%02d] %.2f Kbps",
				ptm_end.tm_hour, ptm_end.tm_min, ptm_end.tm_sec, (float)throughput * 0.008);

		pthread_mutex_lock (&report_mutex);
		write (commfd, buffer, strlen(buffer));
		pthread_mutex_unlock (&report_mutex);

		fprintf (tp_fd, "%s\n", buffer);

	}

	//record test end time and average throughput
	time (&end_time);
	localtime_r (&end_time, &ptm_end);

	throughput = (int) (read_len / ((float)difftime(end_time, start_time) / 2));
	sprintf (buffer, "[%02d:%02d:%02d-%02d:%02d:%02d] %.2f Kbps: write %llu byte, read %llu byte",
			ptm_start.tm_hour, ptm_start.tm_min, ptm_start.tm_sec,
			ptm_end.tm_hour, ptm_end.tm_min, ptm_end.tm_sec, throughput * 0.008, write_len, read_len);

	pthread_mutex_lock (&report_mutex);
	write (commfd, buffer, strlen(buffer));
	pthread_mutex_unlock (&report_mutex);

	fprintf (tp_fd, "End test at %04d%02d%02d %02d:%02d:%02d\n",
			ptm_end.tm_year + 1900, ptm_end.tm_mon + 1, ptm_end.tm_mday,
			ptm_end.tm_hour, ptm_end.tm_min, ptm_end.tm_sec );
	fprintf (tp_fd, "%s\n", buffer);
	fclose (tp_fd);

	DM ("report thread end");
	return NULL;
}

static void *thread_main(void *UNUSED_VAR (data))
{
	struct sockaddr_in serv_addr;
	pthread_t reportThread = (pthread_t) -1;
	pthread_t writeThread = (pthread_t) -1;
	pthread_t readThread = (pthread_t) -1;

	end = 0;
	read_len = 0;
	write_len = 0;

	sem_init (&time_mutex, 0, 0);
	sem_init (&thread_end_mutex, 0, 0);

	if (pthread_create (&reportThread, NULL, reportThroughput, NULL) != 0) {
		LOGE ("ERROR creating reportThread");
		goto end;
	}

	//create socket write port
	if ((write_fd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		LOGE ("ERROR opening socket");
		goto end;
	}

	memset (&serv_addr, 0, sizeof(struct sockaddr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr (server_ip);
	serv_addr.sin_port = htons(portno + 1);

	if (connect (write_fd, (struct sockaddr*) &serv_addr, sizeof (serv_addr)) < 0) {
		LOGE ("ERROR connecting write port");
		goto end;
	}

	//create socket read port
	if ((read_fd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		LOGE ("ERROR opening socket");
		goto end;
	}

	serv_addr.sin_port = htons(portno);

	if (connect (read_fd, (struct sockaddr*) &serv_addr, sizeof (serv_addr)) < 0) {
		LOGE ("ERROR connecting read port");
		goto end;
	}

	//notify connect result
	sem_post (&time_mutex);

	if (pthread_create (&writeThread, NULL, writeSocket, NULL) != 0) {
		LOGE ("ERROR creating writeThread");
		goto end;
	}

#if !SINGLE_THREAD
	if (pthread_create (&readThread, NULL, readSocket, NULL) != 0) {
		LOGE ("ERROR creating readThread");
		goto end;
	}
#endif

	sem_wait (&thread_end_mutex);


end:;
    end = 1;

    sem_post(&time_mutex);

    close (write_fd);
    close (read_fd);
    write_fd = -1;
    read_fd = -1;

    pthread_join (writeThread, NULL);

#if !SINGLE_THREAD
    pthread_join (readThread, NULL);
#endif

    pthread_join (reportThread, NULL);

    DM ("read %llu byte and write %llu byte", read_len, write_len);

    //report to ui
    pthread_mutex_lock (&report_mutex);
    write (commfd, "cmd:test end", 12);
    pthread_mutex_unlock (&report_mutex);

    DM ("test end send");

    working = (pthread_t) -1;

    done = 1;
    return NULL;
}

int thrputclnt_main (int server_socket)
{
	char buffer [1024]; //command from java layer
	int ret = 0;

	while (!done) {
		DM ("waiting connection...\n");
		commfd = wait_for_connection (server_socket);

		if (commfd < 0) {
			DM ("accept client connection failed!\n");
			continue;
		}

		DM ("connection established.\n");

		for (;;) {
			memset (buffer, 0, sizeof (buffer));

			if (read (commfd, buffer, sizeof (buffer)) < 0) {
				DM ("read command error (%d)! close connection!\n", ret);
				break;
			}

			pthread_mutex_lock (&report_mutex);

			buffer [sizeof (buffer) - 1] = 0;

			ret = 0;

			DM ("read command [%s].\n", buffer);

			if (CMP_CMD (buffer, CMD_ISRUNNING)) {
				sprintf (buffer, "%d", (working == (pthread_t) -1 ? 0: 1));
			}else if (CMP_CMD (buffer, CMD_RUN)) {
				if (working == (pthread_t) -1) {
					MAKE_DATA (buffer, CMD_RUN);
					parseParameters(buffer);

					if (pthread_create (&working, NULL, thread_main, NULL) < 0) {
						DM ("pthread_create fail: %s\n", strerror (errno));
						ret = -1;
					}
				}else
					DM ("thrputclnt alreading running");

				buffer[0] = 0;

			} else if (CMP_CMD (buffer, CMD_STOP)) {
				if (working != (pthread_t) -1) {
					end = 1;
					sem_post (&thread_end_mutex);
				}
				sprintf (buffer, "%d", 0);

				//let reportThread could send msg
				pthread_mutex_unlock (&report_mutex);
				continue;

			} else if (CMP_CMD (buffer, CMD_ENDSERVER)) {
				if (working != (pthread_t) -1) {
					end = 1;
					sem_post (&thread_end_mutex);
				}
				done = 1;
				buffer[0] = 0;

				//let reportThread could send msg
				pthread_mutex_unlock (&report_mutex);
				break;
			} else {
				DM ("unkown command [%s]\n", buffer);
				ret  = -1;
				buffer[0] = 0;
			}

			if (buffer[0] == 0)
				sprintf (buffer, "%d", ret);

			DM ("send response [%s]\n", buffer);

			if (write (commfd, buffer, strlen(buffer)) != (ssize_t) strlen (buffer))
				DM ("send response [%s] to client fail!\n", buffer);

			//let reportThread could send msg
			pthread_mutex_unlock (&report_mutex);

		}
		close (commfd);
		commfd = -1;
		ret = 0;
	}

	DM ("thrputclnt is trying to end");
	if (working != (pthread_t) -1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;
	end = 0;

	DM ("thrputclnt is end");

	return ret;
}
