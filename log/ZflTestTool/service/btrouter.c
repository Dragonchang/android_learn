#define	LOG_TAG		"STT:btrouter"

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
#include "headers/usb.h"

//for dragon serial
//from "commands/cmmbrouter/inc/inno_drv.h"
#include <sys/ioctl.h>
#define INNO_DEV_CTL		"/dev/inno_ctl"
#define INNO_IOC_MAGIC		'i'
#define IOCTL_ROUTER_SWITCH	_IOWR(INNO_IOC_MAGIC, 34, int)

//#include <bluetooth/bluetooth.h>
//#include <bluetooth/hci.h>
//#include <bluetooth/hci_lib.h>

#if 1				//allenou, for uart hs bt router, 2008/12/8
//#define UART_HS_SPEED_RATE 3686400
#define UART_HS_SPEED_RATE 115200
#endif

#define HCI_MAX_COMMAND_SIZE 260

#ifndef	B3686400
#define B3686400 0010016
#endif

/* for serviced */
#define	VERSION	"1.3"
/* custom commands */
#define	CMD_RUN		":run:"
#define	CMD_STOP	":stop:"
#define	CMD_ISRUNNING	":isrunning:"
#define CMD_RECONNECT   ":reconnect:"
#define	SOCKET_BUFFER_SIZE	(512)

static int done = 0;
static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
int socket_fd;
int fd, dd;			/* dd - Device descriptor returned by hci_open_dev. */
int end = 0;
int run_result = 0;
sem_t mutex, rfinish, wfinish, openPortMutex;

typedef struct
{
	uint8_t uart_prefix;
	//hci_command_hdr hci_hdr;
	uint32_t speed;
} __attribute__ ((packed)) texas_speed_change_cmd_t;

typedef struct
{
	uint8_t uart_prefix;
	//hci_event_hdr hci_hdr;
	//evt_cmd_complete cmd_complete;
	uint8_t status;
	uint8_t data[16];
} __attribute__ ((packed)) command_complete_t;

/* LIN: init to empty */
static char SYSFS_POWER_ON_PATH[64] = "";

static int init_rfkill ()
{
	char path[64];
	char buf[16];
	int fd, sz, id;
	int rfkill_id = -1;

	for (id = 0;; id++)
	{
		snprintf (path, sizeof (path), "/sys/class/rfkill/rfkill%d/type", id);
		fd = open (path, O_RDONLY);
		if (fd < 0)
		{
			LOGE ("open(%s) failed\n", path);
			return -1;
		}
		sz = read (fd, &buf, sizeof (buf));
		close (fd);
		if (sz >= 9 && memcmp (buf, "bluetooth", 9) == 0)
		{
			rfkill_id = id;
			break;
		}
	}

	sprintf (SYSFS_POWER_ON_PATH, "/sys/class/rfkill/rfkill%d/state",
			rfkill_id);

	if (access (SYSFS_POWER_ON_PATH, F_OK))
	{
		return -1;
	}

	return 0;
}

/* LIN: steal from init.c */
static char *get_bt_power_file (void)
{
	char data[16];
	int fd, n;
	char *x;

	if (SYSFS_POWER_ON_PATH[0])	/* stopping do again */
		goto fin;

	if (init_rfkill ())		//this means init fail
	{
		SYSFS_POWER_ON_PATH[0] = 0;

		if (get_board_name (data, sizeof (data)) == NULL)
		{
			LOGE ("Error parsing /proc/cpuinfo\n");
			goto fin;
		}

		sprintf (SYSFS_POWER_ON_PATH,
				"/sys/module/board_%s/parameters/bluetooth_power_on", data);
	}

	LOGD ("bluetooth_power_on path: [%s]\n", SYSFS_POWER_ON_PATH);

fin:;
    return SYSFS_POWER_ON_PATH;
}

