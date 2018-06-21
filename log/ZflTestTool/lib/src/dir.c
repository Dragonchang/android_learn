#define	LOG_TAG		"STT:dir"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/statfs.h>

#include <cutils/log.h>

#include "libcommon.h"
#include "headers/dir.h"

static pthread_mutex_t dir_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *htc_fuse_storages [] = {
	/*
	 * for fuse multi-user support, list the paths that java app uses
	 *
	 * assume they're symbolic links that link to real mount points
	 */
	 "/storage/emulated/0",
	"/storage/emulated/legacy",
	NULL
};
static const char *active_fuse_storage = NULL;

static const char *fuse_mountpoint = NULL;

static const char *fuse_mountpoint_paths [] = {
	"/mnt/shell/emulated",
	"/storage/emulated",
	NULL
};

static const char *htc_phone_storages [] = {
	"/sdcard",
	NULL
};

static const char *htc_external_storages [] = {
	"/sdcard2",
	"/sdcard",
	NULL
};

static const char *htc_usb_storages [] = {
	"/mnt/usb",
	NULL
};

static char path_usb [PATH_MAX] = "";
static char path_external [PATH_MAX] = "";
static char path_phone [PATH_MAX] = "";
static const char path_internal [] = "/data";

void dir_clear_storage_path (void)
{
	pthread_mutex_lock (& dir_lock);
	memset (path_usb, 0, sizeof (path_usb));
	memset (path_external, 0, sizeof (path_external));
	memset (path_phone, 0, sizeof (path_phone));
	pthread_mutex_unlock (& dir_lock);
}

char dir_get_storage_code (const char *path)
{
	if (path)
	{
		if (! path_usb [0]) (void) dir_get_usb_storage ();
		if (! path_external [0]) (void) dir_get_external_storage ();
		if (! path_phone [0]) (void) dir_get_phone_storage ();

		if (path_usb [0] && (strncmp (path_usb, path, strlen (path_usb)) == 0))
			return STORAGE_CODE_USB;

		if (path_external [0] && (strncmp (path_external, path, strlen (path_external)) == 0))
			return STORAGE_CODE_EXTERNAL;

		if (path_phone [0] && (strncmp (path_phone, path, strlen (path_phone)) == 0))
			return STORAGE_CODE_PHONE;

		if (strncmp (DAT_DIR, path, strlen (DAT_DIR)) == 0)
			return STORAGE_CODE_INTERNAL;
	}
	return STORAGE_CODE_UNKNOWN;
}

static void correct_path (char *buf, int len)
{
	int i;

	for (i = 0; buf [i] && (i < len); i ++)
	{
		if (isalnum (buf [i]) || (buf [i] == '/') || (buf [i] == '-') || (buf [i] == '_') || (buf [i] == ' '))
			continue;

		buf [i] = 0;
		break;
	}
}

static int dir_get_storage (char *realpath, int len, const char **storages)
{
	char linkpath [PATH_MAX];
	int i;

	if ((! realpath) || (! storages))
		return -1;

	realpath [0] = 0;

	for (i = 0; storages [i] != NULL; i ++)
	{
		fLOGD_IF ("--> uid %d checking %s", getuid (), storages [i]);

		bzero (realpath, len);

		/*
		 * is a symbolic link?
		 */
		strncpy (linkpath, storages [i], sizeof (linkpath) - 1);
		linkpath [sizeof (linkpath) - 1] = 0;

		if (readlink (linkpath, realpath, len - 1) >= 0)
		{
			do
			{
				if (active_fuse_storage && (strcmp (active_fuse_storage, realpath) == 0))
				{
					fLOGD_IF ("--> use fuse path [%s]", realpath);
					strncpy (linkpath, realpath, sizeof (linkpath) - 1);
					linkpath [sizeof (linkpath) - 1] = 0;
					break;
				}

				fLOGD_IF ("--> link [%s] to [%s]", linkpath, realpath);

				strncpy (linkpath, realpath, sizeof (linkpath) - 1);
				linkpath [sizeof (linkpath) - 1] = 0;

				bzero (realpath, len);

				if (readlink (linkpath, realpath, len - 1) < 0)
				{
					fLOGD_IF ("--> readlink [%s]: %s", linkpath, strerror (errno));
					break;
				}
			}
			while (1);

			strncpy (realpath, linkpath, len - 1);
			realpath [len - 1] = 0;
			break;
		}

		/*
		 * does the path exist? this way would fail if the mount point did not exist before mounted
		 */
		if (access (storages [i], F_OK) == 0)
		{
			strncpy (realpath, storages [i], len - 1);
			realpath [len - 1] = 0;

			fLOGD_IF ("--> exist %s", realpath);
			break;
		}
	}

	return realpath [0] ? 0 : -1;
}

