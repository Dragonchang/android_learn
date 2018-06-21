#ifndef _PCLINK_H_INCLUDED__
#define _PCLINK_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Auto select tty node on device.
 */

typedef enum {
	PCLINK_CANONICAL = 0,
	PCLINK_RAW = 1
} pclink_mode;

typedef enum {
	PCLINK_NONE = 0,
	PCLINK_UART = 1,
	PCLINK_USB = 2,
	PCLINK_PIPE = 3,
	PCLINK_SOCKET_SERVER = 4,
	PCLINK_SOCKET_CLIENT = 5
} pclink_type;

typedef struct {
	int in_use;
	int is_connected;
	pclink_type type;
	char *ip;
	int port;
	int server_fd;
	int comm_fd;
} pclink_socket_info;

extern int service_init_client (int port);
extern int service_send_command (int fd, const char *cmd, char *buf, int len);

extern void pclink_socket_init (pclink_socket_info *psi);
extern int pclink_socket_in_use (pclink_socket_info *psi);
extern int pclink_socket_is_connected (pclink_socket_info *psi);
extern pclink_type pclink_socket_type (pclink_socket_info *psi);
extern int pclink_socket_open (pclink_socket_info *psi, pclink_mode mode, pclink_type type, const char *hostinfo /* xx.xx.xx.xx:xxxx */);
extern int pclink_socket_accept (pclink_socket_info *psi);
extern int pclink_socket_connect (pclink_socket_info *psi);
extern int pclink_socket_close (pclink_socket_info *psi);

extern int pclink_open (pclink_mode mode);
extern int pclink_open_ex (pclink_mode mode, pclink_type type, const char *devpath);
extern int pclink_close (int fd);
extern int pclink_read (int fd, char *buffer, int len);
extern int pclink_write (int fd, char *buffer, int len);

#ifndef UNUSED_VAR
#define UNUSED_VAR(x) UNUSED_ ## x __attribute__((__unused__))
#endif

#ifdef __cplusplus
}
#endif

#endif
