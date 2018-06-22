#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <errno.h>

#include "headers/partition.h"

#define	LOG_TAG	"STT:partition"

#include <utils/Log.h>
#include "libcommon.h"

/* partition name groups */
const char *p_mfg [] = { "mfg", "MFG", "WDM", NULL };
const char *p_misc [] = { "misc", "MSC", NULL };

const char **p_name_groups [] = { p_mfg, p_misc, NULL };

static const char **partition_get_name_group (const char *name)
{
	int i;

	if (name)
	{
		for (i = 0; p_name_groups [i] && p_name_groups [i][0]; i ++)
		{
			//fLOGD_IF ("partition_get_name_group(): p_name_groups [i][0]=%s\n", p_name_groups [i][0]);

			if (! strcmp (p_name_groups [i][0], name))
			{
				return p_name_groups [i];
			}
		}
	}
	return NULL;
}

int partition_open (PARTITION *pn, const char *partition_name)
{
	const char *p_default [] = { NULL, NULL };

	const char **group;
	int i;

	if ((! pn) || (! partition_name))
	{
		fLOGE ("partition_open(): invalid argument!\n");
		return -1;
	}

	if ((group = partition_get_name_group (partition_name)) == NULL)
	{
		/* no group found, return default with only itself */
		p_default [0] = partition_name;
		group = p_default;

		fLOGD_IF ("partition_open(): no partition group found, use given name itself:\n");
	}
	else
	{
		fLOGD_IF ("partition_open(): use partition group:\n");
	}

	for (i = 0; group [i]; i ++)
	{
		fLOGD_IF ("  [%s]\n", group [i]);
	}

	pn->emmc_ftm = NULL;
	pn->emmc = NULL;
	pn->mtd = NULL;

	for (;;)
	{
		/*
		 * open FTM eMMC
		 */
		fLOGD_IF ("partition_open(): try FTM eMMC partition ...\n");

		if ((pn->emmc_ftm = malloc (sizeof (eMMCFTMPartition))) == NULL)
		{
			fLOGE ("partition_open(): malloc emmc_ftm failed!\n");
		}
		else
		{
			if (group)
			{
				for (i = 0; group [i] && group [i][0]; i ++)
				{
					if (emmc_ftm_open (pn->emmc_ftm, group [i]) >= 0)
						break;
				}

				if (group [i] != NULL)
					break;
			}
			else
			{
				if (emmc_ftm_open (pn->emmc_ftm, partition_name) >= 0)
					break;
			}

			free (pn->emmc_ftm);
			pn->emmc_ftm = NULL;
		}

		/*
		 * open eMMC
		 */
		fLOGD_IF ("partition_open(): try eMMC partition ...\n");

		if ((pn->emmc = malloc (sizeof (eMMCPartition))) == NULL)
		{
			fLOGE ("partition_open(): malloc emmc failed!\n");
		}
		else
		{
			if (group)
			{
				for (i = 0; group [i] && group [i][0]; i ++)
				{
					if (emmc_open (pn->emmc, group [i]) >= 0)
						break;
				}

				if (group [i] != NULL)
					break;
			}
			else
			{
				if (emmc_open (pn->emmc, partition_name) >= 0)
					break;
			}

			free (pn->emmc);
			pn->emmc = NULL;
		}

		/*
		 * open MTD
		 */
		fLOGD_IF ("partition_open(): try MTD partition ...\n");

		if ((pn->mtd = malloc (sizeof (MtdPartition))) == NULL)
		{
			fLOGE ("partition_open(): malloc mtd failed!\n");
		}
		else
		{
			if (group)
			{
				for (i = 0; group [i] && group [i][0]; i ++)
				{
					if (mtd_open (pn->mtd, group [i]) >= 0)
						break;
				}

				if (group [i] != NULL)
					break;
			}
			else
			{
				if (mtd_open (pn->mtd, partition_name) >= 0)
					break;
			}

			free (pn->mtd);
			pn->mtd = NULL;
		}

		/*
		 * failed
		 */
		fLOGE ("partition_open(): cannot find partition [%s]!\n", partition_name);
		return -1;
	}

	return 0;
}

int partition_read (PARTITION *pn, loff_t offset, long length, void *pdata)
{
	if ((! pn) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("partition_read(): invalid argument!\n");
		return -1;
	}
	if (pn->mtd)
	{
fLOGD_IF ("use mtd_read(), path = %s\n", pn->mtd->path);//KILLME
		return mtd_read (pn->mtd, offset, length, pdata);
	}
	if (pn->emmc)
	{
fLOGD_IF ("use emmc_read(), path = %s\n", pn->emmc->path);//KILLME
		return emmc_read (pn->emmc, offset, length, pdata);
	}
	if (pn->emmc_ftm)
	{
fLOGD_IF ("use emmc_ftm_read(), name = %s\n", pn->emmc_ftm->name);//KILLME
		return emmc_ftm_read (pn->emmc_ftm, offset, length, pdata);
	}
	return -1;
}