static void detect_multi_user_fuse_storage_nolock ()
{
	int i;

	if (! active_fuse_storage)
	{
		for (i = 0; htc_fuse_storages [i] != NULL; i ++)
		{
			if (access (htc_fuse_storages [i], F_OK) == 0)
			{
				active_fuse_storage = htc_fuse_storages [i];
				break;
			}
		}
	}

	fLOGD_IF ("detected multi-user fuse storage [%s]", active_fuse_storage);
}

static void dir_set_fuse_mountpoint_nolock ()
{
	int i;

	if (! fuse_mountpoint)
	{
		for (i = 0; fuse_mountpoint_paths [i] != NULL; i ++)
		{
			if (access (fuse_mountpoint_paths [i], F_OK) == 0)
			{
				fuse_mountpoint = fuse_mountpoint_paths [i];
				break;
			}
		}
	}

	fLOGD_IF ("set fuse mountpoint [%s]\n", fuse_mountpoint);
}

static const char *dir_get_usb_storage_nolock (void)
{
	if (path_usb [0])
	{
		fLOGD_IF ("usb --> use previous %s", path_usb);
		return path_usb;
	}

	if (dir_get_storage (path_usb, sizeof (path_usb), htc_usb_storages) < 0)
	{
		const char *env = "USB_STORAGE";
		const char *ext = getenv (env);

		if (! ext)
		{
			env = "default";
			ext = "/mnt/usb";
		}
		else if (ext [0] == 0)
		{
			usleep (100000);

			ext = getenv (env);

			if (! ext)
			{
				env = "default";
				ext = "/mnt/usb";
			}
		}

		strncpy (path_usb, ext, sizeof (path_usb) - 1);
		path_usb [sizeof (path_usb) - 1] = 0;

		fLOGD_IF ("usb --> %s %s", env, path_usb);
	}

	correct_path (path_usb, sizeof (path_usb));

	fLOGD_IF ("usb --> use %s", path_usb);
	return path_usb;
}

const char *dir_get_usb_storage (void)
{
	const char *ret;
	pthread_mutex_lock (& dir_lock);
	ret = dir_get_usb_storage_nolock ();
	pthread_mutex_unlock (& dir_lock);
	return ret;
}

static const char *dir_get_external_storage_nolock (void)
{
	/*
	if (path_external [0])
	{
		fLOGD_IF ("external --> use previous %s", path_external);
		return path_external;
	}
	*/

	/*
	 * try detect multi-user fuse storage
	 */
	detect_multi_user_fuse_storage_nolock ();

	if (dir_get_storage (path_external, sizeof (path_external), htc_external_storages) < 0)
	{
		const char *env = "EXTERNAL_STORAGE";
		const char *ext = getenv (env);

		if (! ext)
		{
			env = "default";
			ext = "/sdcard";
		}
		else if (ext [0] == 0)
		{
			usleep (100000);

			ext = getenv (env);

			if (! ext)
			{
				env = "default";
				ext = "/sdcard";
			}
		}

		strncpy (path_external, ext, sizeof (path_external) - 1);
		path_external [sizeof (path_external) - 1] = 0;

		fLOGD_IF ("external --> %s %s", env, path_external);
	}

	correct_path (path_external, sizeof (path_external));

	fLOGD_IF ("external --> use %s", path_external);
	return path_external;
}

