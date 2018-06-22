#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/ipc.h>

#if 0
#include <sys/sem.h>
#endif

#define	LOG_TAG "STT:socket"

#include <utils/Log.h>

#include "libcommon.h"
#include "headers/socket.h"

#define	SOCKET_DATA_MAX_LEN	(1024)

/* wait for connection is established.
 * return socket fd.
 */
SOCKET_ID socket_init (char *socket_server_ip, int socket_server_port)
{
	SOCKET_ID id;

	key_t key;

	struct sockaddr_in sin;
	int addr_size = sizeof (struct sockaddr_in);

	if ((! socket_server_ip) || (socket_server_port <= 0))
	{
		fLOGE ("unknown server [%s:%d]!\n", socket_server_ip, socket_server_port);
		return NULL;
	}

	/* alloc id */
	id = malloc (sizeof (SOCKET_ID));

	if (id == NULL)
	{
		fLOGE ("no memory!");
		return NULL;
	}

	id->fd = -1;
#if 0
	id->semid = -1;

	/* get semaphore */
	key = ftok (socket_server_ip, socket_server_port);

	id->semid = semget (key, 1, 0);

	if (id->semid == -1)
	{
		union semun arg;

		id->semid = semget (key, 1, IPC_CREAT | 0666);

		if (id->semid == -1)
		{
			fLOGE ("create semaphore failed!\n");
			goto failed;
		}

		arg.val = 1;

		semctl (id->semid, 0, SETVAL, arg);
	}
#else
	pthread_mutex_init (& id->mux, NULL);
#endif

	/* open socket */
	if ((id->fd = socket (PF_INET, SOCK_STREAM, 0)) < 0)
	{
		fLOGE ("create socket error!\n");
		goto failed;
	}

	/* initialize the server descriptor */
	memset (& sin, 0, sizeof (sin));

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr (socket_server_ip);
	sin.sin_port = htons (socket_server_port);

	/* try to connet to the server */
	while (connect (id->fd, (struct sockaddr *) & sin, addr_size) < 0);

	fLOGD ("connected.\n");

	return id;

failed:
	socket_close (id);
	return NULL;
}

void socket_close (SOCKET_ID id)
{
	if (id)
	{
		if (id->fd != -1) close (id->fd);
#if 0
		if (id->semid != -1) semctl (id->semid, 0, IPC_RMID, 0);
#else
		pthread_mutex_destroy (& id->mux);
#endif
		free (id);
	}
}

ssize_t _socket_read (SOCKET_ID id, char *buf, size_t size, int flag)
{
	SOCKET_HEAD h = { SOCKET_MAGIC, 0 };

	ssize_t count;
#if 0
	struct sembuf lock;

	lock.sem_num = 0;
	lock.sem_flg = SEM_UNDO;

	lock.sem_op = -1;
	semop (id->semid, & lock, 1);
#else
	pthread_mutex_lock (& id->mux);
#endif

	if ((recv (id->fd, & h, sizeof (SOCKET_HEAD), flag) != sizeof (SOCKET_HEAD)) || (h.magic != (int) SOCKET_MAGIC))
	{
		fLOGD ("unknown magic!");
		return -1;
	}

	fLOGD ("recv head magic=0x%x, length=%d\n", h.magic, h.length);

	count = recv (id->fd, buf, ((long) size > h.length) ? (size_t) h.length : size, flag);

#if 0
	lock.sem_op = 1;
	semop (id->semid, & lock, 1);
#else
	pthread_mutex_unlock (& id->mux);
#endif
	return count;
}

ssize_t _socket_write (SOCKET_ID id, char *buf, size_t size, int flag)
{
	SOCKET_HEAD h = { SOCKET_MAGIC, 0 };

	ssize_t count;
#if 0
	struct sembuf lock;

	lock.sem_num = 0;
	lock.sem_flg = SEM_UNDO;

	lock.sem_op = -1;
	semop (id->semid, & lock, 1);
#else
	pthread_mutex_lock (& id->mux);
#endif

	h.length = size;

	count = send (id->fd, & h, sizeof (SOCKET_HEAD), flag);

	if (count != sizeof (SOCKET_HEAD))
	{
		return -1;
	}

	fLOGD ("send head magic=0x%x, length=%d\n", h.magic, h.length);

	count = send (id->fd, buf, size, flag);

#if 0
	lock.sem_op = 1;
	semop (id->semid, & lock, 1);
#else
	pthread_mutex_unlock (& id->mux);
#endif
	return count;
}
