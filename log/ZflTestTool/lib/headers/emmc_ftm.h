#ifndef	_SSD_EMMC_FTM_H_
#define	_SSD_EMMC_FTM_H_

#include <sys/types.h>
#include <unistd.h>

#ifndef	free_safe
#define free_safe(o)	if(o){free(o);o=NULL;}
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	FTM_TYPE_UNKNOWN	0
#define	FTM_TYPE_MISC		1
#define	FTM_TYPE_MFG		2

typedef struct {
	char name [32];
	int type;
} eMMCFTMPartition;

extern int emmc_ftm_open	(eMMCFTMPartition *ec, const char *partition_name);
extern int emmc_ftm_read	(eMMCFTMPartition *ec, loff_t offset, long length, void *pdata);
extern int emmc_ftm_write	(eMMCFTMPartition *ec, loff_t offset, long length, void *pdata);
extern int emmc_ftm_close	(eMMCFTMPartition *ec);
extern int emmc_ftm_pagesize	(eMMCFTMPartition *ec);

extern long emmc_ftm_misc_debugflags_read (eMMCFTMPartition *ec, int *pdata);
extern long emmc_ftm_misc_debugflags_write (eMMCFTMPartition *ec, int *pdata);

#ifdef __cplusplus
}
#endif

#endif