const char *dir_get_external_storage (void)
{
	const char *ret;
	pthread_mutex_lock (& dir_lock);
	ret = dir_get_external_storage_nolock ();
	pthread_mutex_unlock (& dir_lock);
	return ret;
}

static const char *dir_get_phone_storage_nolock (void)
{
	const char *extpath;
	/*
	if (path_phone [0])
	{
		fLOGD_IF ("phone --> use previous %s", path_phone);
		return path_phone;
	}
	*/

	dir_set_fuse_mountpoint_nolock ();

	/*
	 * try detect multi-user fuse storage
	 */
	detect_multi_user_fuse_storage_nolock ();

	/*
	 * make sure getting external storage path before phone storage
	 */
	extpath = dir_get_external_storage_nolock ();

	if ((dir_get_storage (path_phone, sizeof (path_phone), htc_phone_storages) < 0) || (strcmp (path_phone, extpath) == 0))
	{
		const char *env = "PHONE_STORAGE";
		const char *ext = getenv (env);

		if (! ext)
		{
			env = "default";
			ext = "/emmc";
		}
		else if (ext [0] == 0)
		{
			usleep (100000);

			ext = getenv (env);

			if (! ext)
			{
				env = "default";
				ext = "/emmc";
			}
		}

		strncpy (path_phone, ext, sizeof (path_phone) - 1);
		path_phone [sizeof (path_phone) - 1] = 0;

		fLOGD_IF ("phone --> %s %s", env, path_phone);
	}

	correct_path (path_phone, sizeof (path_phone));

	fLOGD_IF ("phone --> use %s", path_phone);
	return path_phone;
}

const char *dir_get_phone_storage (void)
{
	const char *ret;
	pthread_mutex_lock (& dir_lock);
	ret = dir_get_phone_storage_nolock ();
	pthread_mutex_unlock (& dir_lock);
	return ret;
}

const char *dir_get_known_storage (const char *storage_name)
{
	const char *ret = NULL;

	if (storage_name)
	{
		if (strcmp (storage_name, STORAGE_KEY_USB) == 0)
		{
			ret = dir_get_usb_storage ();
		}
		else if (strcmp (storage_name, STORAGE_KEY_EXTERNAL) == 0)
		{
			ret = dir_get_external_storage ();
		}
		else if (strcmp (storage_name, STORAGE_KEY_PHONE) == 0)
		{
			ret = dir_get_phone_storage ();
		}
		else if (strcmp (storage_name, STORAGE_KEY_INTERNAL) == 0)
		{
			ret = path_internal;
		}
	}
	return ret;
}

