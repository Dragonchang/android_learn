#ifndef	_HTC_SERVICE_SERVER_H_
#define	_HTC_SERVICE_SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define	SOCKET_SERVER_PATH	TMP_DIR "sttsocket.s."
#define	SOCKET_CLIENT_PATH	TMP_DIR "sttsocket.c."
extern int init_server (int port);
extern int wait_for_connection (int fd);
extern int get_client_info (int sockfd, int *pid, int *uid, int *gid);
extern int get_port (const int sockfd);

extern int get_free_port (void);

extern void local_destroy_all_sockets (void);
extern int local_init_server (int port);
extern int local_destroy_server (int poer);
extern int local_wait_for_connection (int sockfd);
extern int local_get_client_info (int sockfd, int *pid, int *uid, int *gid);

#endif
