#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <errno.h>

#include "headers/partition.h"

#define	LOG_TAG	"STT:emmc"

#include <utils/Log.h>
#include "libcommon.h"

#ifndef	fLOGD
#define	fLOGD	ALOGD
#endif

#ifndef	fLOGW
#define	fLOGW	ALOGW
#endif

#ifndef	fLOGE
#define	fLOGE	ALOGE
#endif

static int find_gpt_partition_path (eMMCPartition *ec, const char *partition_name)
{
	const char *platforms [] = {
		"msm_sdcc",	/* /dev/block/platform/msm_sdcc.1/by-name/ */
		"sdhci-tegra",	/* /dev/block/platform/sdhci-tegra.3/by-name/ */
		NULL
	};

	char path [256];
	char buffer [256];
	int platform_index;
	int driver_index;

	memset (path, 0, sizeof (path));
	memset (buffer, 0, sizeof (buffer));

	for (platform_index = 0; platforms [platform_index] != NULL; platform_index ++)
	{
		for (driver_index = 0; driver_index < 10; driver_index ++)
		{
			snprintf (path, sizeof (path), "/dev/block/platform/%s.%d/by-name/", platforms [platform_index], driver_index);
			path [sizeof (path) - 1] = 0;

			if (access (path, F_OK) == 0)
			{
				fLOGD ("emmc GPT partition exist, path: [%s]\n", path);

				snprintf (path, sizeof (path), "/dev/block/platform/%s.%d/by-name/%s", platforms [platform_index], driver_index, partition_name);
				path [sizeof (path) - 1] = 0;

				if (readlink (path, buffer, sizeof (buffer)) > 0)
				{
					fLOGD ("found emmc GPT partition [%s]=[%s]\n", partition_name, buffer);

					snprintf (ec->path, sizeof (ec->path), "%s", buffer);
					return 0;
				}
			}
		}
	}

	fLOGD ("cannot find emmc GPT partition\n");
	return -1;
}

static int find_partition_path (eMMCPartition *ec, const char *partition_name)
{
	FILE *fp;
	char buffer [256], *name, *ptr;

	if ((fp = fopen ("/proc/emmc", "rb")) == NULL)
	{
		fLOGD ("/proc/emmc: %s\n", strerror (errno));
		ec->path [0] = 0;
		return -1;
	}

	/*
	 * dev:        size     erasesize name
	 * mmcblk0p23: 00040000 00000200 "misc"
	 * mmcblk0p20: 0087f400 00000200 "recovery"
	 * mmcblk0p21: 00400000 00000200 "boot"
	 * mmcblk0p26: 12fffe00 00000200 "system"
	 * mmcblk0p25: 0cbbfa00 00000200 "cache"
	 * mmcblk0p27: 58fffe00 00000200 "userdata"
	 */
	while (fgets (buffer, sizeof (buffer) - 1, fp) != NULL)
	{
		buffer [sizeof (buffer) - 1] = 0;

		/*
		 * make buffer be the device name
		 */
		for (ptr = buffer; *ptr && (*ptr != ':'); ptr ++);
		if (! *ptr) continue;
		*ptr ++ = 0;

		/*
		 * get partition name
		 */
		for (; *ptr && (*ptr != '\"'); ptr ++);
		if (! *ptr) continue;
		name = ++ ptr;
		for (; *ptr && (*ptr != '\"'); ptr ++);
		if (*ptr) *ptr = 0;

		if (! strcmp (partition_name, name))
		{
			fLOGD ("found emmc partition [%s]=[%s]\n", name, buffer);//KILLME
			snprintf (ec->path, sizeof (ec->path), "/dev/block/%s", buffer);
			ec->path [sizeof (ec->path) - 1] = 0;
			fclose (fp);
			return 0;
		}
	}

	fclose (fp);
	return -1;
}

int emmc_open (eMMCPartition *ec, const char *partition_name)
{
	if ((! ec) || (! partition_name))
	{
		fLOGE ("emmc_open(): invalid argument!\n");
		return -1;
	}

	if (find_partition_path (ec, partition_name) < 0)
	{
		fLOGD ("emmc_open(): cannot find eMMC partition [%s]!\n", partition_name);

		if (find_gpt_partition_path (ec, partition_name) < 0)
		{
			fLOGD ("emmc_open(): cannot find GPT eMMC partition [%s]!\n", partition_name);
			return -1;
		}
	}

	ec->fd = open (ec->path, O_RDWR);

	if (ec->fd < 0)
	{
		fLOGE ("emmc_open(): %s: %s\n", ec->path, strerror (errno));
		return -1;
	}

	return 0;
}

int emmc_read (eMMCPartition *ec, loff_t offset, long length, void *pdata)
{
	if ((! ec) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("emmc_read(): invalid argument!\n");
		return -1;
	}
	if (lseek64 (ec->fd, offset, SEEK_SET) < 0)
	{
		fLOGE ("emmc_read(): seek %s: %s\n", ec->path, strerror (errno));
		return -1;
	}
	if (read (ec->fd, pdata, length) != length)
	{
		fLOGE ("emmc_read(): read %s: %s\n", ec->path, strerror (errno));
		return -1;
	}
	return 0;
}

