#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>

#include "headers/socket.h"

#define LOG_TAG				"Socket_comm"

#include "headers/utils.h"

SOCKET_ID init_comm(char *socket_server_ip, int socket_server_port)
{
	char *ack_string = CMD_ACK_CONNECTED;
	SOCKET_ID socket_id;

	//D("init_comm: ip=%s, port=%d\n", socket_server_ip, socket_server_port);
	socket_id = socket_init(socket_server_ip, socket_server_port);

	if (!socket_id)
		return socket_id;

	sleep (1);

	socket_write(socket_id, ack_string, strlen(ack_string));

	//D("init_comm: completed, fd=%d", socket_id->fd);
	return 0;
}

void close_comm(SOCKET_ID socket_id)
{
	//D("close_comm: fd=%d\n", socket_id->fd);
	socket_write(socket_id, CMD_CLI_CLOSE, strlen(CMD_CLI_CLOSE));
	socket_close(socket_id);
}

void abort_comm(SOCKET_ID socket_id, int exit_code)
{
	char buf[64];

	//D("close_comm: fd=%d\n", socket_id->fd);
	memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s %d", CMD_CLI_ABORT, exit_code);

	socket_write(socket_id, buf, strlen(buf));
	socket_close(socket_id);
}

#ifdef __cplusplus
}
#endif