int dir_get_mount_entry (const char *mountpoint, STORAGE_MOUNT_ENTRY *pme)
{
	char mounts [64] = "/proc/mounts";
	char data [PATH_MAX];
	char ch;
	int mounted = 0;
	int idx, count, fd;
	int isfuse;

	if (! mountpoint)
		return mounted;

	/*
	 * protect active_fuse_storage with lock
	 */
	pthread_mutex_lock (& dir_lock);
	isfuse = active_fuse_storage && (strcmp (active_fuse_storage, mountpoint) == 0);
	pthread_mutex_unlock (& dir_lock);

	if ((fd = open (mounts, O_RDONLY)) < 0)
	{
		fLOGE ("open [%s]: %s", mounts, strerror (errno));
		return mounted;
	}

	if (isfuse /* is a multi-user fuse storage */)
	{
		fLOGD_IF ("detected fuse, replace mountpoint [%s] with [%s]\n", mountpoint, fuse_mountpoint);

		snprintf (mounts, sizeof (mounts), " %s ", fuse_mountpoint);
	}
	else
	{
		snprintf (mounts, sizeof (mounts), " %s ", mountpoint);
	}

	mounts [sizeof (mounts) - 1] = 0;

	for (idx = 0, ch = 0;;)
	{
		count = read (fd, & ch, sizeof (char));

		if ((ch == '\n') || (count <= 0))
		{
			data [idx] = 0;

			if (strstr (data, mounts) == NULL)
			{
				//fLOGD_IF ("  [%s] <-> [%s]", mounts, data);
			}
			else
			{
				mounted = 1;

				if (pme)
				{
					strncpy (pme->entry, data, sizeof (pme->entry) - 1);
					pme->entry [sizeof (pme->entry) - 1] = 0;

					pme->device = pme->entry;

					pme->mountpoint = strchr (pme->device, ' ');
					if (! pme->mountpoint) break;
					*pme->mountpoint ++ = 0;

					pme->type = strchr (pme->mountpoint, ' ');
					if (! pme->type) break;
					*pme->type ++ = 0;

					pme->options = strchr (pme->type, ' ');
					if (! pme->options) break;
					*pme->options ++ = 0;

					fLOGD_IF ("  [%s] <-> [%s][%s][%s][%s]", mounts, pme->device, pme->mountpoint, pme->type, pme->options);
				}
				else
				{
					fLOGD_IF ("  [%s] <-> [%s]", mounts, data);
				}
				break;
			}

			if (count <= 0)
			{
				mounted = 0;
				break;
			}

			idx = 0;
			continue;
		}

		if (idx < (int) (sizeof (data) - 1))
		{
			data [idx ++] = ch;
		}
	}

	close (fd);

	//fLOGD_IF ("  [%s] mounted = [%d]", mounts, mounted);
	return mounted;
}

int dir_fuse_state ()
{
	return dir_get_mount_entry (fuse_mountpoint, NULL);
}

int dir_storage_state (const char *storage_path)
{
	return dir_get_mount_entry (storage_path, NULL);
}

int dir_exists (const char *path)
{
	struct stat st;

	if (stat (path, & st) < 0)
	{
		fLOGE ("stat [%s]: %s\n", path, strerror (errno));
		return 0;
	}

	return (S_ISDIR (st.st_mode) ? 1 : 0);
}

static int unlink_file (const char *path)
{
	char *pbuf;
	char *p;
	int ret, err;

	if (path [strlen (path) - 1] != '/')
		return unlink (path);

	pbuf = strdup (path);

	if (! pbuf)
		return unlink (path);

	for (p = & pbuf [strlen (pbuf) - 1]; (p != pbuf) && (*p == '/'); p --)
		*p = 0;

	ret = unlink (pbuf);
	err = errno;

	free (pbuf);

	errno = err;
	return ret;
}

int dir_create_recursive (const char *path)
{
	char *pbuf;

	if (dir_exists (path))
		return 0;

	pbuf = strdup (path);

	if (pbuf)
	{
		char *p;

		p = & pbuf [strlen (pbuf) - 1];

		if (*p == '/') *p = 0;

		p = pbuf;

		if (*p == '/') p ++;

		for (p = strchr (p, '/'); p; p = strchr (p, '/'))
		{
			*p = 0;

			if (mkdir (pbuf, DEFAULT_DIR_MODE) < 0)
			{
				if (errno == EEXIST)
				{
					/* do not show EEXIST error here unless it's not a directory */
					if (! dir_exists (pbuf))
					{
						fLOGW ("mkdir [%s]: path was existed but not a directory! force removing the invalid file and try again...\n", pbuf);

						unlink_file (pbuf);

						mkdir (pbuf, DEFAULT_DIR_MODE);
					}
				}
				else
				{
					fLOGE ("mkdir [%s]: %s\n", path, strerror (errno));
				}
			}

			*p ++ = '/';
		}

		free (pbuf);
	}

	if (mkdir (path, DEFAULT_DIR_MODE) < 0)
	{
		if (errno == EEXIST)
		{
			if (! dir_exists (path))
			{
				fLOGW ("mkdir [%s]: path was existed but not a directory! force removing the invalid file and try again...\n", path);

				unlink_file (path);

				if (mkdir (path, DEFAULT_DIR_MODE) < 0)
				{
					fLOGE ("mkdir retried [%s]: %s\n", path, strerror (errno));
				}
			}
			else
			{
				fLOGD_IF ("mkdir [%s]: %s\n", path, strerror (errno));
			}
		}
		else
		{
			fLOGE ("mkdir [%s]: %s\n", path, strerror (errno));
		}
	}
	else
	{
		fLOGD_IF ("mkdir [%s] ok.\n", path);
	}

	return (dir_exists (path) ? 0 : -1);
}

