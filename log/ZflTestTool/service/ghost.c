#define	LOG_TAG "STT:ghost"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/inotify.h>

#include "headers/poll.h"

#include "common.h"
#include "server.h"

#define	VERSION	"1.8"
/*
 * 1.8	: prevent ghost failed to sync configs from sdcard2/ghost to data/ghost since sdcard is not actually mounted.
 * 1.7	: add logs in Embedded Log for debugging.
 * 1.6	: force watching a mountpoint if it was failed before.
 * 1.5	: support usb storage.
 * 1.4	: show copied byte count.
 * 1.3	: check external storage state before use it.
 * 1.2	: support specific external storage.
 * 1.1	: add .nomedia to the ghost folder.
 */

#define	GHOST_DEBUG	(0)

/* custom commands */
#define	CMD_MOUNT	":mount:"
#define	CMD_WAIT	":wait:"

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t working = (pthread_t) -1;

static POLL poller = POLL_INITIAL;

static const char *extpath = NULL;
static const char *usbpath = NULL;
static int ifd = -1;
static int wd_src = -1;
static int wd_dst = -1;
static char path_src [PATH_MAX];
static char path_dst [PATH_MAX];

/*
 * the drop list is to keep the events need to be ignored next time
 */
static GLIST_NEW (drop_list);

static int in_drop_list (uint32_t bit)
{
	return (glist_find (& drop_list, (void *) ((long) bit)) < 0) ? 0 : 1;
}

static int drop_list_length (void)
{
	return glist_length (& drop_list);
}

static void drop_add (uint32_t bit)
{
#if GHOST_DEBUG
	DM ("add 0x%08X to drop list\n", bit);
#endif
	glist_add (& drop_list, (void *) ((long) bit));
}

static void drop_remove (uint32_t bit)
{
	int idx = glist_find (& drop_list, (void *) ((long) bit));

	if (idx >= 0)
	{
	#if GHOST_DEBUG
		DM ("remove 0x%08X from drop list\n", bit);
	#endif
		glist_delete (& drop_list, idx, NULL);
	}
}

#if GHOST_DEBUG
static void drop_dump (void)
{
	int i, len = glist_length (& drop_list);

	for (i = 0; i < len; i ++)
	{
		DM ("    drop %d: 0x%08X\n", i, (uint32_t) glist_get (& drop_list, i));
	}
}
#endif

static int copy_file (const char *from, const char *to, const char *filename)
{
	char buf [PATH_MAX];
	FILE *fpr, *fpw;
	size_t count, total;
	int ret;

	DM ("[embedded] copy [%s%s] to [%s%s] ...\n", from, filename, to, filename);

	snprintf (buf, sizeof (buf) - 1, "%s%s", from, filename);
	buf [sizeof (buf) - 1] = 0;

	if ((fpr = fopen (buf, "rb")) == NULL)
	{
		DM ("copy from %s: %s\n", buf, strerror (errno));
		return -1;
	}

	snprintf (buf, sizeof (buf) - 1, "%s%s", to, filename);
	buf [sizeof (buf) - 1] = 0;

	if ((fpw = fopen (buf, "wb")) == NULL)
	{
		DM ("copy to %s: %s\n", buf, strerror (errno));
		fclose (fpr);
		return -1;
	}

	ret = 0;
	total = 0;

	while (! feof (fpr))
	{
		count = fread (buf, 1, sizeof (buf), fpr);

		if (ferror (fpr))
		{
			DM ("read from %s%s: %s\n", from, filename, strerror (errno));
			ret = -1;
			break;
		}

		if (count)
		{
			if (fwrite (buf, 1, count, fpw) != count)
			{
				DM ("write to %s%s: %s\n", to, filename, strerror (errno));
				ret = -1;
				break;
			}
			total += count;
		}
	}

	if ((ret == -1) && (total > 0))
	{
		/*
		 * target file has data, it will cause IN_CLOSE_WRITE, return 0 to make sure adding the event to drop list
		 */
		ret = 0;
	}

	/*
	 * make others accessible
	 */
	fchmod (fileno (fpw), 0777);

	fclose (fpr);
	fclose (fpw);

	DM ("[embedded] copied %u bytes.\n", (unsigned int) total);
	return ret;
}

