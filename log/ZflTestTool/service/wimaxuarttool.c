#define LOG_TAG		"STT:WimaxUARTTool"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <termios.h>

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/endian.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/endian.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <semaphore.h>
#include <ctype.h>
#include <utils/Log.h>

#include "common.h"
#include "server.h"

#include "libcommon.h"
#include "headers/ttytool.h"
#include "headers/board.h"
#include "headers/pclink.h"

//#define   UART_HS_SPEED_RATE 115200
#define     MAX_READ_BUFFER_SIZE 2048 //8192
#define     SOCKET_BUFFER_SIZE	(1024)

/* for serviced */
#define	VERSION	"1.0_20110318"
/* custom commands */
#define	CMD_RUN		":run:"
#define	CMD_STOP	":stop:"
#define	CMD_ISRUNNING	":isrunning:"
#define CMD_GETVER  ":getver:"

#define WIMAX_UART_PORT "/dev/ttyMSM2"

static int wimax_uart_set_speed (int Wdd, struct termios *ti, int speed);
static int wimax_uart_speed(int s);
static void wimax_write_sdlog(int size, char* buf);

static int done = 0;
static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
//int socket_fd;
int fd, Wdd;			/* dd - Device descriptor returned by uart_dev. */
int Wend = 0;
//int run_result = 0;
sem_t Wmutex, Wrfinish;//, Wwfinish;

int init_uart_port (const char *dev, int send_break)
{
    struct termios ti;

    DM("init_uart_port port=%s\n",dev);
    Wdd=open(dev, O_RDWR | O_NOCTTY);
    if(Wdd<0)
    {
        DM("Can't open serial port %s = %d \n",dev,Wdd);
        return -1;
    }
    DM("uart_open OK! Wdd=%d\n",Wdd);
    tcflush(Wdd,TCIOFLUSH);

    if(tcgetattr(Wdd,&ti)<0)
    {
        DM("Can't get port setting\n");
        return -1;
    }

    cfmakeraw(&ti);

    //ti.c_cflag |= CLOCAL;
    //ti.c_cflag &= ~CRTSCTS;

    DM("ti.c_cflag = 0x%x\n",ti.c_cflag);

    if(ti.c_cflag & CRTSCTS )
        DM("RTS/CTS enabled! \n");
    else
        DM("RTS/CTS disabled! \n");


    if(tcsetattr(Wdd,TCSANOW, &ti)<0)
    {
        DM("Can't set port setting! \n");
        return -1;
    }

    if(wimax_uart_set_speed(Wdd,&ti,115200)<0)
    {
        DM("Can't set initial baudrate! \n");
        return -1;
    }
    DM("wimax_uart_set_speed OK!\n");
    tcflush(Wdd, TCIOFLUSH);

    if (send_break)
    {
        tcsendbreak (Wdd,0);
        usleep(500000);
    }
    DM("init_uart_port end! Wdd=%d\n",Wdd);
    return Wdd;
}

static int wimax_uart_set_speed (int Wdd, struct termios *ti, int speed)
{
    cfsetospeed(ti, wimax_uart_speed(speed));
    cfsetispeed(ti, wimax_uart_speed(speed));
    return tcsetattr(Wdd, TCSANOW, ti);
}


//void* wimaxWriteUR (void *null)
static void wimaxWriteUR (void)
{

	int i, n, len;
	int stop = 0;
	unsigned char rbuf[18]= { 'c','m','d','\0','\"','s','h','o','w','v','e','r','s','i','o','n','\"','\r'};
	//unsigned char rbuf[1]= { 'c'};
	//unsigned char rbuf='c';

	DM("wimaxWriteUR starting...\n");
	//while (1)
	{

		LOGD ("starting write... %d\n", (int) sizeof (rbuf));
		{
			len = write (Wdd, &rbuf, sizeof (rbuf));
			LOGD ("write finished... \n");
			//memset (rbuf, 0, sizeof (rbuf));
		}
	}
	DM("wimaxWriteUR end len = %d\n",len);
}


void* wimaxReadUR (void *UNUSED_VAR (null))
{

	int i, n, len, received;
	uint16_t opcode;
	int stop = 0;

	unsigned char buf[MAX_READ_BUFFER_SIZE] = { 0 };
	unsigned char cmd[MAX_READ_BUFFER_SIZE] = { 0 };

	struct pollfd p;
	p.fd = Wdd;
	p.events = POLLIN;

	pthread_detach (pthread_self ());

	while (1)
	{
		//DM("wimaxReadUR while starting...\n");
		sem_wait (&Wmutex);
		stop = Wend;
		sem_post (&Wmutex);

		if (stop)
		{
			//DM("stop wimaxReadUR\n");
			sem_post (&Wrfinish);
			break;		//this thread finish right here
		}
		else
		{
			//DM("start polling....\n");
			while ((n = poll(&p, 1, 1000)) < 0)
				;
			//DM("polling...%d\n",n);
			if(!n) continue; //pollin timeout
		}
		DM("reading UR...\n");
		received = read (Wdd, &cmd, sizeof (cmd));
		LOGE("RECEIVED %d byte\n",received);
		LOGD("receive command: %s\n",cmd);
		wimax_write_sdlog (received, (char *) cmd);
		//usleep(50000);


	}
	return NULL;
}


