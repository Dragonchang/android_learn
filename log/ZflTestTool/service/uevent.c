#define	LOG_TAG		"STT:local_uevent"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "common.h"

#define	MAX_LOCAL_UEVENT_SLOT	4

typedef struct {
	char *key;
	int pipefds [2];
} LOCAL_UEVENT;

static pthread_mutex_t uevent_lock = PTHREAD_MUTEX_INITIALIZER;
static LOCAL_UEVENT *list = NULL;

static int add_node (const char *key) /* return idx */
{
	int i;
	if (list)
	{
		for (i = 0; i < MAX_LOCAL_UEVENT_SLOT; i ++)
		{
			if (! list [i].key)
			{
				if ((list [i].key = strdup (key)) == NULL)
					break;

				if (pipe (list [i].pipefds) != 0)
				{
					free (list [i].key);
					list [i].key = NULL;
					break;
				}

				return i;
			}
		}
	}
	return -1;
}

static void del_node (int i)
{
	if (list && list [i].key)
	{
		close (list [i].pipefds [0]);
		close (list [i].pipefds [1]);
		free (list [i].key);
		list [i].key = NULL;
	}
}

void local_uevent_initial (void)
{
	pthread_mutex_lock (& uevent_lock);
	if (! list)
	{
		if ((list = (LOCAL_UEVENT *) malloc (sizeof (LOCAL_UEVENT) * MAX_LOCAL_UEVENT_SLOT)) != NULL)
		{
			memset (list, 0, sizeof (LOCAL_UEVENT) * MAX_LOCAL_UEVENT_SLOT);
		}
	}
	pthread_mutex_unlock (& uevent_lock);
}

void local_uevent_destroy (void)
{
	int i;
	pthread_mutex_lock (& uevent_lock);
	if (list)
	{
		for (i = 0; i < MAX_LOCAL_UEVENT_SLOT; i ++)
			if (list [i].key)
				del_node (i);
		free (list);
		list = NULL;
	}
	pthread_mutex_unlock (& uevent_lock);
}

void local_uevent_dispatch (const char *uevent)
{
	int i;
	pthread_mutex_lock (& uevent_lock);
	if (list)
	{
		for (i = 0; i < MAX_LOCAL_UEVENT_SLOT; i ++)
		{
			//DM ("check %d:[%s][dispatch:%d][read:%d]\n", i, list [i].key, list [i].pipefds [0], list [i].pipefds [1]);
			if (list [i].key)
			{
				//DM ("  dispatch event to [%s]\n", list [i].key);
				if (write (list [i].pipefds [1], uevent, strlen (uevent) + 1) < 0)
				{
					DM ("failed to dispatch: %s\n", strerror (errno));
				}
			}
		}
	}
	pthread_mutex_unlock (& uevent_lock);
}

int local_uevent_register (const char *key)
{
	int i;
	pthread_mutex_lock (& uevent_lock);
	i = add_node (key);
	if (i != -1)
	{
		i = list [i].pipefds [0];
	}
	pthread_mutex_unlock (& uevent_lock);
	return i;
}

void local_uevent_unregister (const char *key)
{
	int i;
	pthread_mutex_lock (& uevent_lock);
	if (list)
	{
		for (i = 0; i < MAX_LOCAL_UEVENT_SLOT; i ++)
		{
			if ((list [i].key) && (! strcmp (list [i].key, key)))
			{
				del_node (i);
				break;
			}
		}
	}
	pthread_mutex_unlock (& uevent_lock);
}

void local_uevent_try_to_end (void)
{
	local_uevent_dispatch ("end");
}

int local_uevent_is_ended (const char *uevent)
{
	return (strcmp (uevent, "end") == 0);
}