/*
 * return 0 on ok, -1 on error or failed.
 */
int dir_write_test (const char *path)
{
#if 1
	return (access (path, W_OK) == 0) ? 0 : -1;
#else
	const char *tmp = ".write_file_test.";
	char *pbuf;
	int len, fd;

	if ((len = strlen (path)) == 0)
		return -1;

	len += strlen (tmp) + 16 /* for random number */;

	if ((pbuf = (char *) malloc (len)) == NULL)
		return -1;

	strcpy (pbuf, path);

	if (pbuf [strlen (pbuf) - 1] != '/')
	{
		strcat (pbuf, "/");
	}

	strcat (pbuf, tmp);

	sprintf (& pbuf [strlen (pbuf)], "%d", rand ());

	len = -1;

	if ((fd = open (pbuf, O_CREAT | O_WRONLY, DEFAULT_FILE_MODE)) < 0)
	{
		fLOGE ("open [%s]: %s\n", pbuf, strerror (errno));
	}
	else
	{
		if (write (fd, tmp, strlen (tmp)) == (ssize_t) strlen (tmp))
		{
			len = 0;
		}
		else
		{
			fLOGE ("write [%s]: %s\n", pbuf, strerror (errno));
		}
		close (fd);
	}

	unlink (pbuf);
	free (pbuf);

	return len;
#endif
}

void dir_no_media (const char *path)
{
	if (path)
	{
		char buffer [PATH_MAX];
		int fd;

		snprintf (buffer, sizeof (buffer), "%s%s", path, ".nomedia");
		buffer [sizeof (buffer) - 1] = 0;

		if (access (buffer, F_OK) != 0)
		{
			if ((fd = open (buffer, O_CREAT | O_WRONLY, DEFAULT_FILE_MODE)) < 0)
				return;

			close (fd);
		}
	}
}

