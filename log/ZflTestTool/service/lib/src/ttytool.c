#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#define	LOG_TAG	"STT:tty"

#include <utils/Log.h>

#include "headers/attr_table_switch.h"
#include "headers/ttytool.h"
#include "headers/board.h"

static GLIST *glist_head;
static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

int _compare_tty_fd (void *key, void *member)
{
	TTYDB *p = member;
	if (key && p && (*((int *)key) == p->tty_saved_fd)) return 0;
	return -1;
}

void ttydb_dump ()
{
	GLIST *node;
	TTYDB *p;

	if (! glist_head) return;

	ALOGD ("ttydb_dump +++\n");
	for (node = glist_head; node; node = GLIST_NEXT (node))
	{
		if ((p = (TTYDB *) GLIST_DATA (node)) != NULL)
		{
			ALOGD ("TTYDB fd: [%d], state: [%d]\n", p->tty_saved_fd, p->tty_state);
		}
		else
		{
			ALOGE ("TTYDB null member\n");
		}
	}
	ALOGD ("ttydb_dump ---\n");
}

int ttydb_add (int fd)
{
	pthread_mutex_lock (& data_lock);
	TTYDB *ttydb = (TTYDB *) malloc (sizeof (TTYDB));
	ttydb->tty_saved_fd = fd;
	ttydb->tty_state = RESET;
	glist_add (& glist_head, ttydb);
	pthread_mutex_unlock (& data_lock);
	return 0;
}

int ttydb_remove (int fd)
{
	if (! glist_head) return -1;
	pthread_mutex_lock (& data_lock);
	int idx = glist_find_ex (& glist_head, (void *) &fd, _compare_tty_fd);
	if (idx < 0)
	{
		pthread_mutex_unlock (& data_lock);
		return -1;
	}
	glist_delete (& glist_head, idx, NULL);
	pthread_mutex_unlock (& data_lock);
	return 0;
}

TTYDB *ttydb_get (int fd)
{
	TTYDB *p;
	if (! glist_head) ttydb_add (fd);
	p = glist_get (& glist_head, glist_find_ex (& glist_head, (void *) &fd, _compare_tty_fd));
	if (p == (TTYDB *) -1)
	{
		ttydb_add (fd);
		p = glist_get (& glist_head, glist_find_ex (& glist_head, (void *) &fd, _compare_tty_fd));
	}
	return (p == (TTYDB *) -1) ? NULL : p;
}

int tty_open (const char* tty_name)	/* open tty device */
{
	/*
	 * Open modem device for reading and writing and not as controlling tty
	 * because we don't want to get killed if linenoise sends CTRL-C.
	 */
	int fd = open(tty_name, O_RDWR | O_NOCTTY);		/* set to blocking I/O */
	if (fd < 0) {
		ALOGE("tty_open failed for opening: %s\n", tty_name);
		ALOGE("Error: %s\n", strerror(errno));
		return(-1);
	}
	if (isatty(fd) < 0) {
		ALOGE("%s seems NOT to be a tty device!\n", tty_name);
		return(-1);
	}

	return(fd);
}