static int copy_dir (const char *from, const char *to)
{
	DIR *dir;
	struct dirent *entry;
	int ret = 0;
	if ((dir = opendir (from)) == NULL)
	{
		DM ("[embedded] copy_dir: opendir [%s]: %s\n", from, strerror (errno));
	}
	else
	{
		while ((entry = readdir (dir)) != NULL)
		{
			DM ("[embedded] copy_dir: [%s]\n", entry->d_name);

			if (entry->d_type != DT_REG)
				continue;

			ret = copy_file (from, to, entry->d_name);

			if (ret < 0)
				break;
		}
		closedir (dir);
	}
	return ret;
}

static void restore_all_from_dst (void)
{
	copy_dir (path_dst, path_src);
}

static void copy_all_to_dst (void)
{
	copy_dir (path_src, path_dst);
}

static int delete_from_dst (const char *filename)
{
	char buf [PATH_MAX];
	snprintf (buf, sizeof (buf) - 1, "%s%s", path_dst, filename);
	buf [sizeof (buf) - 1] = 0;
	DM ("[embedded] delete [%s] ...\n", buf);
	return unlink (buf);
}

static int copy_to_dst (const char *filename)
{
	return copy_file (path_src, path_dst, filename);
}

static void inotify_close (void)
{
	if (ifd >= 0)
	{
		close (ifd);
		ifd = -1;
	}
}

static void remove_watch (void)
{
	DM ("inotify remove watch [%d:%s], [%d:%s]\n", wd_src, path_src, wd_dst, path_dst);

	if (wd_src != -1)
	{
		if (inotify_rm_watch (ifd, wd_src) < 0)
		{
			DM ("inotify_rm_watch(%d:%s) failed: %s\n", wd_src, path_src, strerror (errno));
		}
		wd_src = -1;
	}
	if (wd_dst != -1)
	{
		if (inotify_rm_watch (ifd, wd_dst) < 0)
		{
			DM ("inotify_rm_watch(%d:%s) failed: %s\n", wd_dst, path_dst, strerror (errno));
		}
		wd_dst = -1;
	}
}

static int storage_mounted (const char *dst)
{
	return ((strncmp (usbpath, dst, strlen (usbpath)) == 0) && (dir_storage_state (usbpath) == 1)) ||
		((strncmp (extpath, dst, strlen (extpath)) == 0) && (dir_storage_state (extpath) == 1));
}

static void add_watch (void)
{
#if GHOST_DEBUG
	//uint32_t mask = IN_ALL_EVENTS;
	uint32_t mask = IN_IGNORED | IN_ISDIR | IN_CLOSE_WRITE | IN_DELETE | IN_DELETE_SELF;
#else
	uint32_t mask = IN_IGNORED | IN_ISDIR | IN_CLOSE_WRITE | IN_DELETE | IN_DELETE_SELF;
#endif

	if (storage_mounted (path_dst) && (! dir_exists (path_dst)))
	{
		DM ("[embedded] dst storage of [%s] is mounted but path is not existed, create it and copy files from src!\n", path_dst);
		remove_watch (); // remove all watches before mass copy
		if (access (path_dst, F_OK) == 0) // in case it's a file, not a dir
			unlink (path_dst);
		dir_create_recursive (path_dst);
		copy_all_to_dst ();
	}

	if (! dir_exists (path_src))
	{
		DM ("[embedded] src path [%s] is not existed, create it and copy files from dst!\n", path_src);
		remove_watch (); // remove all watches before mass copy
		if (access (path_src, F_OK) == 0) // in case it's a file, not a dir
			unlink (path_src);
		dir_create_recursive (path_src);
		restore_all_from_dst ();
	}

	wd_src = inotify_add_watch (ifd, path_src, mask);

	if (wd_src < 0)
	{
		DM ("inotify_add_watch(%s) failed: %s\n", path_src, strerror (errno));
		wd_src = -1;
	}

	wd_dst = -1;

	if (storage_mounted (path_dst))
	{
		wd_dst = inotify_add_watch (ifd, path_dst, mask);

		if (wd_dst < 0)
		{
			DM ("inotify_add_watch(%s) failed: %s\n", path_dst, strerror (errno));
			wd_dst = -1;
		}
	}

	DM ("inotify watch [%d:%s], [%d:%s]\n", wd_src, path_src, wd_dst, path_dst);
}

