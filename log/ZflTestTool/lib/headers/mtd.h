#ifndef	_SSD_MTD_H_
#define _SSD_MTD_H_

#include <sys/types.h>
#include <unistd.h>
#include <mtd/mtd-user.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef	free_safe
#define free_safe(o)	if(o){free(o);o=NULL;}
#endif

#define	MTD_MISC_DEBUGFLAGS_DATA_LEN	(1024) /* (0xFF * 4) + 4 */
#define	MTD_MISC_DEBUGFLAGS_OFFSET_2K	(0x1000)
#define	MTD_MISC_DEBUGFLAGS_OFFSET_4K	(0x2000)

#define MTD_PROC_FILENAME		"/proc/mtd"
#define MAX_NUM_OF_MTD_PARTITION	(32)

typedef struct {
	char path	[256];
	int		fd;
	int		device_index;//to be removed
	unsigned int	size;
	unsigned int	erase_size;
	char		*name;//to be removed
} MtdPartition;

extern MtdPartition *mtd_find_partition_by_name (const char *);

/* return partition device fd */
extern int mtd_open_partition_device (const MtdPartition *);

/* get partition info */
extern int mtd_get_partition_info		(int fd, mtd_info_t *);
extern int mtd_get_partition_region_info	(int fd, region_info_t *);
extern int mtd_set_partition_erase_info		(int fd, uint32_t start, uint32_t length);

extern int alloc_mtd_repeat_and_do (ssize_t (*io_func) (int, void *, size_t), int fd, void *addr, size_t plen);

extern int mtd_open	(MtdPartition *mn, const char *partition_name);
extern int mtd_read	(MtdPartition *mn, loff_t offset, long length, void *pdata);
extern int mtd_write	(MtdPartition *mn, loff_t offset, long length, void *pdata);
extern int mtd_close	(MtdPartition *mn);
extern int mtd_pagesize	(MtdPartition *mn);

extern long mtd_misc_debugflags_read (MtdPartition *mn, int *pdata);
extern long mtd_misc_debugflags_write (MtdPartition *mn, int *pdata);

#ifdef __cplusplus
}
#endif

#endif