int dir_select_log_path (char *buffer, int len)
{
	const char *path = NULL;

	int auto_select = 0;
	int log_to_phone = 0;
	int log_to_external = 0;
	int log_to_usb = 0;

	if ((! buffer) || (len <= 0))
		return -1;

	if (buffer [0] == '/')
	{
		/*
		 * there is already a path specified, just use it
		 */
		int i = strlen (buffer);

		if (i < (len - 1))
		{
			if (buffer [i - 1] != '/')
				strcat (buffer, "/");
		}

		if ((dir_create_recursive (buffer) < 0) || (dir_write_test (buffer) < 0))
		{
			fLOGE ("cannot write to [%s]!\n", buffer);
			return -1;
		}
		return 0;
	}

	if ((strncmp (buffer, STORAGE_KEY_AUTO, strlen (STORAGE_KEY_AUTO)) == 0) || (buffer [0] == 0))
	{
		auto_select = 1;

		if (strncmp (buffer, STORAGE_KEY_AUTO ":", strlen (STORAGE_KEY_AUTO) + 1) == 0)
		{
			/* having specified storages */
			if (strstr (buffer, ":" STORAGE_KEY_PHONE))
				log_to_phone = 1;

			if (strstr (buffer, ":" STORAGE_KEY_EXTERNAL))
				log_to_external = 1;

			if (strstr (buffer, ":" STORAGE_KEY_USB))
				log_to_usb = 1;
		}
		else
		{
			/* no specified storages, default all in select list */
			log_to_phone = 1;
			log_to_external = 1;
			log_to_usb = 1;
		}
	}
	else if (strcmp (buffer, STORAGE_KEY_INTERNAL) == 0)
	{
		auto_select = 0;
		log_to_phone = 0;
		log_to_external = 0;
		log_to_usb = 0;
	}
	else if (strcmp (buffer, STORAGE_KEY_EXTERNAL) == 0)
	{
		auto_select = 0;
		log_to_phone = 0;
		log_to_external = 1;
		log_to_usb = 0;
	}
	else if (strcmp (buffer, STORAGE_KEY_PHONE) == 0)
	{
		auto_select = 0;
		log_to_phone = 1;
		log_to_external = 0;
		log_to_usb = 0;
	}
	else if (strcmp (buffer, STORAGE_KEY_USB) == 0)
	{
		auto_select = 0;
		log_to_phone = 0;
		log_to_external = 0;
		log_to_usb = 1;
	}
	else
	{
		fLOGE ("unknown keyword [%s]!\n", buffer);
		return -1;
	}

	if (log_to_usb)
	{
		/* try to log to usb storage */
		snprintf (buffer, len, "%s/" LOG_FOLDER_NAME, dir_get_usb_storage ());
		buffer [len - 1] = 0;

		if ((dir_storage_state (dir_get_usb_storage ()) == 0 /* not mounted */) ||
			(dir_create_recursive (buffer) < 0) ||
			(dir_write_test (buffer) < 0))
		{
			if (! auto_select)
			{
				fLOGE ("cannot write to [%s]!\n", buffer);
				return -1;
			}

			fLOGD_IF ("cannot write to [%s], try external storage!\n", buffer);

			/*
			 * try other storages later
			 */
			log_to_usb = 0;
		}
		else
		{
			/* succeeded, do not need to try external and phone storage in auto mode */
			log_to_external = 0;
			log_to_phone = 0;
		}
	}

	if (log_to_external)
	{
		/* try to log to external storage */
		snprintf (buffer, len, "%s/" LOG_FOLDER_NAME, dir_get_external_storage ());
		buffer [len - 1] = 0;

		if ((dir_storage_state (dir_get_external_storage ()) == 0 /* not mounted */) ||
			(dir_create_recursive (buffer) < 0) ||
			(dir_write_test (buffer) < 0))
		{
			if (! auto_select)
			{
				fLOGE ("cannot write to [%s]!\n", buffer);
				return -1;
			}

			fLOGD_IF ("cannot write to [%s], try phone storage!\n", buffer);

			/*
			 * try other storages later
			 */
			log_to_external = 0;
		}
		else
		{
			/* succeeded, do not need to try phone storage in auto mode */
			log_to_phone = 0;
		}
	}

	if (log_to_phone)
	{
		/* try to log to phone storage */
		snprintf (buffer, len, "%s/" LOG_FOLDER_NAME, dir_get_phone_storage ());
		buffer [len - 1] = 0;

		if ((dir_storage_state (dir_get_phone_storage ()) == 0 /* not mounted */) ||
			(dir_create_recursive (buffer) < 0) ||
			(dir_write_test (buffer) < 0))
		{
			if (! auto_select)
			{
				fLOGE ("cannot write to [%s]!\n", buffer);
				return -1;
			}

			fLOGD_IF ("cannot write to [%s], try internal storage!\n", buffer);

			/*
			 * try internal storage later
			 */
			log_to_phone = 0;
		}
	}

	if ((! log_to_phone) && (! log_to_external) && (! log_to_usb))
	{
		/* use internal storage */
		snprintf (buffer, len, "%s", LOG_DIR);
		buffer [len - 1] = 0;

		if ((dir_create_recursive (buffer) < 0) || (dir_write_test (buffer) < 0))
		{
			fLOGE ("cannot write to [%s]!\n", buffer);
			return -1;
		}
	}

	return 0;
}

