#ifndef	_SSD_EMMC_H_
#define _SSD_EMMC_H_

#include <sys/types.h>
#include <unistd.h>

#ifndef	free_safe
#define free_safe(o)	if(o){free(o);o=NULL;}
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	EMMC_MISC_DEBUGFLAGS_DATA_LEN	(1024) /* (0xFF * 4) + 4 */
#define	EMMC_MISC_DEBUGFLAGS_OFFSET_2K	(0x1000)
#define	EMMC_MISC_DEBUGFLAGS_OFFSET_4K	(0x2000)

//misc partition page 6
#define	EMMC_MISC_USIM_OFFSET_2K	(0x3000)
#define	EMMC_MISC_USIM_OFFSET_4K	(0x6000)

typedef struct {
	char path [256];
	int fd;
} eMMCPartition;

extern int emmc_open	(eMMCPartition *ec, const char *partition_name);
extern int emmc_read	(eMMCPartition *ec, loff_t offset, long length, void *pdata);
extern int emmc_write	(eMMCPartition *ec, loff_t offset, long length, void *pdata);
extern int emmc_close	(eMMCPartition *ec);
extern int emmc_pagesize(eMMCPartition *ec);

extern long emmc_misc_debugflags_read (eMMCPartition *ec, int *pdata);
extern long emmc_misc_debugflags_write (eMMCPartition *ec, int *pdata);

extern int emmc_misc_usim_read (eMMCPartition *ec, void *pdata, long length);
extern int emmc_misc_usim_write (eMMCPartition *ec, void *pdata, long length);

#ifdef __cplusplus
}
#endif

#endif
