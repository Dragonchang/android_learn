#define	LOG_TAG		"STT:iotty"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <private/android_filesystem_config.h>

#include "common.h"
#include "server.h"

#include "headers/board.h"
#include "headers/ttytool.h"
#include "headers/usb.h"
#include "headers/poll.h"

/* ======================= */
/* === Service Version === */
/* ======================= */

#define	VERSION	"2.2"
/*
 * 2.2	: support ttyGS2 for T1 (flounder).
 * 2.1	: open debug mode when persist.radio.stt.iotty=1.
 * 2.0	: add mapping for Fit.
 * 1.10 : allow read from attr for USB serial node path when type is auto
 * 1.9	: read from attr for USB serial node path
 * 1.8	: support STE USB node ttyMUSB0.
 * 1.7	: support STE nodes.
 * 1.6	: change the tty type of ttyGS0 to uart.
 * 1.5	: add ti device node ttyGS0.
 * 1.4	: add ti device node ttyS2.
 * 1.3	: add board name to device node mapping list.
 * 1.2	: auto select uart/usb device node when the given device path is "auto".
 * 1.1	: accept a string parameter of ":close:" command, that requests service send specified string before end.
 * 1.0	: initial commit by Charlie Lin 20090213.
 */

/* ======================= */
/* === Custom Commands === */
/* ======================= */

#define	CMD_GETPATH	":getpath:"
/*
 * Get current path of device node.
 * Param	: none
 * Return	: path string
 * Note		: none
 */

#define	CMD_SETPATH	":setpath:"
/*
 * Set the path of device node.
 * Param	: path string
 * Return	: 0 for success, -1 for failure
 * Note		: not valid in opened state
 */

#define	CMD_GETIOPORT	":getioport:"
/*
 * Get the port of data socket.
 * Param	: none
 * Return	: port number
 * Note		: none
 */

#define	CMD_SETIOPORT	":setioport:"
/*
 * Set the port of data socket.
 * Param	: port number
 * Return	: 0 for success, -1 for failure
 * Note		: not valid in opened state
 */

#define	CMD_GETMODE	":getmode:"
/*
 * Get the data transfer mode.
 * Param	: none
 * Return	: 0 for canonical mode, 1 for raw mode, -1 for failure
 * Note		: none
 */

#define	CMD_SETMODE	":setmode:"
/*
 * Set the port of data socket.
 * Param	: 0 for canonical mode, 1 for raw mode, others are no effect
 * Return	: 0 for success, -1 for failure
 * Note		: not valid in opened state
 */

#define	CMD_GETTYPE	":gettype:"
/*
 * Get the device io type.
 * Param	: none
 * Return	: 0 for none, 1 for uart, 2 for usb, 3 for pipe, -1 for failure
 * Note		: none
 */

#define	CMD_SETTYPE	":settype:"
/*
 * Set the device io type.
 * Param	: 0 for none, 1 for uart, 2 for usb, 3 for pipe, others are no effect
 * Return	: 0 for success, -1 for failure
 * Note		: not valid in opened state
 */

#define	CMD_OPEN	":open:"
/*
 * Open data socket.
 * Param	: none
 * Return	: 0 for success, -1 for failure
 * Note		: none
 */

#define	CMD_CLOSE	":close:"
/*
 * Close data socket.
 * Param	: the string that should send back to client before connection close, it's optional
 * Return	: 0 for success, -1 for failure
 * Note		: none
 */

/* ========================= */
/* === Service Variables === */
/* ========================= */

#define	TTY_AUTO	"auto"

#define	TTY_TYPE_NONE	0
#define	TTY_TYPE_UART	1
#define	TTY_TYPE_USB	2
#define	TTY_TYPE_PIPE	3
#define	TTY_TYPE_LAST	TTY_TYPE_PIPE

#define	MODE_CANONICAL	0
#define	MODE_RAW	1
#define	MODE_LAST	MODE_RAW

typedef struct {
	int type;
	const char *path;
} TTY_NODE;