int set_bluetooth_power (int on)
{

	int fd = open (get_bt_power_file (), O_WRONLY);
	if (fd == -1)
	{
		LOGE ("Can't open for write");
		return -1;
	}
	const char *buffer = (on ? "1" : "0");
	int sz;
#ifdef BT_TI
	if(on == 1){
	 	write (fd, "0", 1);
		usleep(1000000);
		sz =  write (fd, "1", 1);
	}else
		sz =  write (fd, "0", 1);
#else
	sz =  write (fd, buffer, 1);
#endif
	if (sz != 1)
	{
		LOGE ("Can't write %s", get_bt_power_file ());
		return -1;
	}
	close (fd);

	return 0;
}

/*UART*/
int UART_init ()
{
	if ((fd = pclink_open (PCLINK_RAW)) < 0)
	{
		LOGE ("open pclink error!\r\n");
		return 1;
	}
	return 0;
}

int UART_exit ()
{
	if (pclink_close (fd) < 0)
	{
		LOGE ("close pclink error!\r\n");
		return -1;
	}
	fd = -1;
	return 0;
}

/* modified HCI functions that require open device */

int send_cmd (uint16_t opcode, uint8_t plen, void *param)
{
	/*uint8_t type = HCI_COMMAND_PKT;
	hci_command_hdr hc;
	struct iovec iv[3];
	int ivn;

	hc.opcode = opcode;
	hc.plen = plen;

	iv[0].iov_base = &type;
	iv[0].iov_len = 1;
	iv[1].iov_base = &hc;
	iv[1].iov_len = HCI_COMMAND_HDR_SIZE;
	ivn = 2;

	if (plen)
	{
		iv[2].iov_base = param;
		iv[2].iov_len = plen;
		ivn = 3;
	}

	while (writev (dd, iv, ivn) < 0)
	{
		if (errno == EAGAIN || errno == EINTR)
			continue;
		return -1;
	}*/
	return 0;
}

void *readCommand (void *UNUSED_VAR (null))
{
	/*int i, n, len, received;
	uint16_t opcode;
	int stop = 0;
	unsigned char buf[HCI_MAX_COMMAND_SIZE] = { 0 };
	unsigned char cmd[HCI_MAX_COMMAND_SIZE] = { 0 };
	//struct hci_filter nf;

	struct pollfd p;
	p.fd = fd;
	p.events = POLLIN;

	pthread_detach (pthread_self ());

	//FILE *hcicmds = fopen(TMP_DIR "hcidump.txt","w");

	while (1)
	{

		sem_wait (&mutex);
		stop = end;
		sem_post (&mutex);

		if (stop)
		{
			//fclose(hcicmds);
			sem_post (&rfinish);
			break;		//this thread finish right here
		}
		else
		{
			while ((n = poll(&p, 1, 1000)) < 0)
				;
			if(!n) continue; //pollin timeout
		}

		received = read (fd, cmd, sizeof (cmd));
		LOGE("RECEIVED %d byte and cmd[0] is %2.2x",received,cmd[0]);
		LOGD
			("receive command: %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
			 cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7],
			 cmd[8], cmd[9]);

		if (received > 0 && cmd[0] == 0x01)
		{
			while ((received < 4) || (received < ((int) cmd[3] + 4)))
			{
				if ((len = read (fd, buf, sizeof (buf))) > 0)
				{
					memcpy (&cmd[received], buf, len);
					memset (buf, 0, sizeof (buf));
					received += len;
				}
			}

			LOGD
				("receive command: %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
				 cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7],
				 cmd[8], cmd[9]);
			/*
			   fprintf(hcicmds,"		{");
			   for(i=0;i<received;i++)
			   fprintf(hcicmds,"0x%2.2X, ",cmd[i]);
			   fprintf(hcicmds,"},\n");
			   */

			//opcode = htobs ((uint16_t) (cmd[2] << 8) | (uint16_t) cmd[1]);
			/*
			   hci_filter_clear(&nf);
			   hci_filter_set_ptype(HCI_EVENT_PKT,  &nf);
			   hci_filter_set_event(EVT_CMD_STATUS, &nf);
			   hci_filter_set_event(EVT_CMD_COMPLETE, &nf);
			   hci_filter_set_opcode(opcode, &nf);
			   setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf));
			   */
			/*if (send_cmd (opcode, (uint8_t) (cmd[3]), &cmd[4]) < 0)
			{
				LOGE ("send_cmd error\n");
				continue;
			}
			memset (cmd, 0, sizeof (cmd));
		}
	}*/
	return NULL;
}