int emmc_write (eMMCPartition *ec, loff_t offset, long length, void *pdata)
{
	if ((! ec) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("emmc_write(): invalid argument!\n");
		return -1;
	}
	if (lseek64 (ec->fd, offset, SEEK_SET) < 0)
	{
		fLOGE ("emmc_write(): seek %s: %s\n", ec->path, strerror (errno));
		return -1;
	}
	if (write (ec->fd, pdata, length) != length)
	{
		fLOGE ("emmc_write(): write %s: %s\n", ec->path, strerror (errno));
		return -1;
	}
	return 0;
}

int emmc_close (eMMCPartition *ec)
{
	if (! ec)
	{
		fLOGE ("emmc_close(): invalid argument!\n");
		return -1;
	}
	if (ec->fd >= 0)
	{
		close (ec->fd);
	}
	return 0;
}

int emmc_pagesize (eMMCPartition *UNUSED_VAR (ec))
{
	char buf [PROPERTY_VALUE_MAX];
	int pagesize;

	bzero (buf, sizeof (buf));

	property_get ("ro.pagesize", buf, "");

	if (buf [0])
	{
		pagesize = atoi (buf);
		fLOGD ("use ro.pagesize pagesize = %d (0x%X)\n", pagesize, pagesize);
		return pagesize;
	}

	property_get ("ro.boot.pagesize", buf, "");

	if (buf [0])
	{
		pagesize = atoi (buf);
		fLOGD ("use ro.boot.pagesize pagesize = %d (0x%X)\n", pagesize, pagesize);
		return pagesize;
	}

	property_get ("ro.boot.misc_pagesize", buf, "");

	if (buf [0])
	{
		pagesize = atoi (buf);
		fLOGD ("use ro.boot.misc_pagesize pagesize = %d (0x%X)\n", pagesize, pagesize);
		return pagesize;
	}

	pagesize = PAGESIZE_2K; // default eMMC pagesize

	fLOGD ("use emmc pagesize = %d (0x%X)\n", pagesize, pagesize);

	return pagesize;
}

static int emmc_misc_debugflags_offset (eMMCPartition *ec)
{
	int pagesize = emmc_pagesize (ec);

	switch (pagesize)
	{
	case PAGESIZE_2K:
		return EMMC_MISC_DEBUGFLAGS_OFFSET_2K;
	case PAGESIZE_4K:
		return EMMC_MISC_DEBUGFLAGS_OFFSET_4K;
	default:
		fLOGE ("emmc_misc_debugflags_offset(): unknown pagesize %d!\n", pagesize);
	}

	return OFFSET_UNKNOWN;
}

static int emmc_misc_usim_offset (eMMCPartition *ec)
{
	int pagesize = emmc_pagesize (ec);

	switch (pagesize)
	{
	case PAGESIZE_2K:
		return EMMC_MISC_USIM_OFFSET_2K;
	case PAGESIZE_4K:
		return EMMC_MISC_USIM_OFFSET_4K;
	default:
		fLOGE ("emmc_misc_usim_offset(): unknown pagesize %d!\n", pagesize);
	}

	return OFFSET_UNKNOWN;
}

long emmc_misc_debugflags_read (eMMCPartition *ec, int *pdata)
{
	int offset;

	if (! ec)
		return -1;

	if (! pdata)
		return EMMC_MISC_DEBUGFLAGS_DATA_LEN;

	if ((offset = emmc_misc_debugflags_offset (ec)) == OFFSET_UNKNOWN)
		return -1;

	if (emmc_read (ec, (loff_t) offset, EMMC_MISC_DEBUGFLAGS_DATA_LEN, (void *) pdata) < 0)
	{
		fLOGE ("emmc_misc_debugflags_read(): failed!\n");
		return -1;
	}
	return 0;
}

long emmc_misc_debugflags_write (eMMCPartition *ec, int *pdata)
{
	int offset;

	if (! ec)
		return -1;

	if (! pdata)
		return EMMC_MISC_DEBUGFLAGS_DATA_LEN;

	if ((offset = emmc_misc_debugflags_offset (ec)) == OFFSET_UNKNOWN)
		return -1;

	if (emmc_write (ec, (loff_t) offset, EMMC_MISC_DEBUGFLAGS_DATA_LEN, (void *) pdata) < 0)
	{
		fLOGE ("emmc_misc_debugflags_write(): failed!\n");
		return -1;
	}
	return 0;
}

int emmc_misc_usim_read (eMMCPartition *ec, void *pdata, long length)
{
	int offset;

	if ((! ec) || (! pdata))
		return -1;

	if ((offset = emmc_misc_usim_offset (ec)) == OFFSET_UNKNOWN)
		return -1;

	if (emmc_read (ec, (loff_t) offset, length, (void *) pdata) < 0)
	{
		fLOGE ("emmc_misc_usim_read(): failed!\n");
		return -1;
	}
	return 0;
}

int emmc_misc_usim_write (eMMCPartition *ec, void *pdata, long length)
{
	int offset;

	if ((! ec) || (! pdata))
		return -1;

	if ((offset = emmc_misc_usim_offset (ec)) == OFFSET_UNKNOWN)
		return -1;

	if (emmc_write (ec, (loff_t) offset, length, (void *) pdata) < 0)
	{
		fLOGE ("emmc_misc_usim_write(): failed!\n");
		return -1;
	}
	return 0;
}