static TTY_NODE nodes [] = {
	/* device nodes in order */
	{ TTY_TYPE_USB,		"/dev/ttyHSUSB2"	},	/* 0 */
	{ TTY_TYPE_UART,	"/dev/ttyMSM2"		},	/* 1 */
	{ TTY_TYPE_UART,	"/dev/ttyMSM0"		},	/* 2 */
	{ TTY_TYPE_UART,	"/dev/ttyGS0"		},	/* 3 */
	{ TTY_TYPE_UART,	"/dev/ttyS2"		},	/* 4 */
	{ TTY_TYPE_USB,		"/dev/ttyMUSB0"		},	/* 5 */
	{ TTY_TYPE_UART,	"/dev/ttyAMA0"		},	/* 6 */
	{ TTY_TYPE_UART,	"/dev/ttyAMA2"		},	/* 7 */
	{ TTY_TYPE_USB,		"/dev/ttyfs"		},	/* 8 */
	{ TTY_TYPE_USB,		"/dev/ttyHSUSB0"	},	/* 9 */
	{ TTY_TYPE_UART,	"/dev/ttyGS2"		},	/* 10 */
	/* end of list */
	{ TTY_TYPE_NONE,	NULL			}
};

typedef struct {
	const char *board;
	int index;
} DEVICE_TTY_NODE;

static DEVICE_TTY_NODE device_nodes [] = {
	//{ "st-ericsson",	7 },	/* ttyAMA2 */
	{ "flounder",		10 },	/* ttyGS2 */
	{ "flounder64",		10 },	/* ttyGS2 */
	{ "z4dtg",		9 },	/* ttyHSUSB0 */
	{ "z4td",		9 },	/* ttyHSUSB0 */
	{ "cp5dtu",		9 },	/* ttyHSUSB0 */
	{ "cp5dug",		9 },	/* ttyHSUSB0 */
	{ "cp5dwg",		9 },	/* ttyHSUSB0 */
	{ "cp5vedtu",		9 },	/* ttyHSUSB0 */
	{ "cp5vedwg",		9 },	/* ttyHSUSB0 */
	{ "cp5dtc",		9 },	/* ttyHSUSB0 */
	{ "cp2u",		9 },	/* ttyHSUSB0 */
	{ "csndug",		9 },	/* ttyHSUSB0 */
	{ "csnu",               9 },	/* ttyHSUSB0 */
	{ "htc_csnu",           9 },	/* ttyHSUSB0 */
	{ "cp2dcg",		9 },	/* ttyHSUSB0 */
	{ "cp2dtg",		9 },	/* ttyHSUSB0 */
	{ "cp2dug",		9 },	/* ttyHSUSB0 */
	{ "prototd",            9 },	/* ttyHSUSB0 */
	{ "fit",		5 },	/* ttyMUSB0 */
	{ "st-ericsson",	5 },	/* ttyMUSB0 */
	{ "click",		1 },	/* ttyMSM2 */
	{ "desirec",		1 },	/* ttyMSM2 */
	{ "hero",		1 },	/* ttyMSM2 */
	{ "heroc",		1 },	/* ttyMSM2 */
	{ "bravo",		1 },	/* ttyMSM2 */
	{ "passion",		1 },	/* ttyMSM2 */
	{ NULL,			0 }
};

/* device type */
static int device_type = TTY_TYPE_NONE;

/* data transfer mode, default is canonical */
static int device_mode = MODE_CANONICAL;

/* path of device node */
static char device [PATH_MAX] = "";

/* string for client before connection close */
static char *closing = NULL;

/* data socket port number */
static int port = -1;

/* socket fd */
static int device_fd = -1;
static int server_fd = -1;

/* working threads */
static pthread_t thread_read = (pthread_t) -1;

/* polling control */
static POLL poll_read = POLL_INITIAL;
static POLL poll_write = POLL_INITIAL;

/* debug */
static int debug = 0;

/* ================== */
/* === Procedures === */
/* ================== */

