#ifndef	_SSD_SOCKET_COMM_H_
#define	_SSD_SOCKET_COMm_H_

#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

SOCKET_ID init_comm(char *socket_server_ip, int socket_server_port);
void close_comm(SOCKET_ID socket_id);
void abort_comm(SOCKET_ID socket_id, int exit_code);

#ifdef __cplusplus
}
#endif

#endif
