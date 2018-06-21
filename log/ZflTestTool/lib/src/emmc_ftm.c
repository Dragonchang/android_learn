#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <errno.h>

#include "headers/partition.h"

#define	LOG_TAG	"STT:emmc_ftm"

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

#if HTC_FLAG_FTM_LIB_EMMC

/*
 * vendor/htc/ftm_lib/ftm_lib_emmc/ftm_lib_emmc_raw_rw.h
 *
 * MISC_CONFIG_DATA_LEN
 *
 * vendor/htc/ftm_lib/ftm_lib_emmc/ftm_lib_emmc_api.h
 *
 * int MISC_read_ConfigData (int *pData);
 * int MISC_write_ConfigData (int *pData);
 * int MISC_read_raw (unsigned char *pData, int offset, int size);
 * int MISC_write_raw (unsigned char *pData, int offset, int size);
 * int MFG_read_raw (unsigned char *pData, int offset, int size);
 * int MFG_write_raw (unsigned char *pData, int offset, int size);
 *
 * LOCAL_SHARED_LIBRARIES += libftm_lib_emmc_utility
 */
#include "ftm_lib_emmc_raw_rw.h"
#include "ftm_lib_emmc_api.h"

int emmc_ftm_open (eMMCFTMPartition *ec, const char *partition_name)
{
	if ((! ec) || (! partition_name))
	{
		fLOGE ("emmc_ftm_open(): invalid argument!\n");
		return -1;
	}

	if (strcmp (partition_name, "misc") == 0)
	{
		ec->type = FTM_TYPE_MISC;
	}
	else if (strcmp (partition_name, "mfg") == 0)
	{
		ec->type = FTM_TYPE_MFG;
	}
	else
	{
		fLOGD ("emmc_ftm_open(): do not support partition [%s]!\n", partition_name);
		return -1;
	}

	strncpy (ec->name, partition_name, sizeof (ec->name) - 1);
	ec->name [sizeof (ec->name) - 1] = 0;

	fLOGD ("emmc_ftm_open(): open partition [%s].\n", ec->name);
	return 0;
}

int emmc_ftm_read (eMMCFTMPartition *ec, loff_t offset, long length, void *pdata)
{
	int ret = -1;

	if ((! ec) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("emmc_ftm_read(): invalid argument!\n");
		return -1;
	}

	switch (ec->type)
	{
	case FTM_TYPE_MISC:
		ret = MISC_read_raw ((unsigned char *) pdata, (int) offset, (int) length);
		break;
	case FTM_TYPE_MFG:
		ret = MFG_read_raw ((unsigned char *) pdata, (int) offset, (int) length);
		break;
	default:
		errno = -EINVAL;
	}

	if (ret < 0)
	{
		fLOGE ("emmc_ftm_read(): read %s: %s\n", ec->name, strerror (errno));
		return -1;
	}
	return 0;
}

int emmc_ftm_write (eMMCFTMPartition *ec, loff_t offset, long length, void *pdata)
{
	int ret = -1;

	if ((! ec) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("emmc_ftm_write(): invalid argument!\n");
		return -1;
	}

	switch (ec->type)
	{
	case FTM_TYPE_MISC:
		ret = MISC_write_raw ((unsigned char *) pdata, (int) offset, (int) length);
		break;
	case FTM_TYPE_MFG:
		ret = MFG_write_raw ((unsigned char *) pdata, (int) offset, (int) length);
		break;
	default:
		errno = -EINVAL;
	}

	if (ret < 0)
	{
		fLOGE ("emmc_ftm_write(): write %s: %s\n", ec->name, strerror (errno));
		return -1;
	}
	return 0;
}

int emmc_ftm_close (eMMCFTMPartition *ec)
{
	if (! ec)
	{
		fLOGE ("emmc_ftm_close(): invalid argument!\n");
		return -1;
	}

	ec->type = FTM_TYPE_UNKNOWN;
	return 0;
}

int emmc_ftm_pagesize (eMMCFTMPartition *UNUSED_VAR (ec))
{
	int pagesize = PAGESIZE_2K; // default eMMC pagesize
	fLOGD ("use emmc_ftm, no pagesize is required, return default value %d\n", pagesize);
	return pagesize;
}

long emmc_ftm_misc_debugflags_read (eMMCFTMPartition *ec, int *pdata)
{
	if ((! ec) || (ec->type != FTM_TYPE_MISC))
		return -1;

	if (! pdata)
		return MISC_CONFIG_DATA_LEN;

	if (MISC_read_ConfigData (pdata) != 0)
	{
		fLOGE ("emmc_ftm_misc_debugflags_read(): MISC_read_ConfigData() failed!\n");
		return -1;
	}

	return 0;
}

long emmc_ftm_misc_debugflags_write (eMMCFTMPartition *ec, int *pdata)
{
	if ((! ec) || (ec->type != FTM_TYPE_MISC))
		return -1;

	if (! pdata)
		return MISC_CONFIG_DATA_LEN;

	if (MISC_write_ConfigData (pdata) != 0)
	{
		fLOGE ("emmc_ftm_misc_debugflags_write(): MISC_write_ConfigData() failed!\n");
		return -1;
	}

	return 0;
}

#else /* ! HTC_FLAG_FTM_LIB_EMMC */

int emmc_ftm_open (eMMCFTMPartition *UNUSED_VAR (ec), const char *UNUSED_VAR (partition_name)) { return -1; }
int emmc_ftm_read (eMMCFTMPartition *UNUSED_VAR (ec), loff_t UNUSED_VAR (offset), long UNUSED_VAR (length), void *UNUSED_VAR (pdata)) { return -1; }
int emmc_ftm_write (eMMCFTMPartition *UNUSED_VAR (ec), loff_t UNUSED_VAR (offset), long UNUSED_VAR (length), void *UNUSED_VAR (pdata)) { return -1; }
int emmc_ftm_close (eMMCFTMPartition *UNUSED_VAR (ec)) { return -1; }
int emmc_ftm_pagesize (eMMCFTMPartition *UNUSED_VAR (ec)) { return -1; }
long emmc_ftm_misc_debugflags_read (eMMCFTMPartition *UNUSED_VAR (ec), int *UNUSED_VAR (pdata)) { return -1; }
long emmc_ftm_misc_debugflags_write (eMMCFTMPartition *UNUSED_VAR (ec), int *UNUSED_VAR (pdata)) { return -1; }

#endif /* HTC_FLAG_FTM_LIB_EMMC */