/* read file to specify device type to auto:0, uart:1, usb:2 */
static int customize_device_type (void)
{
	FILE *fp;
	char buf [1];

	fp = fopen (DAT_DIR ".ssd_test_pclink", "r");

	if (! fp)
	{
		DM ("cannot open pclink device flag file!");
		return 0;
	}

	if (fread (buf, 1,  sizeof(buf), fp) < 1 )
		goto failed;

	fclose (fp);

	if (strncmp (buf, "1" , 1) == 0)
		device_type = TTY_TYPE_UART;
	else if (strncmp (buf, "2" , 1) == 0)
		device_type = TTY_TYPE_USB;
	else
		goto failed;

	DM ("customize_device_type: (%d) \n", device_type);

	return 1;

failed:;
       DM ("customize_device_type: failed");
       fclose (fp);
       return 0;

}

/* read from attr for serial tty name */
static int get_serial_node (void)
{
	FILE *fp;
	int MAX_TTY_NAME_LEN = 15;
	char buf [MAX_TTY_NAME_LEN];
	int read_count;

	fp = fopen ("/sys/module/serial/parameters/serial_name", "r");

	if (!fp)
	{
		DM ("there is no serial_name attribute!");
		return 0;
	}

	if ((read_count = fread(buf, 1, sizeof(buf), fp)) < 1)
		goto failed;

	fclose (fp);

	buf[read_count - 1] = 0;
	sprintf (device, "/dev/%s", buf);
	device_type = TTY_TYPE_USB;

	DM ("probe_device: get_serial_node (%d) %s\n", device_type, device);

	return 1;

failed:;
       DM ("get_serial_node: failed");
       fclose (fp);
       return 0;

}
/*
int compare_kernel_version(char* ver)
{
	char* proc_version = "/proc/version" ;
	int ret = 0;

	DM (" compare kernel version ");

	if(access(proc_version,F_OK) == 0)
	{
		FILE* fp = fopen(proc_version,"r");
		char line[1024] = {0};
		if(fp)
		{
			if(fgets(line,sizeof(line),fp))
			{
				if(strstr(line,ver))
				{
					DM ("kernel version : %s\n",line);
					ret = 1;
				}
			}
			fclose(fp);
		}
	}
	return ret ;
}
*/
/* probe device name */
static void probe_device (void)
{
	/* read file to deside device type if file exist*/
	int specify_device_type = customize_device_type ();

	/* serial node may not exist before enabled */
	enable_usb_serial (1);

	/* set default device type for compatibility */
	if (device_type == TTY_TYPE_NONE)
		device_type = TTY_TYPE_UART;

	/* probe device if it's auto */
	if (strcmp (device, TTY_AUTO) == 0)
	{
		DEVICE_TTY_NODE *pdnode;
		TTY_NODE *pnode = NULL;
		char board [32];

		/* get device node directly if serial_name attribute exists*/
		if (((device_type == TTY_TYPE_USB) || !specify_device_type) && get_serial_node())
			return;

		if (get_board_name (board, sizeof (board)) != NULL && !specify_device_type )
		{
			/* probe known device nodes */
			for (pdnode = device_nodes; pdnode && (pdnode->board != NULL); pdnode ++)
			{
				DM ("probe [%s] <-> [%s][%s]\n", board, pdnode->board, nodes [pdnode->index].path);

				if (strcmp (board, pdnode->board) == 0)
				{
					pnode = & nodes [pdnode->index];

					/* check existence */
					if (access (pnode->path, F_OK) == 0)
					{
						strcpy (device, pnode->path);
						device_type = pnode->type;
						break;
					}

					pnode = NULL;

					/* don't break here to allow multiple assign to a board */
				}
			}
		}

		/* pnode is not NULL if there is a match in device_nodes */
		if (pnode == NULL)
		{
			/* probe unknown device nodes */
			for (pnode = nodes; pnode && (pnode->type != TTY_TYPE_NONE); pnode ++)
			{
				/* check existence */
				if (access (pnode->path, F_OK) == 0)
				{
					if (!specify_device_type || pnode->type == device_type)
					{
						strcpy (device, pnode->path);
						device_type = pnode->type;
						break;
					}
				}
			}
		}
	}

	DM ("probe_device: (%d) %s\n", device_type, device);
}