int partition_write (PARTITION *pn, loff_t offset, long length, void *pdata)
{
	if ((! pn) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("partition_write(): invalid argument!\n");
		return -1;
	}
	if (pn->mtd)
	{
fLOGD_IF ("use mtd_write(), path = %s\n", pn->mtd->path);//KILLME
		return mtd_write (pn->mtd, offset, length, pdata);
	}
	if (pn->emmc)
	{
fLOGD_IF ("use emmc_write(), path = %s\n", pn->emmc->path);//KILLME
		return emmc_write (pn->emmc, offset, length, pdata);
	}
	if (pn->emmc_ftm)
	{
fLOGD_IF ("use emmc_ftm_write(), name = %s\n", pn->emmc_ftm->name);//KILLME
		return emmc_ftm_write (pn->emmc_ftm, offset, length, pdata);
	}
	return -1;
}

int partition_close (PARTITION *pn)
{
	int ret = -1;

	if (! pn)
	{
		fLOGE ("partition_close(): invalid argument!\n");
		return ret;
	}
	if (pn->mtd)
	{
		ret = mtd_close (pn->mtd);
		free (pn->mtd);
		pn->mtd = NULL;
		return ret;
	}
	if (pn->emmc)
	{
		ret = emmc_close (pn->emmc);
		free (pn->emmc);
		pn->emmc = NULL;
		return ret;
	}
	if (pn->emmc_ftm)
	{
		ret = emmc_ftm_close (pn->emmc_ftm);
		free (pn->emmc_ftm);
		pn->emmc_ftm = NULL;
		return ret;
	}

	return ret;
}

int partition_pagesize (PARTITION *pn)
{
	if (! pn)
	{
		fLOGE ("partition_pagesize(): invalid argument!\n");
	}
	else
	{
		if (pn->mtd)
		{
			return mtd_pagesize (pn->mtd);
		}
		if (pn->emmc)
		{
			return emmc_pagesize (pn->emmc);
		}
		if (pn->emmc_ftm)
		{
			return emmc_ftm_pagesize (pn->emmc_ftm);
		}
	}
	return PAGESIZE_UNKNOWN;
}

long partition_misc_debugflags_read (PARTITION *pn, int *pdata)
{
	if (! pn)
		return -1;

	if (pn->mtd)
	{
		return mtd_misc_debugflags_read (pn->mtd, pdata);
	}
	if (pn->emmc)
	{
		return emmc_misc_debugflags_read (pn->emmc, pdata);
	}
	if (pn->emmc_ftm)
	{
		return emmc_ftm_misc_debugflags_read (pn->emmc_ftm, pdata);
	}

	fLOGE ("partition_misc_debugflags_read(): not supported!\n");
	return -1;
}

long partition_misc_debugflags_write (PARTITION *pn, int *pdata)
{
	if (! pn)
		return -1;

	if (pn->mtd)
	{
		return mtd_misc_debugflags_write (pn->mtd, pdata);
	}
	if (pn->emmc)
	{
		return emmc_misc_debugflags_write (pn->emmc, pdata);
	}
	if (pn->emmc_ftm)
	{
		return emmc_ftm_misc_debugflags_write (pn->emmc_ftm, pdata);
	}

	fLOGE ("partition_misc_debugflags_write(): not supported!\n");
	return -1;
}

int partition_misc_usim_read (PARTITION *pn, void *pdata, long length)
{
	if (! pn)
		return -1;

	if (pn->emmc)
	{
		return emmc_misc_usim_read (pn->emmc, pdata, length);
	}

	fLOGE ("partition_misc_usim_read(): not supported!\n");
	return -1;
}

int partition_misc_usim_write (PARTITION *pn, void *pdata, long length)
{
	if (! pn)
		return -1;

	if (pn->emmc)
	{
		return emmc_misc_usim_write (pn->emmc, pdata, length);
	}

	fLOGE ("partition_misc_usim_write(): not supported!\n");
	return -1;
}

int partition_is_autostart_bit_set (void)
{
	PARTITION pn;

	long data_length;
	int *data = NULL;
	int ret = 0;

	if (partition_open (& pn, "misc") < 0)
		goto end;

	if ((data_length = partition_misc_debugflags_read (& pn, NULL)) <= 0)
	{
		fLOGE ("partition_is_autostart_bit_set(): invalid data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
		goto end;
	}

	if (data_length != (long) (MISC_DEBUGFLAGS_COUNT * sizeof (int)))
	{
		/*
		 * just warning, keep going
		 */
		fLOGW ("partition_is_autostart_bit_set(): unexpected data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
	}

	data = (int *) malloc (data_length);

	if (! data)
	{
		fLOGE ("partition_is_autostart_bit_set(): malloc failed!\n");
		goto end;
	}

	memset (data, 0, data_length);

	if (partition_misc_debugflags_read (& pn, data) < 0)
		goto end;

	fLOGD_IF ("partition_is_autostart_bit_set(): debug flag 5 value 0x%08X\n", data [5]);

	ret = (data [5] & 0x1);

end:;
	if (data) free (data);
	partition_close (& pn);
	return ret;
}