void *sendResponse (void *UNUSED_VAR (null))
{
	/*int i, n, len;
	int stop = 0;
	unsigned char rbuf[HCI_MAX_EVENT_SIZE];

	struct pollfd p;
	p.fd = dd;
	p.events = POLLIN;

	pthread_detach (pthread_self ());

	while (1)
	{

		sem_wait (&mutex);
		stop = end;
		sem_post (&mutex);

		if (stop)
		{
			sem_post (&wfinish);
			break;		//this thread finish right here
		}
		else
		{
			while ((n = poll (&p, 1, 1000)) < 0);
			if (!n)
				continue;		//pollin timeout
		}

		LOGD ("polling OK!\n");
		len = read (dd, rbuf, sizeof (rbuf));
		if (len > 0)
		{
			write (fd, rbuf, len);
			memset (rbuf, 0, sizeof (rbuf));
		}
	}*/
	return NULL;
}

static int uart_speed (int s)
{
	switch (s)
	{
		case 9600:
			return B9600;
		case 19200:
			return B19200;
		case 38400:
			return B38400;
		case 57600:
			return B57600;
		case 115200:
			return B115200;
		case 230400:
			return B230400;
		case 460800:
			return B460800;
		case 500000:
			return B500000;
		case 576000:
			return B576000;
		case 921600:
			return B921600;
		case 1000000:
			return B1000000;
		case 1152000:
			return B1152000;
		case 1500000:
			return B1500000;
		case 3686400:
			return B3686400;
		default:
			return B57600;
	}
}

static int set_speed (int dd, struct termios *ti, int speed)
{
	cfsetospeed (ti, uart_speed (speed));
	cfsetispeed (ti, uart_speed (speed));
	return tcsetattr (dd, TCSANOW, ti);
}

/*
 * Read an HCI event from the given file descriptor.
 */
int read_hci_event (int fd, unsigned char* buf, int size)
{
	int remain, r;
	int count = 0;

	if (size <= 0)
		return -1;

	/* The first byte identifies the packet type. For HCI event packets, it
	 * should be 0x04, so we read until we get to the 0x04. */
	while (1)
	{
		r = read (fd, buf, 1);
		if (r <= 0)
			return -1;
		if (buf[0] == 0x04)
			break;
	}
	count++;

	/* The next two bytes are the event code and parameter total length. */
	while (count < 3)
	{
		r = read (fd, buf + count, 3 - count);
		if (r <= 0)
			return -1;
		count += r;
	}

	/* Now we read the parameters. */
	if (buf[2] < (size - 3))
		remain = buf[2];
	else
		remain = size - 3;

	while ((count - 3) < remain)
	{
		r = read (fd, buf + count, remain - (count - 3));
		if (r <= 0)
			return -1;
		count += r;
	}

	return count;
}