static void sync_src_and_dst (void)
{
	char buf [PATH_MAX];
	DIR *dir;
	struct dirent *entry;
	/*
	 * copy the files not existed in dst to src
	 */
	if ((dir = opendir (path_dst)) == NULL)
	{
		DM ("[embedded] sync_src_and_dst: opendir [%s]: %s\n", path_dst, strerror (errno));
	}
	else
	{
		while ((entry = readdir (dir)) != NULL)
		{
			DM ("[embedded] sync_src_and_dst: [%s]\n", entry->d_name);

			if (entry->d_type != DT_REG)
				continue;

			snprintf (buf, sizeof (buf) - 1, "%s%s", path_src, entry->d_name);
			buf [sizeof (buf) - 1] = 0;

			if (access (buf, F_OK) != 0)
			{
				if (wd_src != -1)
				{
					remove_watch ();
				}

				/*
				 * exists in dst but not exists in src
				 */
				copy_file (path_dst, path_src, entry->d_name);
			}
		}
		closedir (dir);
	}
	/*
	 * copy all files in src to dst
	 */
	copy_dir (path_src, path_dst);
}

#if GHOST_DEBUG
/*
#define IN_ACCESS 0x00000001
#define IN_MODIFY 0x00000002
#define IN_ATTRIB 0x00000004
#define IN_CLOSE_WRITE 0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_OPEN 0x00000020
#define IN_MOVED_FROM 0x00000040
#define IN_MOVED_TO 0x00000080
#define IN_CREATE 0x00000100
#define IN_DELETE 0x00000200
#define IN_DELETE_SELF 0x00000400
#define IN_MOVE_SELF 0x00000800

#define IN_UNMOUNT 0x00002000
#define IN_Q_OVERFLOW 0x00004000
#define IN_IGNORED 0x00008000

#define IN_CLOSE (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_MOVE (IN_MOVED_FROM | IN_MOVED_TO)

#define IN_ONLYDIR 0x01000000
#define IN_DONT_FOLLOW 0x02000000
#define IN_MASK_ADD 0x20000000
#define IN_ISDIR 0x40000000
#define IN_ONESHOT 0x80000000
*/
static const char *mask2name (uint32_t bit)
{
	switch (bit)
	{
	/* events */
	case IN_ACCESS:		return "IN_ACCESS";
	case IN_ATTRIB:		return "IN_ATTRIB";
	case IN_CLOSE_WRITE:	return "IN_CLOSE_WRITE";
	case IN_CLOSE_NOWRITE:	return "IN_CLOSE_NOWRITE";
	case IN_CREATE:		return "IN_CREATE";
	case IN_DELETE:		return "IN_DELETE";
	case IN_DELETE_SELF:	return "IN_DELETE_SELF";
	case IN_MODIFY:		return "IN_MODIFY";
	case IN_MOVE_SELF:	return "IN_MOVE_SELF";
	case IN_MOVED_FROM:	return "IN_MOVED_FROM";
	case IN_MOVED_TO:	return "IN_MOVED_TO";
	case IN_OPEN:		return "IN_OPEN";

	/* special bits */
	case IN_IGNORED:	return "IN_IGNORED";
	case IN_ISDIR:		return "IN_ISDIR";
	case IN_Q_OVERFLOW:	return "IN_Q_OVERFLOW";
	case IN_UNMOUNT:	return "IN_UNMOUNT";
	}
	return "UNKNOWN";
}
#endif