int dir_clear (const char *path, GLIST *patterns)
{
	struct stat st;
	struct dirent *entry;
	DIR *dir;
	char *buffer;
	int len;

	if (stat (path, & st) < 0)
	{
		fLOGE ("stat [%s]: %s\n", path, strerror (errno));
		return -1;
	}

	if (! S_ISDIR (st.st_mode))
	{
		errno = 0;
		unlink (path);
		fLOGD_IF ("unlink [%s][%s]\n", path, strerror (errno));
		return 0;
	}

	len = strlen (path) + PATH_MAX;

	if ((buffer = malloc (len)) == NULL)
	{
		fLOGD_IF ("malloc %d: %s\n", len, strerror (errno));
		return -1;
	}

	if ((dir = opendir (path)) == NULL)
	{
		fLOGD_IF ("opendir [%s][%s]\n", path, strerror (errno));
		free (buffer);
		return -1;
	}

	while ((entry = readdir (dir)) != NULL)
	{
		if ((strcmp (entry->d_name, ".") == 0) || (strcmp (entry->d_name, "..") == 0))
			continue;

		if (patterns)
		{
			GLIST *p;
			char *s;

			for (p = patterns; p; p = GLIST_NEXT (p))
			{
				s = GLIST_DATA (p);

				if (s && (! strncmp (s, entry->d_name, strlen (s))))
					break;
			}

			if (! p)
			{
				//fLOGD_IF ("skip [%s]\n", entry->d_name);
				continue;
			}
		}

		snprintf (buffer, len, "%s/%s", path, entry->d_name);
		buffer [len - 1] = 0;

		if (entry->d_type == DT_DIR)
		{
			dir_clear (buffer, patterns);
			errno = 0;
			rmdir (buffer);
			fLOGD_IF ("rmdir [%s][%s]\n", buffer, strerror (errno));
		}
		else
		{
			errno = 0;
			unlink (buffer);
			fLOGD_IF ("unlink [%s][%s]\n", buffer, strerror (errno));
		}
	}

	free (buffer);
	closedir (dir);
	return 0;
}

const char *dir_get_larger_storage(void)
{
	const char *ext_path = dir_get_external_storage();
	const char *phone_path = dir_get_phone_storage();
	int ext_state = dir_storage_state(ext_path);
	int phone_state = dir_storage_state(phone_path);

	if (ext_state == 1)
	{
		if (phone_state == 1)
		{
			struct statfs st_phone;
			struct statfs st_ext;
			memset (& st_phone, 0, sizeof (st_phone));
			memset (& st_ext, 0, sizeof (st_ext));
			if (statfs (ext_path, & st_ext) < 0)
			{
				fLOGD_IF ("statfs %s: %s\n", ext_path, strerror (errno));
				return phone_path;
			}
			if (statfs (phone_path, & st_phone) < 0)
			{
				fLOGD_IF ("statfs [%s]: %s\n", phone_path, strerror (errno));
				return ext_path;
			}

			unsigned long phone_size = (unsigned long) ((unsigned long long) st_phone.f_bsize * (unsigned long long) st_phone.f_bfree / (1024*1024));
			unsigned long ext_size = (unsigned long) ((unsigned long long) st_ext.f_bsize * (unsigned long long) st_ext.f_bfree / (1024*1024));
			fLOGD_IF("%s: free size = %luMB\n", phone_path, phone_size);
			fLOGD_IF("%s: free size = %luMB\n", ext_path, ext_size);

			if (ext_size > phone_size)
			{
				fLOGD_IF("external storage free size is larger than phone storage.\n");
				return ext_path;
			}
			else
			{
				fLOGD_IF("phone storage free size is larger than external storage.\n");
				return phone_path;
			}
		}
		else
		{
			fLOGD_IF("phone storage is not mount, use external storage [%s]\n", ext_path);
			return ext_path;
		}
	}
	else
	{
		if (phone_state == 1)
		{
			fLOGD_IF("external storage is not mount, use phone storage [%s]\n", phone_path);
			return phone_path;
		}
		else
		{
			fLOGD_IF("Both phone and external storage are not mounted.\n");
			return NULL;
		}
	}
}