static int read_command_complete (int fd, uint16_t opcode)
{
	//command_complete_t resp;
	/* Read reply. */
	/*if (read_hci_event (fd, (unsigned char *) &resp, sizeof (resp)) < 0)
	{
		LOGE ("Failed to read response for opcode %#04x\n", opcode);
		return -1;
	}
*/
	/* Parse speed-change reply */
	/*if (resp.uart_prefix != HCI_EVENT_PKT)
	{
		LOGE ("Error in response: not an event packet, but %#02X!\n",
				resp.uart_prefix);
		return -1;
	}

	if (resp.hci_hdr.evt != EVT_CMD_COMPLETE)
	{
		LOGE ("Error in response: not a cmd-complete event, but 0x%02x!\n",
				resp.hci_hdr.evt);
		return -1;
	}

	if (resp.hci_hdr.plen < 4)
	{
		LOGE ("Error in response: plen is not >= 4, but %#02X!\n",
				resp.hci_hdr.plen);
		return -1;
	}
*/
	/* cmd-complete event: opcode */
	/*if (resp.cmd_complete.opcode != (uint16_t) opcode)
	{
		LOGE ("Error in response: opcode is %#04X, not %#04X!",
				resp.cmd_complete.opcode, opcode);
		return -1;
	}

	if (resp.status != 0)
	{
		LOGE ("Error in response: status is %x for opcode %#04x\n",
				resp.status, opcode);
		return -1;
	}*/

	//return resp.status == 0 ? 0 : -1;
return -1;
}

static int btrouter_texas_change_speed (int fd, struct termios *ti, uint32_t speed)
{

	/* Send a speed-change request */
	/*texas_speed_change_cmd_t cmd;
	int n;

	cmd.uart_prefix = HCI_COMMAND_PKT;
	cmd.hci_hdr.opcode = 0xff36;
	cmd.hci_hdr.plen = sizeof (uint32_t);
	cmd.speed = speed;

	LOGD ("--- Setting speed to %d ---\n", speed);
	n = write (fd, &cmd, sizeof (cmd));
	if (n < 0)
	{
		LOGE ("--- Failed to write speed-set command ---\n");
		return -1;
	}

	if (n < (int) sizeof (cmd))
	{
		LOGE ("--- Wanted to write %d bytes, could only write %d. Stop ---\n",
				(int) sizeof (cmd), n);
		return -1;
	}

	/* Parse speed-change reply */
	/*if (read_command_complete (fd, 0xff36) < 0)
	{
		LOGE ("--- Can't get read_command_complete ---\n");
		return -1;
	}

	if (set_speed (fd, ti, speed) < 0)
	{
		LOGE ("--- Can't set baud rate ---\n");
		return -1;
	}

	LOGD ("--- Texas speed changed to %d. ---\n", speed);*/
	return 0;
}

static void get_bt_uart_node (char *bt_uart_path)
{
	int i;
	char board [32]={0}, temp_node[32]={0};
	const char *bt_uart_nodes [] = {
		"/dev/ttyHS2",
		"/dev/ttyHS0",
		"/dev/ttyS1",
		"/dev/cg2900_hci_raw", /* Note: For STE, put this node before /dev/ttyAMA0 */
		"/dev/ttyAMA0",
		"/dev/ttyMSM0",
        "/dev/ttyS0",
		""
	};

	/*
		Original list node way is not suitable for STE projects.
		For example, PYD_TD use ttyAMA0 and CP2 use ttyAMA1, list node
		will have some collision problem
	*/
	if (get_board_name (board, sizeof (board)) != NULL ) {
		if (strcmp (board, "cp2dcg") == 0 ||
			strcmp (board, "cp2dtg") == 0 ||
			strcmp (board, "cp2dug") == 0 ||
			strcmp (board, "prototd") == 0 ||
			strcmp (board, "csndug") == 0 ||
			strcmp (board, "cp2u") == 0 ||
			strcmp (board, "csnu") == 0 ||
			strcmp (board, "htc_csnu") == 0
		) {
			strcpy (temp_node, "/dev/ttyAMA1");
		}

	        if (strcmp (board, "z4dtg") == 0 ||
	                        strcmp (board, "z4td") == 0 ||
				strcmp (board, "cp5dtu") == 0 ||
				strcmp (board, "cp5dug") == 0 ||
				strcmp (board, "cp5dwg") == 0 ||
				strcmp (board, "cp5vedtu") == 0 ||
				strcmp (board, "cp5vedwg") == 0 ||
				strcmp (board, "cp5dtc") == 0)
	        {
	            strcpy (temp_node, "/dev/ttyS0");
	        }

               if (strcmp (board, "flounder64") == 0 || strcmp (board, "flounder") == 0 || strcmp (board, "flounder_lte") == 0) {
                   strcpy (temp_node, "/dev/ttyTHS2");
               }

		if (strlen(temp_node) > 0) {
			if (!access(temp_node, F_OK)) {
				strcpy (bt_uart_path, temp_node);
				return;
			} else {
				LOGD("Could not open temp_node !?");
			}
		}
	}

	/* Follow old list node way if no find above */
	for (i = 0; bt_uart_nodes[i][0]; i++)
	{
		if (!access(bt_uart_nodes[i], F_OK)) {
			strcpy (bt_uart_path, bt_uart_nodes[i]);
			return;
		}
	}

	strcpy (bt_uart_path, bt_uart_nodes[i-1]);
	LOGD("Could not find bt uart path!");
}