/* open device node */
static int open_device (void)
{
	int fd = -1;

	DM ("Before use settings [dev=%s][mode=%d][type=%d]\n", device, device_mode, device_type);
	/* probe device node */
	probe_device ();

	const char *attr_path_fserial_ics	= "/sys/class/android_usb/f_serial/on";

	if(access(attr_path_fserial_ics ,F_OK)==0)
	{
		if(!strcmp(device,"/dev/ttyHSUSB2"))
		{
			if(chown(device,AID_SYSTEM,AID_SYSTEM) == 0)
			{
				if(chmod(device,S_IRUSR | S_IWUSR) < 0)
				{
					DM ("chmod fails : %s\n",device);
				}
			}
			else{
				DM ("chown fails : %s\n",device);
			}
		}
	}

	DM ("use settings [dev=%s][mode=%d][type=%d]\n", device, device_mode, device_type);

	if (device_type == TTY_TYPE_UART)
	{
	#ifdef BUILD_AND
		/* enable uart device */
		uart_get ();
	#endif

		if ((fd = tty_open (device)) < 0)
		{
			fd = -1;
		#ifdef BUILD_AND
			uart_put ();
		#endif
			goto end;
		}
		if (tty_raw (fd) < 0)
		{
			DM ("failed to config tty to raw mode!\n");
			/* go on without break */
		}
	}
	else
	{
	#ifdef BUILD_AND
		if (device_type == TTY_TYPE_USB)
		{
			/* enable usb serial */
			usb_serial_get ();
			usleep (1000000); //1s
		}
	#endif

		if ((fd = open (device, O_RDWR)) < 0)
		{
			DM ("open_device [%s] failed: %s\n", device, strerror (errno));
			fd = -1;
		#ifdef BUILD_AND
			if (device_type == TTY_TYPE_USB)
			{
				usb_serial_put ();
			}
		#endif
			goto end;
		}
	}

end:;
	return fd;
}

/* close device node */
static int close_device (int fd)
{
	int ret = -1;

	if (device_type == TTY_TYPE_UART)
	{
		ret = tty_close (fd);

	#ifdef BUILD_AND
		/* disable uart device */
		uart_put ();
	#endif
	}
	else if (device_type == TTY_TYPE_USB)
	{
		ret = close (fd);

	#ifdef BUILD_AND
		/* disable usb serial */
		usb_serial_put ();
	#endif
	}
	else
	{
		ret = close (fd);
	}

	device_type = TTY_TYPE_NONE;
	return ret;
}

/* read data from socket and redirect to device, this thread only be created when socket connected */
static void *thread_write_impl (void *arg)
{
	char buffer [1024];
	int count;

	int socketfd = (int) ((long) arg);

	DM ("thread_write: socket fd = %d.\n", socketfd);

	/* init polling */
	if (poll_open (& poll_write) < 0)
	{
		DM ("poll_open: %s\n", strerror (errno));
		return NULL;
	}

	for (;;)
	{
		if (debug) DM ("thread_write_impl: poll_wait\n");

		if (poll_wait (& poll_write, socketfd, -1) <= 0)
			break;

		if (debug) DM ("thread_write_impl: read socketfd=%d\n", socketfd);

		if ((count = read (socketfd, buffer, sizeof (buffer))) < 0)
		{
			DM ("read data from socket failed: %s\n", strerror (errno));
			break;
		}

		if (debug) DM ("thread_write_impl: count=%d\n", count);

		if (count == 0)
			continue;

		if (debug) DM ("thread_write_impl: write device_fd=%d\n", device_fd);

		if (write (device_fd, buffer, count) < 0)
		{
			DM ("write data to tty failed: %s\n", strerror (errno));
			break;
		}
	}

	/* close polling */
	poll_close (& poll_write);

	DM ("thread_write: end of thread.\n");

	return NULL;
}