static void *thread_main (void *UNUSED_VAR (null))
{
	struct inotify_event *pi;
	char buf [2048];
	int ufd = -1;
	int fds [2];
	int idx, count;
	uint32_t i;

	prctl (PR_SET_NAME, (unsigned long) "ghost:monitor", 0, 0, 0);

	if (poll_open (& poller) < 0)
	{
		DM ("cannot create pipe!\n");
		return NULL;
	}

	if ((ifd = inotify_init ()) < 0)
	{
		DM ("initialize inotify instance failed: %s\n", strerror (errno));
		poll_close (& poller);
		return NULL;
	}

	if ((ufd = local_uevent_register ("ghost")) < 0)
	{
		DM ("register local uevent failed!\n");
		poll_close (& poller);
		inotify_close ();
		return NULL;
	}

	pthread_mutex_lock (& data_lock);
	if (storage_mounted (path_dst))
	{
		DM ("[embedded] storage of [%s] is mounted.\n", path_dst);

		if (dir_exists (path_src) && dir_exists (path_dst))
		{
			DM ("[embedded] sync [%s] and [%s] ...\n", path_src, path_dst);

			sync_src_and_dst ();
		}
		else
		{
			DM ("[embedded] [%s] exist=%d, [%s] exist=%d\n", path_src, access (path_src, F_OK) == 0, path_dst, access (path_dst, F_OK) == 0);
		}

		add_watch ();
	}
	else
	{
		DM ("[embedded] storage [%s] is not yet mounted!\n", path_dst);

		if (! dir_exists (path_src))
		{
			if (access (path_src, F_OK) == 0) // in case it's a file, not a dir
				unlink (path_src);
			dir_create_recursive (path_src);
		}
	}
	pthread_mutex_unlock (& data_lock);

	fds [0] = ufd;
	fds [1] = ifd;
	count = sizeof (buf);

	for (;;)
	{
		idx = poll_multiple_wait (& poller, -1, fds, sizeof (fds) / sizeof (int));

		if (idx <= 0)
		{
			/* error or user break */
			break;
		}

		memset (buf, 0, count);

		count = read (fds [-- idx], buf, sizeof (buf) - 1);

		if (count <= 0)
		{
			DM ("read %s failed: %s\n", (idx == 0) ? "uevent" : "inotify", strerror (errno));
			break;
		}

		if (idx == 0 /* uevent */)
		{
			if (local_uevent_is_ended (buf))
				break;

		#if 0
			/*
			 * uevent [add@/devices/platform/msm_sdcc.4/mmc_host/mmc2/mmc2:8f03]
			 * uevent [add@/devices/platform/msm_sdcc.4/mmc_host/mmc2/mmc2:8f03/block/mmcblk1]
			 * uevent [add@/devices/platform/msm_sdcc.4/mmc_host/mmc2/mmc2:8f03/block/mmcblk1/mmcblk1p1]
			 * uevent [add@/devices/virtual/bdi/179:32]
			 *
			 * uevent [remove@/devices/platform/msm_sdcc.4/mmc_host/mmc2/mmc2:8f03/block/mmcblk1/mmcblk1p1]
			 * uevent [remove@/devices/virtual/bdi/179:32]
			 * uevent [remove@/devices/platform/msm_sdcc.4/mmc_host/mmc2/mmc2:8f03/block/mmcblk1]
			 * uevent [remove@/devices/platform/msm_sdcc.4/mmc_host/mmc2/mmc2:8f03]
			 */
			if (strstr (buf, "/mmc_host/") && (! strstr (buf, "/block/")) && ((strncmp (buf, "add@", 4) == 0) || (strncmp (buf, "remvoe@", 7) == 0)))
			{
				DM ("uevent [%s]\n", buf);
			}
		#endif
		}
		else if (idx == 1 /* inotify */)
		{
			int is_src;

			pthread_mutex_lock (& data_lock);

			for (pi = (struct inotify_event *) buf; (char *) pi < & buf [count]; pi = (struct inotify_event *) (((char *) pi) + sizeof (struct inotify_event) + pi->len))
			{
				is_src = (pi->wd == wd_src) ? 1 : ((pi->wd == wd_dst) ? 0 : -1);

				DM ("inotify [%d:[%s], mask:0x%08X, cookie:0x%08X, len:%d, name:[%s]]\n", pi->wd, (is_src == 1) ? path_src : ((is_src == 0) ? path_dst : "???"), pi->mask, pi->cookie, pi->len, (pi->len > 0) ? pi->name : "");

			#if GHOST_DEBUG
				for (i = 0x80000000; i != 0; i >>= 1)
				{
					if (pi->mask & i)
					{
						DM ("    0x%08X: %s\n", i, mask2name (pi->mask & i));
					}
				}
			#endif

				if (is_src < 0)
				{
					/* just ignore */
					continue;
				}

				if (pi->mask & IN_IGNORED)
				{
					if (is_src)
					{
						wd_src = -1;

						if (! dir_exists (path_src))
						{
							if (access (path_src, F_OK) == 0) // in case it's a file, not a dir
								unlink (path_src);
							if (dir_create_recursive (path_src) == 0)
							{
								restore_all_from_dst ();
								add_watch ();
							}
						}
					}
					else
					{
						wd_dst = -1;

						if (storage_mounted (path_dst) && (! dir_exists (path_dst)))
						{
							if (access (path_dst, F_OK) == 0) // in case it's a file, not a dir
								unlink (path_dst);
							if (dir_create_recursive (path_dst) == 0)
							{
								copy_all_to_dst ();
								add_watch ();
							}
						}
					}
					continue;
				}

				if ((pi->len == 0) || (pi->mask & IN_ISDIR) || (pi->mask & IN_DELETE_SELF))
					continue;

				/*
				 * below we need only IN_DELETE and IN_CLOSE_WRITE
				 */
				if (! (pi->mask & (IN_DELETE | IN_CLOSE_WRITE)))
					continue;

				if (pi->name [pi->len - 1] != 0)
				{
					pi->name [pi->len - 1] = 0;

					DM ("fix name [%s]\n", pi->name);
				}

				if (is_src)
				{
					if (wd_dst < 0)
					{
						/* nothing to do */
						DM ("    ignore event\n");
					}
					else if (pi->mask & IN_DELETE)
					{
						/* delete dst too */
						if (delete_from_dst (pi->name) == 0)
						{
							drop_add (IN_DELETE);
						}
					}
					else if (pi->mask & IN_CLOSE_WRITE)
					{
						/* copy to dst */
						if (copy_to_dst (pi->name) == 0)
						{
							drop_add (IN_CLOSE_WRITE);
						}
					}
				}
				else
				{
					char f [PATH_MAX];

					snprintf (f, sizeof (f) - 1, "%s%s", path_src, pi->name);
					f [sizeof (f) - 1] = 0;

					if (pi->mask & IN_DELETE)
					{
						if (in_drop_list (IN_DELETE))
						{
							DM ("    ignore event 0x%08X\n", IN_DELETE);
							drop_remove (IN_DELETE);
						}
						else if (access (f, F_OK) == 0)
						{
							/* existed in src, copy to dst */
							if (copy_to_dst (pi->name) == 0)
							{
								drop_add (IN_CLOSE_WRITE);
							}
						}
					}
					else if (pi->mask & IN_CLOSE_WRITE)
					{
						if (in_drop_list (IN_CLOSE_WRITE))
						{
							DM ("    ignore event 0x%08X\n", IN_CLOSE_WRITE);
							drop_remove (IN_CLOSE_WRITE);
						}
						else if (access (f, F_OK) == 0)
						{
							/* existed in src, copy to dst */
							if (copy_to_dst (pi->name) == 0)
							{
								drop_add (IN_CLOSE_WRITE);
							}
						}
						else
						{
							/* not existed in src, delete */
							if (delete_from_dst (pi->name) == 0)
							{
								drop_add (IN_DELETE);
							}
						}
					}
				}
			#if GHOST_DEBUG
				drop_dump ();
			#endif
			}

			pthread_mutex_unlock (& data_lock);
		}
	}

	local_uevent_unregister ("ghost");

	poll_close (& poller);

	pthread_mutex_lock (& data_lock);
	remove_watch ();
	inotify_close ();
	pthread_mutex_unlock (& data_lock);
	return NULL;
}