/* Initialize UART0 driver */
int init_uart0 (const char *dev, int send_break)
{
	struct termios ti;
	int i;

	LOGD ("init_uart0\n");
	dd = open (dev, O_RDWR | O_NOCTTY);
	if (dd < 0)
	{
		LOGE ("--- Can't open serial port %s=%d ---\n", dev, dd);
		return -1;
	}
	LOGD ("uart_open OK!\n");

	if (!strcmp(dev, "/dev/cg2900_hci_raw"))
	{
		/* for STE */
		LOGD ("Open STE port OK!\n");
		return dd;
	}

	tcflush (dd, TCIOFLUSH);

	if (tcgetattr (dd, &ti) < 0)
	{
		LOGE ("Can't get port settings");
		return -1;
	}

	cfmakeraw (&ti);

	ti.c_cflag |= CLOCAL;
	ti.c_cflag |= CRTSCTS;

	if (tcsetattr (dd, TCSANOW, &ti) < 0)
	{
		LOGE ("Can't set port settings");
		return -1;
	}

	/* Set initial baudrate */
	if (set_speed (dd, &ti, 115200) < 0)
	{
		LOGE ("Can't set initial baud rate");
		return -1;
	}

#ifndef BT_BCM
	if (!strcmp(dev, "/dev/ttyHS0")) {
		/* for ti */
		LOGD ("use TI vendor command to set high speed baud rate\n");

		if (btrouter_texas_change_speed (dd, &ti, UART_HS_SPEED_RATE) < 0)
		{
			LOGE ("--- Can't set initial high speed baud rate ---\n");
			return -1;
		}
	}
#endif

	LOGD ("set_speed OK!\n");
	tcflush (dd, TCIOFLUSH);

	if (send_break)
	{
		tcsendbreak (dd, 0);
		usleep (500000);
	}
	return dd;
}

int htc_switch_router_mode(int enableUART) {

	int fd, ret = 0;
   	LOGD("htc_switch_router_mode+\r\n");
	if ((fd = open (INNO_DEV_CTL, O_RDWR)) < 0)
		return -1;

	LOGD("open IOCTL sucessfully.(%d)\n",ret);

	LOGD("fd = %d\n",fd);
	ret = ioctl(fd, IOCTL_ROUTER_SWITCH, (long*)&enableUART);
   	LOGD("htc_switch_router_mode- (%d)\r\n",ret);
	close (fd);
	return ret;

}