/* read data from device and redirect to socket, also waiting connection at first */
static void *thread_read_impl (void *UNUSED_VAR (null))
{
	pthread_t thread_write = (pthread_t) -1;

	char buffer [1024];
	int count;
	int socketfd;

	/* init polling */
	if (poll_open (& poll_read) < 0)
	{
		DM ("poll_open: %s\n", strerror (errno));
		return NULL;
	}

	/* waiting connection */
	socketfd = wait_for_connection (server_fd);

	if (socketfd < 0)
	{
		DM ("accept connection failed: %s\n", strerror (errno));
		return NULL;
	}

	DM ("thread_read: connection accepted, socket fd = %d.\n", socketfd);

	/* start write thread */
	if (pthread_create (& thread_write, NULL, thread_write_impl, (void *) ((long) socketfd)) < 0)
	{
		DM ("pthread_create thread_write failed: %s\n", strerror (errno));
		thread_write = (pthread_t) -1;
		return NULL;
	}

	DM ("thread_read: thread_write created.\n");

	/* start reading */
	for (;;)
	{
		if (debug) DM ("thread_read_impl: poll_wait\n");

		if (poll_wait (& poll_read, device_fd, -1) <= 0)
			break;

		if (device_mode == MODE_CANONICAL)
		{
			if (debug) DM ("thread_read_impl: tty_read_line device_fd=%d\n", device_fd);

			count = tty_read_line (device_fd, buffer, sizeof (buffer));
		}
		else
		{
			if (debug) DM ("thread_read_impl: read device_fd=%d\n", device_fd);

			count = read (device_fd, buffer, sizeof (buffer));
		}

		if (debug) DM ("thread_read_impl: count=%d\n", count);

		if (count < 0)
		{
			DM ("read data from tty failed: %s\n", strerror (errno));
			break;
		}

		if (count == 0)
			continue;

		if (debug) DM ("thread_read_impl: write socketfd=%d\n", socketfd);

		if (write (socketfd, buffer, count) < 0)
		{
			DM ("write data to socket failed: %s\n", strerror (errno));
			break;
		}
	}

	/* write empty string */
	if (closing)
	{
		DM ("write [%s] to socket before close ...\n", closing);
		if (write (socketfd, closing, strlen (closing)) < 0)
		{
			DM ("write data to socket failed: %s\n", strerror (errno));
		}
		free (closing);
		closing = NULL;
		sleep (1);
	}

	DM ("thread_read: end of loop.\n");

	/* close polling */
	poll_close (& poll_read);

	/* close io socket */
	shutdown (socketfd, SHUT_RDWR);
	close (socketfd);
	socketfd = -1;

	/* stop write thread */
	poll_break (& poll_write);
	pthread_join (thread_write, NULL);
	thread_write = (pthread_t) -1;

	DM ("thread_read: end of thread.\n");

	return NULL;
}

