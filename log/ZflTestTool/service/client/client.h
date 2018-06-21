#ifndef	_SERVICE_CLIENT_H_
#define	_SERVICE_CLIENT_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int init_client (const char *ip, int port);

extern int local_init_client (int port);
extern int local_destroy_client (int port);

#endif