static void *thread_main (void *UNUSED_VAR (null))
{
	char uart_path[50];
	pthread_t readThread, writeThread;

	if (set_bluetooth_power (1))
	{
		LOGE ("Can't power on bluetooth");
		sem_post(&openPortMutex);
		return (void *) 1;
	}


	if (UART_init () == 0)
	{
		if (access ("/sys/module/msm_serial_debugger/parameters/enable",F_OK)){
			if (htc_switch_router_mode(1)!=0)
				LOGE("switch to router mode fail!!!");
			else
				LOGE("switch to router mode sucessfully.");
		}

		get_bt_uart_node(uart_path);
		LOGD("Get bt uart node [%s]", uart_path);

		if (init_uart0 (uart_path, 0) < 0)
		{
			LOGE ("Can't initialize device");
			sem_post(&openPortMutex);
			return (void *) 1;
		}

		sem_init (&mutex, 0, 1);
		sem_init (&rfinish, 0, 0);
		sem_init (&wfinish, 0, 0);
		pthread_create (&readThread, NULL, readCommand, NULL);
		pthread_create (&writeThread, NULL, sendResponse, NULL);
		run_result = 1;
		sem_post(&openPortMutex);

		while (!end){
			usleep(100000);
		}

		/* quit read/write thread */
		sem_wait (&rfinish);
		sem_wait (&wfinish);

		UART_exit ();

		//close bt uart
		close(dd);
	}
	else
		sem_post(&openPortMutex);

	set_bluetooth_power (0);

	if (access ("/sys/module/msm_serial_debugger/parameters/enable",F_OK)){
		if (htc_switch_router_mode(0)!=0){
			LOGE("switch to router mode fail!!! v2");
			enable_usb_serial (1);
		}
		else
			LOGE("switch to router mode sucessfully.");
	}

	usleep(100000);  // 100 ms !!

	run_result = 0;

	return 0;
}

int btrouter_main (int server_socket)
{
	pthread_t working = (pthread_t) - 1;
	char buffer[SOCKET_BUFFER_SIZE + 15];
	int commfd = -1;
	int ret = 0;
	struct timespec ts;
	char board [32]={0};
	int usb_serial_config_done = 0;

	if (!usb_serial_config_done) {
		DM ("8994 usbserial by prop\n");
		system ("setprop sys.usb.config mtp,adb,mass_storage,serial");
		usb_serial_config_done = 1;
		// set below prop back to original mode
		//system ("setprop sys.usb.config mtp,adb,mass_storage");
	}

	if (!usb_serial_config_done) {
		get_board_name(board, sizeof (board));
		if (strcmp (board, "flounder64") == 0 || strcmp (board, "flounder") == 0 || strcmp (board, "flounder_lte") == 0) {
			system("setprop sys.usb.config mtp,adb,serial");
		} else {
			enable_usb_serial (1);
		}
	}
	usleep(100000);

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
				end = 1;

				pthread_mutex_lock (&data_lock);
				done = 1;
				pthread_mutex_unlock (&data_lock);
				buffer[0] = 0;
				break;
			}
			else if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, CMD_RUN))
			{
				sem_init(&openPortMutex, 0, 0);

				/* start bt router */
				if (working == (pthread_t) - 1)
				{
					end = 0;
					if (pthread_create (&working, NULL, thread_main, NULL) != 0)
						ret = -1;
					else
					{
						clock_gettime(CLOCK_REALTIME, &ts);
						ts.tv_sec += 15;
						sem_timedwait(&openPortMutex, &ts);
					}
				}
				else
				{
					DM ("the bt router is already running");
#ifdef BT_TI
					set_bluetooth_power (1);
#endif
				}

				sem_destroy(&openPortMutex);
				sprintf (buffer,"%d", run_result);
			}
			else if (CMP_CMD (buffer, CMD_STOP))
			{
#ifdef BT_TI
				set_bluetooth_power (0);
#else
				if (working != (pthread_t) - 1)
				{
					end = 1;

					/* quit thread */
				pthread_mutex_lock (&data_lock);
					done = 1;
					pthread_mutex_unlock (&data_lock);

					pthread_join (working, NULL);
					working = (pthread_t) - 1;
					done = 0;

					buffer[0] = 0;
				}
#endif
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

	if (working != (pthread_t) - 1)
		pthread_join (working, NULL);

	/* reset done flag */
	done = 0;

	return 0;
}