static int process_command (char *command, char *response)
{
	if (CMP_CMD (command, CMD_ENDSERVER))
	{
		strcpy (response, "0");
		return -1;
	}

	if (CMP_CMD (command, CMD_GETVER))
	{
		strcpy (response, VERSION);
	}
	else if (CMP_CMD (command, CMD_WAIT))
	{
		int i;
		for (i = 0; i < 5; i ++)
		{
			DM ("wait for syncing done ...\n");
			usleep (100000);
			pthread_mutex_lock (& data_lock);
			if (drop_list_length () == 0) i = 65535;
			pthread_mutex_unlock (& data_lock);
		}
		DM ("end of waiting\n");
		strcpy (response, "0");
	}
	else if (CMP_CMD (command, CMD_MOUNT))
	{
		const char *mountpoint;
		int mount;

		MAKE_DATA (command, CMD_MOUNT);

		mount = (command [0] == '0') ? 0 : 1;
		mountpoint = & command [2];

		DM ("mountpoint=[%s][%d], path_dst=[%s], extpath=[%s]\n", mountpoint, mount, path_dst, extpath);

		if (! strcmp (mountpoint, usbpath))
		{
			if (mount)
			{
				mountpoint = usbpath;
			}
			else if (dir_storage_state (extpath) == 1 /* ext mounted */)
			{
				mountpoint = extpath;
			}
			else
			{
				mountpoint = NULL;
			}
		}
		else if (! strcmp (mountpoint, extpath))
		{
			if (mount)
			{
				if (dir_storage_state (usbpath) == 1 /* usb not mounted */)
				{
					mountpoint = usbpath;
				}
				else
				{
					mountpoint = extpath;
				}
			}
			else if (dir_storage_state (usbpath) == 1 /* usb mounted */)
			{
				mountpoint = usbpath;
			}
			else
			{
				mountpoint = NULL;
			}
		}
		else
		{
			DM ("ignore mountpoint [%s][%d]!\n", mountpoint, mount);
			strcpy (response, "-1");
			return 0;
		}

		DM ("selected mountpoint=[%s]\n", mountpoint);

		if ((mountpoint != NULL) && ((wd_dst == -1) || (strncmp (mountpoint, path_dst, strlen (mountpoint)) != 0)))
		{
			pthread_mutex_lock (& data_lock);

			remove_watch ();

			snprintf (path_dst, sizeof (path_dst), "%s/" GHOST_FOLDER_NAME, mountpoint);
			path_dst [sizeof (path_dst) - 1] = 0;

			if (dir_exists (path_src) && dir_exists (path_dst))
			{
				sync_src_and_dst ();
			}
			add_watch ();

			pthread_mutex_unlock (& data_lock);
		}

		strcpy (response, "0");
	}
	else
	{
		DM ("unknown command [%s]!\n", command);
		strcpy (response, "-1");
	}
	return 0;
}

