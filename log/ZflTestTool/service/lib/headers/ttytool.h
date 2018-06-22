#ifndef _TTYTOOL_H_INCLUDED__
#define _TTYTOOL_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <termios.h>

#include "glist.h"

#define	BAUDRATE	B115200		/* default baud rate, can be changed via ... */

typedef struct {
  char*  buf;
  int   size;
} thread_parm_t;

/* Flow control */
typedef enum {RESET, RAW, CANONICAL} ttystate;

typedef struct {
  int				tty_saved_fd;
  ttystate			tty_state;
  struct termios	tty_termios;
} TTYDB;

/*
 * sync with kernel_path/include/linux/serial.h
 * in order to set low latency of tty
 */
struct serial_struct {
	int	type;
	int	line;
	unsigned int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short	close_delay;
	char	io_type;
	char	reserved_char[1];
	int	hub6;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	unsigned char	*iomem_base;
	unsigned short	iomem_reg_shift;
	unsigned int	port_high;
	unsigned long	iomap_base;	/* cookie passed into ioremap */
};

void ttydb_dump ();
int ttydb_add (int fd);
int ttydb_remove (int fd);
TTYDB *ttydb_get (int fd);

int tty_open		(const char *tty_name);		/* open tty device */
int tty_canonical	(int fd);			/* put tty in canonical mode */
int tty_reset		(int fd);			/* reset tty to original state */
int tty_close		(int fd);			/* close tty */
int tty_input_size	(int fd, int *bytes);

int tty_set_lowlatency	(int fd, int enable);

int tty_raw		(int fd);			/* put tty in row mode */

ssize_t tty_read_line (int fd, char *buf, size_t count);	/* always try to read one line in both raw and canonical modes */

int uart_get (void);
int uart_put (void);

#ifdef __cplusplus
}
#endif

#endif