static void *thread_main(void *UNUSED_VAR (null))
{
    char uart_path[50];
    pthread_t WreadThread;//, WwriteThread ;
    int read_len;
    unsigned char rbuf[MAX_READ_BUFFER_SIZE];

    strcpy(uart_path, WIMAX_UART_PORT );
    //Step 1: Set HW switch & UART3 GPIO
    //Step 2: Init UART port
    if(init_uart_port(uart_path,0)<0)
    {
        DM("Can't initialize device! \n");
        return (void *)1;
    }

	sem_init (&Wmutex, 0, 1);
	sem_init (&Wrfinish, 0, 0);
	//sem_init (&Wwfinish, 0, 0);
	//wimaxWriteUR();
	pthread_create (&WreadThread, NULL, wimaxReadUR, NULL);

	//run_result = 1;

	while (!Wend){
		usleep(100000);
	}
	DM("thread_main while loop exit! \n");
	/* quit read/write thread */
	sem_wait (&Wrfinish);
	//sem_wait (&Wwfinish);

	//UART_exit ();

		//close bt uart
	DM("thread_main exit! \n");
	close(Wdd);
    return NULL;
}

int wimaxuarttool_main(int server_socket)
{
    char buffer[SOCKET_BUFFER_SIZE + 16];
    int commfd = -1;
    int ret = 0;
    pthread_t working = (pthread_t) -1 ;

    while(! done)
    {
        DM("start while loop...\n");
        //Connect socket server
        DM("waiting connection...\n");
        commfd = wait_for_connection(server_socket);

        if (commfd < 0)
        {
            DM("accept client connection failed!\n");
            continue;
        }
        DM("connection established. Done=[%d]\n",done);


        for(;;)
        {
            //socket buffer
            memset(buffer,0,sizeof(buffer));
            ret = read (commfd,buffer,sizeof(buffer));

            if(ret <= 0)
            {
                DM("read command error(%d)! close connection!\n",ret);
                break;
            }

            buffer[sizeof(buffer)-1]=0;
            ret =0;

			DM ("read command [%s].\n", buffer);

            if(CMP_CMD(buffer,CMD_RUN))
            {
                if(working == (pthread_t)-1)
                {
		    Wend = 0;
                    if(pthread_create(& working, NULL, thread_main,NULL)<0)
                        ret = -1;
                }else
		{
		    DM ("wimaxuarttool is already running\n");
		}
                buffer[0] = 0;
            }else if(CMP_CMD(buffer,CMD_STOP))
            {
                if(working != (pthread_t) -1)
                {
                    // quit thread
		    Wend = 1;
                    pthread_mutex_lock(&data_lock);
                    done = 1;
                    pthread_mutex_unlock(&data_lock);

                    pthread_join(working, NULL);
                    working = (pthread_t) -1;
                    done = 0;
                    buffer[0]=0;
                }
            }else if(CMP_CMD(buffer,CMD_ISRUNNING))
            {
                ret = (working == (pthread_t)-1) ? 0 : 1 ;
                sprintf (buffer,"%d",ret);
            }else if(CMP_CMD(buffer,CMD_GETVER))
            {
                strcpy(buffer, VERSION);
            }else
            {
                DM("unknown command[%s]!\n",buffer);
                ret = -1;
                buffer[0]=0;
            }

            // command response
            if(buffer[0]==0)
            {
                DM("buffer[0]==0");
                sprintf(buffer,"%d",ret);
            }
            DM("send response [%s].\n",buffer);

            if(write(commfd,buffer,strlen(buffer)) !=(ssize_t)strlen(buffer))
            {
                DM("send response [%s] to client failed!\n",buffer);
            }

            DM("FOR END. Done=[%d]\n",done);

        }

        close(commfd);
        commfd = -1;
        DM("WHILE END. Done=[%d]\n",done);

    }

    //reset done flag to
    done=0;
    return 0;
}

static int wimax_uart_speed(int s)
{
    switch(s)
    {
        case 115200:
            DM("Baud rate = 115200!\n");
            return B115200;
        default:
            DM("Default Baud rate = 57600!\n");
            return B57600;
    }
}

static void wimax_write_sdlog(int size, char* buf)
{
    char *LOGFILE_htclog="/data/zfllog/wimax_uart.txt";
    char *LOGFILE_sd="/sdcard/wimax_uart.txt";

    FILE *fp_htclog = fopen(LOGFILE_htclog,"a+");
    FILE *fp_sd = fopen(LOGFILE_sd,"a+");

    if(fp_htclog==NULL)
        DM("open write zfllog log file failed!");
    else
    {
        fwrite(buf,1,size,fp_htclog);
        fclose(fp_htclog);
    }

    if(fp_sd==NULL)
        DM("open write sd log file failed!");
    else
    {
        fwrite(buf,1,size,fp_sd);
        fclose(fp_sd);
    }

}