static void ghost_dir_get_storage (void)
{
	if ((extpath = dir_get_external_storage ()) == NULL)
		return;

	if ((usbpath = dir_get_usb_storage ()) == NULL)
		return;

	strncpy (path_src, GHOST_DIR, sizeof (path_src));
	path_src [sizeof (path_src) - 1] = 0;

	dir_no_media (path_src);

	if (dir_storage_state (usbpath) == 1 /* mounted */)
	{
		snprintf (path_dst, sizeof (path_dst), "%s/" GHOST_FOLDER_NAME, usbpath);
		path_dst [sizeof (path_dst) - 1] = 0;
		dir_no_media (path_dst);
	}
	else
	{
		snprintf (path_dst, sizeof (path_dst), "%s/" GHOST_FOLDER_NAME, extpath);
		path_dst [sizeof (path_dst) - 1] = 0;

		if (dir_storage_state (extpath) == 1 /* mounted */)
		{
			dir_no_media (path_dst);
		}
	}
}

int ghost_main (int server_socket)
{
	char buffer [PATH_MAX + 16];
	int commfd = -1;
	int ret = 0;
	DM ("ghost_main\n");

	ghost_dir_get_storage ();

	if (pthread_create (& working, NULL, thread_main, NULL) < 0)
		return -1;

	for (;;)
	{
		DM ("waiting connection ...\n");

		commfd = wait_for_connection (server_socket);

		if (commfd < 0)
		{
			DM ("accept client connection failed!\n");
			continue;
		}

		DM ("connection established.\n");

		ghost_dir_get_storage ();

		for (;;)
		{
			memset (buffer, 0, sizeof (buffer));

			ret = read (commfd, buffer, sizeof (buffer));

			if (ret <= 0)
			{
				DM ("read command error (%d)! close connection!\n", ret);
				break;
			}

			buffer [sizeof (buffer) - 1] = 0;

			DM ("read command [%s].\n", buffer);

			if (process_command (buffer, buffer) < 0)
				break;

			DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close (commfd);
		commfd = -1;
	}

	if (working != (pthread_t) -1)
	{
		poll_break (& poller);
		working = (pthread_t) -1;
		pthread_join (working, NULL);
	}
	return 0;
}