/* service entry */
int iotty_main (int server_socket)
{
	char buffer [PATH_MAX + 16];

	int ret = 0;
	int done = 0;
	int commfd = -1;

	property_get ("persist.radio.stt.iotty", buffer, "0");

	debug = (buffer [0] == '1');

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
				done = 1;
				break;
			}

			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
			}
			else if (CMP_CMD (buffer, CMD_GETPATH))
			{
				strcpy (buffer, device);
			}
			else if (CMP_CMD (buffer, CMD_SETPATH))
			{
				MAKE_DATA (buffer, CMD_SETPATH);

				if (device_fd != -1)
				{
					DM ("cannot change device path while device being opened!\n");
					ret = -1;
				}
				else if (! buffer [0])
				{
					DM ("reject empty device path!\n");
					ret = -1;
				}
				else if ((strcmp (buffer, TTY_AUTO) != 0) && (access (buffer, R_OK | W_OK) < 0))
				{
					DM ("%s: %s\n", buffer, strerror (errno));
					ret = -1;
				}
				else
				{
					strcpy (device, buffer);
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_GETIOPORT))
			{
				if (port < 0)
				{
					port = get_free_port ();
				}
				sprintf (buffer, "%d", port);
			}
			else if (CMP_CMD (buffer, CMD_SETIOPORT))
			{
				MAKE_DATA (buffer, CMD_SETIOPORT);

				ret = atoi (buffer);

				if (device_fd != -1)
				{
					DM ("cannot change port while device being opened!\n");
					ret = -1;
				}
				else if (ret <= 0)
				{
					DM ("invalid port number (%d)!\n", ret);
					ret = -1;
				}
				else
				{
					port = ret;
					ret = 0;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_GETMODE))
			{
				sprintf (buffer, "%d", device_mode);
			}
			else if (CMP_CMD (buffer, CMD_SETMODE))
			{
				MAKE_DATA (buffer, CMD_SETMODE);

				ret = atoi (buffer);

				if (device_fd != -1)
				{
					DM ("cannot change transfer mode while device being opened!\n");
					ret = -1;
				}
				else if ((ret < 0) || (ret > MODE_LAST))
				{
					DM ("invalid mode (%d)!\n", ret);
					ret = -1;
				}
				else
				{
					DM ("set mode (%d)!\n", ret);
					device_mode = ret;
					ret = 0;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_GETTYPE))
			{
				sprintf (buffer, "%d", device_type);
			}
			else if (CMP_CMD (buffer, CMD_SETTYPE))
			{
				MAKE_DATA (buffer, CMD_SETTYPE);

				ret = atoi (buffer);

				if (device_fd != -1)
				{
					DM ("cannot change device type while device being opened!\n");
					ret = -1;
				}
				else if ((ret < 0) || (ret > TTY_TYPE_LAST))
				{
					DM ("invalid type (%d)!\n", ret);
					ret = -1;
				}
				else
				{
					device_type = ret;
					ret = 0;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_OPEN))
			{
				if (device_fd != -1)
				{
					DM ("device [%s] was already opened!\n", device);
					ret = -1;
				}
				else for (;;)
				{
					/* open device */
					if ((device_fd = open_device ()) < 0)
					{
						ret = device_fd = -1;
						break;
					}
					/* open data socket */
					if ((server_fd = init_server (port)) < 0)
					{
						close_device (device_fd);
						ret = server_fd = device_fd = -1;
						break;
					}
					/* start read thread, this will also start write thread */
					if (pthread_create (& thread_read, NULL, thread_read_impl, NULL) < 0)
					{
						DM ("pthread_create thread_read failed: %s\n", strerror (errno));
						thread_read = (pthread_t) -1;
						shutdown (server_fd, SHUT_RDWR);
						close (server_fd);
						close_device (device_fd);
						ret = server_fd = device_fd = -1;
						break;
					}
					/* all ok */
					break;
				}
				buffer [0] = 0;
			}
			else if (CMP_CMD (buffer, CMD_CLOSE))
			{
				MAKE_DATA (buffer, CMD_CLOSE);

				/* set closing string */
				if (closing)
				{
					free (closing);
					closing = NULL;
				}
				if (buffer [0])
				{
					closing = strdup (buffer);
				}
				/* close data sockets */
				if (server_fd != -1)
				{
					shutdown (server_fd, SHUT_RDWR);
					close (server_fd);
					server_fd = -1;
				}
				/* stop read thread */
				if (thread_read != (pthread_t) -1)
				{
					/* quit thread */
					poll_break (& poll_read);
					pthread_join (thread_read, NULL);
					thread_read = (pthread_t) -1;
				}
				/* close device */
				if (device_fd != -1)
				{
					close_device (device_fd);
					device_fd = -1;
				}
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

			DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		if (closing)
		{
			free (closing);
			closing = NULL;
		}

		shutdown (commfd, SHUT_RDWR);
		close (commfd);
		commfd = -1;

		ret = 0;
	}

	return ret;
}

#ifndef BUILD_AND
int tty_open (const char *tty_name)
{
	int fd = open (tty_name, O_RDWR | O_NOCTTY);	/* set to blocking I/O */

	if (fd < 0)
	{
		DM ("failed to open %s: %s\n", tty_name, strerror (errno));
		return -1;
	}

	if (isatty (fd) < 0)
	{
		DM ("%s seems NOT to be a tty device!\n", tty_name);
		return -1;
	}

	return fd;
}
int tty_close (int fd)
{
	close (fd);
	return 0;
}
int tty_raw (int fd)
{
	return 0;
}
ssize_t tty_read_line (int fd, char *buf, size_t count)
{
	return read (fd, buf, count);
}
#endif
