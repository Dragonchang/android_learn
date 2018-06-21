#ifndef	_SSD_PARTITION_H_
#define _SSD_PARTITION_H_

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <cutils/properties.h>

#include "mtd.h"
#include "emmc.h"
#include "emmc_ftm.h"

#ifndef	free_safe
#define free_safe(o)	if(o){free(o);o=NULL;}
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	PAGESIZE_UNKNOWN	(-1)
#define	PAGESIZE_2K		(2048)
#define	PAGESIZE_4K		(4096)

#define	OFFSET_UNKNOWN			(-1)

typedef struct {
	MtdPartition		*mtd;
	eMMCPartition		*emmc;
	eMMCFTMPartition	*emmc_ftm;
} PARTITION;

#define PARTITION_INIT	{NULL, NULL, NULL}

extern int partition_open	(PARTITION *pn, const char *partition_name);
extern int partition_read	(PARTITION *pn, loff_t offset, long length, void *pdata);
extern int partition_write	(PARTITION *pn, loff_t offset, long length, void *pdata);
extern int partition_close	(PARTITION *pn);
extern int partition_pagesize	(PARTITION *pn);

extern int partition_is_autostart_bit_set (void);

#define	MISC_DEBUGFLAGS_MAX_COUNT	(256)
#define	MISC_DEBUGFLAGS_COUNT		(16)

extern long partition_misc_debugflags_read (PARTITION *pn, int *pdata);
extern long partition_misc_debugflags_write (PARTITION *pn, int *pdata);

extern int partition_misc_usim_read (PARTITION *pn, void *pdata, long length);
extern int partition_misc_usim_write (PARTITION *pn, void *pdata, long length);

#ifndef UNUSED_VAR
#define UNUSED_VAR(x) UNUSED_ ## x __attribute__((__unused__))
#endif

#ifdef __cplusplus
}
#endif

#endif
