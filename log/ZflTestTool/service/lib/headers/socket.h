#ifndef	_SSD_SOCKET_H_
#define	_SSD_SOCKET_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	SOCKET_MAGIC	(0x89124138)

typedef struct {
	int	magic;
	int	length;
} SOCKET_HEAD;

typedef struct {
	int	fd;
#if 0
	int	semid;
#else
	pthread_mutex_t	mux;
#endif
} * SOCKET_ID;

extern SOCKET_ID socket_init (char *socket_server_ip, int socket_server_port);
extern void	 socket_close (SOCKET_ID id);

extern ssize_t	_socket_read (SOCKET_ID id, char *buf, size_t size, int flag);
extern ssize_t	_socket_write (SOCKET_ID id, char *buf, size_t size, int flag);

#define	socket_read(id,buf,size)	_socket_read(id,buf,size,MSG_WAITALL)
#define	socket_write(id,buf,size)	_socket_write(id,buf,size,MSG_WAITALL)

/* general socket commands:
 *
 * s:CMD_SVR_VERSION		.. c:CMD_ACK_VERSION+data
 * s:CMD_SVR_VERSION+data	.. c:CMD_ACK_VERSION+data
 * s:CMD_SVR_READ		.. c:CMD_ACK_DATA+data		.. c:CMD_ACK_READ_SUCCESS
 * s:CMD_SVR_READ+data		.. c:CMD_ACK_DATA+data		.. c:CMD_ACK_READ_SUCCESS
 * s:CMD_SVR_WRITE+data		.. c:CMD_ACK_WRITE_SUCCESS
 * s:CMD_SVR_CLOSE		.. c:CMD_CLI_CLOSE
 * c:CMD_CLI_ABORT
 */
#define CMD_ACK_CONNECTED	"ack:connected"
#define	CMD_ACK_VERSION		"ack:version"
#define	CMD_ACK_DATA		"ack:data"
#define	CMD_ACK_READ_SUCCESS	"ack:read_success"
#define	CMD_ACK_READ_FAILURE	"ack:read_failure"
#define	CMD_ACK_WRITE_SUCCESS	"ack:write_success"
#define	CMD_ACK_WRITE_FAILURE	"ack:write_failure"

#define	CMD_SVR_VERSION		"svr:version"
#define CMD_SVR_READ		"svr:read"
#define CMD_SVR_WRITE		"svr:write"
#define CMD_SVR_CLOSE		"svr:close"

#define CMD_CLI_CLOSE		"cli:close"
#define CMD_CLI_ABORT		"cli:abort"

#ifdef __cplusplus
}
#endif

#endif