int tty_canonical(int fd)
{
	struct termios buftio;

	if (tcgetattr(fd, &buftio) < 0)
		return(-1);

	TTYDB *p = ttydb_get (fd);
	p->tty_termios = buftio;

	/* setting baud rate */
	cfsetispeed(&buftio, BAUDRATE);
	cfsetospeed(&buftio, BAUDRATE);
	buftio.c_cflag |= (CLOCAL | CREAD);

	/* (Control Options)
	 * No parity, charcter size(8N1),
	 * disable Hardware Flow control
	 */
	buftio.c_cflag &= ~PARENB;
	buftio.c_cflag &= ~CSTOPB;
	buftio.c_cflag &= ~CSIZE;
	buftio.c_cflag |= CS8;
	buftio.c_cflag &= ~CRTSCTS;

	/* (Local Options)
	 * choose Canonical Input, ECHO and ECHOE off
	 */
	buftio.c_lflag |= (ICANON);
	buftio.c_lflag &= ~(ECHO | ECHOE);

	/* (Input Options)
	 * disable Software Flow control
	 */
	buftio.c_iflag &= ~(IXON | IXOFF | IXANY);
	//buftio.c_iflag |= ICRNL;
	buftio.c_iflag |= IGNCR;

	/* (Output Options)
	 * raw output
	 */
	buftio.c_oflag &= ~(OPOST);

	if (tcsetattr(fd, TCSANOW, &buftio) < 0)
		return(-1);

	if (tcgetattr(fd, &buftio) < 0) {
		tcsetattr(fd, TCSAFLUSH, &(p->tty_termios));
		return(-1);
	}

	/*
	 * Verify that the changes stuck. tcsetattr can return 0 on
	 * partial success.
	 */
	ALOGE("Check if we enters canonical mode...");
	if (((buftio.c_lflag & (ICANON)) != (ICANON)) ||
		(buftio.c_lflag & (ECHO | ECHOE)) ||
		(buftio.c_iflag & (IXON | IXOFF | IXANY)) ||
		(buftio.c_cflag & (CSIZE | PARENB | CS8 | CSTOPB)) != CS8 ||
		(buftio.c_oflag & OPOST)) {
		ALOGE("fail!\n");
		tcsetattr(fd, TCSAFLUSH, &(p->tty_termios));
		return(-1);
	}
	ALOGD("done!\n");
	p->tty_state = CANONICAL;
	ttydb_dump ();
	return(0);
}


int tty_raw(int fd)	/* put terminal into raw mode */
{
	int err;
	struct termios buf;

	TTYDB *p = ttydb_get (fd);
	if ((!p) || (p->tty_state != RESET))
	{
		errno = EINVAL;
		return(-1);
	}

	if (tcgetattr(fd, &buf) < 0)
		return(-1);
	p->tty_termios = buf;

	/* setting baud rate */
	cfsetispeed(&buf, BAUDRATE);
	cfsetospeed(&buf, BAUDRATE);
	/*
	 * (Local options)
	 * Echo off, canonical mode off, extended input
	 * procesing off, signal chars off.
	 */
	buf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);


	/*
	 * (Input options)
	 * No SIGINT on BREAK, CR-to-NL off, input parity
	 * check off, don't strip 8th bit on input, output
	 * flow control off.
	 */
	buf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

	//buf.c_iflag &= IGNPAR;
	/*
	 * (Control options)
	 * Clear size bits, parity checking off.
	 */
	buf.c_cflag &= ~(CSIZE | PARENB);

	//buf.c_cflag |= (BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD);
	/*
	 * (Control options)
	 * Set 8 bits/char.
	 */
	buf.c_cflag |= CS8;

	/*
	 * (Output options)
	 * Output processing off.
	 */
	buf.c_oflag &= ~(OPOST);

	/*
	 * 1 byte at a time, no timer.
	 */
	buf.c_cc[VMIN] = 1;
	buf.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &buf) < 0)
		return(-1);
	/*
	 * Verify that the changes stuck. tcsetattr can return 0 on
	 * partial success.
	 */
	if (tcgetattr(fd, &buf) < 0) {
		err = errno;
		tcsetattr(fd, TCSAFLUSH, &(p->tty_termios));
		errno = err;
		return(-1);
	}
	if ((buf.c_lflag & (ECHO | ICANON | IEXTEN | ISIG)) ||
		(buf.c_iflag & (BRKINT | ICRNL | INPCK | ISTRIP | IXON)) ||
		(buf.c_cflag & (CSIZE | PARENB | CS8)) != CS8 ||
		(buf.c_oflag & OPOST) || buf.c_cc[VMIN] != 1 ||
		buf.c_cc[VTIME] != 0) {
		tcsetattr(fd, TCSAFLUSH, &(p->tty_termios));
		errno = EINVAL;
		return(-1);
	}

	p->tty_state = RAW;
	ttydb_dump ();

	tty_set_lowlatency(fd, 1);
	return(0);

}

int tty_set_lowlatency(int fd, int enable)
{
	struct serial_struct srl;

	ioctl(fd, TIOCGSERIAL, &srl);
	if (enable)
		srl.flags = (1U << 13);
	else
		srl.flags &= !(1U << 13);
	ioctl(fd, TIOCSSERIAL, &srl);
	return 0;
}

int tty_reset(int fd)	/* restore terminal's mode */
{
	TTYDB *p = ttydb_get (fd);
	if ((!p) || (p->tty_state == RESET))
	{
		return(0);
	}

	ALOGD("restore original settings...\n");
	if (tcsetattr(fd, TCSAFLUSH, &(p->tty_termios)) < 0)
		return(-1);
	p->tty_state = RESET;
	return(0);
}

int tty_close(int fd)
{
	tty_set_lowlatency(fd, 0);
	tty_reset(fd);
	close(fd);
	ttydb_remove (fd);
	return(0);
}

int tty_input_size(int fd, int* bytes)
{
	ioctl(fd, FIONREAD, bytes);
	return 0;
}

static char cached [4096];
static unsigned int len = 0;

static int tty_read_from_cache (char *buf, size_t count)
{
	char *ptr;
	int bytesRead;

	if ((! len) || (count < 3))
		return -1;

	if (! (ptr = strchr (cached, '\r')))
		return -1;

	/* get length to copy */
	*ptr = 0;
	bytesRead = strlen (cached);
	*ptr = '\r';

	/* need room for newline and null character */
	count -= 2;

	/* adjust length to avoid overflow */
	while ((size_t) bytesRead > count)
	{
		bytesRead --;
		ptr --;
	}

	/* copy bytesRead bytes */
	strncpy (buf, cached, bytesRead);

	/* append newline and null character */
	buf [bytesRead ++] = '\r';
	buf [bytesRead] = 0;
	ALOGD ("tty_read_from_cache: %d [%s]\n", bytesRead, buf);

	/* fix cached data */
	if (*ptr == '\r') ptr ++;
	memmove (cached, ptr, strlen (ptr) + 1);
	len = strlen (cached);

	return bytesRead;
}

ssize_t tty_read_line (int fd, char *buf, size_t count)
{
	int bytesRead, i;

	if (count < 3)	/* at least one data, one newline and one null characters */
	{
		ALOGE ("tty_read_line: user buffer is too small!\n");
		return -1;
	}

	for (;;)
	{
		/* cached data */
		if ((bytesRead = tty_read_from_cache (buf, count)) >= 0)
			break;

		bytesRead = read (fd, buf, count);

		ALOGD ("tty_read_line: read count = %d\n", bytesRead);

		if (bytesRead < 0)	/* error break */
			break;

		buf [bytesRead] = 0;

		TTYDB *p = ttydb_get (fd);
		if ((!p) || (p->tty_state == CANONICAL))
			break;

		if ((len + bytesRead) >= sizeof (cached))
		{
			ALOGE ("tty_read_line: cache full! data may lost!\n");
			strncpy (cached, buf, sizeof (cached) - len);
			len = sizeof (cached) - 1;
			cached [len] = 0;
			if (strchr (cached, '\r') == NULL) cached [len - 1] = '\r';
		}
		else
		{
			/* append to cached data */
			strncat (cached, buf, sizeof (cached) - strnlen (cached, sizeof (cached)) - 1);
			len += bytesRead;
		}
	}

	return bytesRead;
}

static ATTR_TABLE uart_control_table [] = {
	{ "/sys/module/msm_serial_debugger/parameters/enable",		"0", 	0, "" },
	{ "/sys/module/msm_serial_debugger/parameters/uart_enabled",	"1", 	0, "" },
	{ "/sys/module/pm/parameters/sleep_mode",			"5",	0, "" },
	{ "/sys/module/pm/parameters/idle_sleep_mode",			"5",	0, "" },
	{ "/sys/module/board_%s/parameters/h2w_path",			"2", 	1, "" },
	/* for ti omap */
	{ "/sys/devices/platform/omap-uart.0/sleep_timeout",		"0", 	0, "" },
	{ "/sys/devices/platform/omap-uart.1/sleep_timeout",		"0", 	0, "" },
	{ "/sys/devices/platform/omap-uart.2/sleep_timeout",		"0", 	0, "" },
	{ "/sys/devices/platform/omap-uart.3/sleep_timeout",		"0", 	0, "" },
	{ "/sys/devices/platform/serial8250.0/sleep_timeout",		"0", 	0, "" },
	{ "/sys/cmmb_router/router_switch",				"1", 	0, "" },
	{ "", "", 0, "" }
};
static int ref_count = 0;

int uart_get (void)
{
	return table_get (& ref_count, uart_control_table);
}

int uart_put (void)
{
	return table_put (& ref_count, uart_control_table);
}
