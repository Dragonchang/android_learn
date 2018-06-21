#include <sys/types.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <errno.h>

#include "headers/partition.h"

#define	LOG_TAG	"STT:mtd"

#include <utils/Log.h>
#include "libcommon.h"

unsigned int erase_size = 131072;

int alloc_mtd_repeat_and_do (ssize_t (*io_func) (int, void *, size_t), int fd, void *addr, size_t len)
{
	const size_t min_alloc_size = 0x1000; /* minimum read/write size is 4k */

	size_t try_alloc_size, init_len;
	char *curr_data;
	int loops, i, init_loops;

	loff_t pos = lseek64 (fd, 0, SEEK_CUR);

	init_loops = 1;
	init_len = len;

#if 1
	/*
	 * a dirty fix for kernel MTD drvier. (Marvel ITS#3932, kernel may trigger low memory killer and block IO functions if retry too many times)
	 */
	if (len >= 0x40000)
	{
		init_loops <<= 3;
		init_len >>= 3;
	}
#endif

	/*
	 * Reduce data size by 1/2 and try again until reaching minimum alloc size 4k.
	 */
	for (loops = init_loops, try_alloc_size = init_len; try_alloc_size >= min_alloc_size;)
	{
		/*
		 * divide by 2
		 */
		try_alloc_size >>= 1;

		/*
		 * multiply by 2
		 */
		loops <<= 1;

		/*
		 * reset read pointer
		 */
		curr_data = (char *) addr;

		fLOGD_IF ("retry in %d loops, io size = 0x%x bytes", loops, (unsigned int) try_alloc_size);

		for (i = 0; i < loops; i ++)
		{
			if (try_alloc_size != (size_t) io_func (fd, curr_data, try_alloc_size))
			{
				fLOGE ("retry failed: %s", strerror (errno));

				if (errno == ENOMEM)
					break;

				/*
				 * other errors
				 */
				return -1;
			}

			curr_data += try_alloc_size;

			fLOGD_IF ("  io #%d, total 0x%x bytes processed", i, (unsigned int) ((size_t) curr_data - (size_t) addr));
		}

		if (((size_t) curr_data - (size_t) addr) == len)
			return 0;

		if (lseek64 (fd, pos, SEEK_SET) != pos)
		{
			fLOGE ("lseek64 to 0x%08llx: %s", (unsigned long long) pos, strerror (errno));
			break;
		}
	}

	/*
	 * finally we still fail to read/write
	 */
	fLOGE ("all io failed!");
	return -1;
}

static ssize_t io (ssize_t (*func) (int, void *, size_t), int fd, void *addr, size_t len)
{
	loff_t pos;

	if (! addr)
		return -1;

	pos = lseek64 (fd, 0, SEEK_CUR);

	if (func (fd, addr, len) == (ssize_t) len)
	{
		fLOGD_IF ("%s 0x%x bytes at 0x%08llx", (func == read) ? "read" : "write", (unsigned int) len, (unsigned long long) pos);
		return len;
	}

	fLOGW ("%s 0x%x bytes at 0x%08llx: %s", (func == read) ? "read" : "write", (unsigned int) len, (unsigned long long) pos, strerror (errno));

	if (errno != ENOMEM)
	{
		return -1;
	}

	if (lseek64 (fd, pos, SEEK_SET) != pos)
	{
		fLOGE ("lseek64 to 0x%08llx: %s", (unsigned long long) pos, strerror (errno));
		return -1;
	}

	if (alloc_mtd_repeat_and_do (func, fd, addr, len) == 0)
		return len;

	return -1;
}

static int read_block (const MtdPartition *partition, int fd, char *data)
{
	struct mtd_ecc_stats before, after;
	ssize_t size;
	int mgbb;
	loff_t pos;

	if (ioctl (fd, ECCGETSTATS, & before))
	{
		fLOGE ("ECCGETSTATS error (%s)", strerror (errno));
		return -1;
	}

	pos = lseek64 (fd, 0, SEEK_CUR);

	size = partition->erase_size;

	while ((pos + size) <= (loff_t) partition->size)
	{
		if ((mgbb = ioctl (fd, MEMGETBADBLOCK, & pos)) != 0)
		{
			fLOGE ("MEMGETBADBLOCK returned %d at 0x%08llx (%s)", mgbb, (unsigned long long) pos, strerror (errno));
		}
		else if (lseek64 (fd, pos, SEEK_SET) != pos)
		{
			fLOGE ("lseek64 to 0x%08llx: %s", (unsigned long long) pos, strerror (errno));
		}
		else if (io (read, fd, data, size) != size)
		{
			fLOGE ("read %ld bytes at 0x%08llx", (long) size, (unsigned long long) pos);
		}
		else if (ioctl (fd, ECCGETSTATS, & after))
		{
			fLOGE ("ECCGETSTATS error (%s)", strerror (errno));
			return -1;
		}
		else if (after.failed != before.failed)
		{
			fLOGE ("ECC errors (%d soft, %d hard) at 0x%08llx", after.corrected - before.corrected, after.failed - before.failed, (unsigned long long) pos);
		}
		else
		{
			int i;

			for (i = 0; i < size; i ++)
			{
				if (data [i] != 0)
				{
					return 0; // success
				}
			}

			fLOGE ("read all-zero block at 0x%08llx, skipping ...", (unsigned long long) pos);
		}

		pos += partition->erase_size;

		fLOGD_IF ("try to read next block at 0x%08llx ...", (unsigned long long) pos);
	}

	/*
	 * run out of space on the device
	 */
	errno = ENOSPC;
	return -1;
}

static int write_block (const MtdPartition *partition, int fd, char *data)
{
	struct erase_info_user erase_info;
	ssize_t size;
	char *verify;
	int mgbb;
	int retry;

	loff_t pos = lseek64 (fd, 0, SEEK_CUR);

	if (pos == (loff_t) -1)
		return 1;

	size = partition->erase_size;

	while ((pos + size) <= (loff_t) partition->size)
	{
		if ((mgbb = ioctl (fd, MEMGETBADBLOCK, & pos)) != 0)
		{
			fLOGE ("MEMGETBADBLOCK returned %d at 0x%08llx (%s)", mgbb, (unsigned long long) pos, strerror (errno));
			pos += partition->erase_size;
			fLOGD_IF ("try to write next block at 0x%08llx ...", (unsigned long long) pos);
			continue; /* don't try to erase known factory-bad blocks */
		}

		erase_info.start = pos;
		erase_info.length = size;

		for (retry = 0; retry < 2; retry ++)
		{
			if (ioctl (fd, MEMERASE, & erase_info) < 0)
			{
				fLOGE ("erase failure at 0x%08llx (%s)", (unsigned long long) pos, strerror (errno));
				continue;
			}

			if ((lseek64 (fd, pos, SEEK_SET) != pos) || (io ((ssize_t (*) (int, void *, size_t)) write, fd, data, size) != size))
			{
				fLOGE ("write error at 0x%08llx (%s)", (unsigned long long) pos, strerror (errno));
				continue;
			}

			verify = malloc (size);

			if (verify)
			{
				if ((lseek64 (fd, pos, SEEK_SET) != pos) || (io (read, fd, verify, size) != size))
				{
					fLOGE ("re-read error at 0x%08llx (%s)", (unsigned long long) pos, strerror (errno));
					free (verify);
					continue;
				}

				if (memcmp (data, verify, size) != 0)
				{
					fLOGE ("verification error at 0x%08llx (%s)", (unsigned long long) pos, strerror (errno));
					free (verify);
					continue;
				}

				free (verify);
			}

			if (retry > 0)
			{
				fLOGE ("wrote block after %d retries", retry);
			}

			return 0; // success
		}

		/*
		 * erase it once more as we give up on this block
		 */
		fLOGE ("skip writing block at 0x%08llx", (unsigned long long) pos);
		ioctl (fd, MEMERASE, & erase_info);
		pos += partition->erase_size;
	}

	/*
	 * run out of space on the device
	 */
	errno = ENOSPC;
	return -1;
}

/* return device fd */
int mtd_open_partition_device (const MtdPartition *pp)
{
	char devname [16];
	int ret;

	if ((! pp) || (pp->device_index == -1))
		return -1;

	snprintf (devname, sizeof (devname), "/dev/mtd/mtd%d", pp->device_index);

	ret = open (devname, O_RDWR);

	if (ret < 0) fLOGE ("cannot open device [%s]!\n", devname);

	return ret;
}

int mtd_get_partition_info (int fd, mtd_info_t *pinfo)
{
	if ((fd < 0) || (! pinfo))
		return -1;

	return ioctl (fd, MEMGETINFO, pinfo);
}

int mtd_get_partition_region_info (int fd, region_info_t *pinfo)
{
	if ((fd < 0) || (! pinfo))
		return -1;

	return ioctl (fd, MEMGETREGIONINFO, pinfo);
}

int mtd_set_partition_erase_info (int fd, uint32_t start, uint32_t length)
{
	erase_info_t erase_info;
	erase_info.length = erase_size;	//assume erasing last found partition

	if (fd < 0)
		return -1;

	if (length % erase_size != 0) {
		LOGE("length %d is not a multiple of erase size %d", length, erase_size);
		return -1;
	}

	int result = 0;
	for(erase_info.start = start; erase_info.start < length; erase_info.start += erase_size) {
		loff_t off = (off_t)erase_info.start;
		int ret = ioctl(fd, MEMGETBADBLOCK, &off);
		if (ret != 0) {
			LOGD("offset %d is bad block, skip...", erase_info.start);
			continue;
		}
		LOGD("trying to erase offset %d", erase_info.start);
		if (ioctl (fd, MEMERASE, & erase_info) < 0) {
			LOGD("erasing offset %d failed, errno = %d", erase_info.start, errno);
			result = -1;
		}
	}

	return result;
}

/*
 * Simplified MTD functions.
 */

static int find_partition_path (MtdPartition *mn, const char *partition_name)
{
	char buf [512], *ptr;
	int fd, nbytes, matches;
	int mtdnum, mtdsize, mtderasesize;
	char mtdname [64];

	mn->path [0] = 0;

	if ((fd = open (MTD_PROC_FILENAME, O_RDONLY)) < 0)
	{
		fLOGD_IF ("%s: %s\n", MTD_PROC_FILENAME, strerror (errno));
		goto failed;
	}

	nbytes = read (fd, buf, sizeof (buf) - 1);

	close (fd);

	if (nbytes < 0)
	{
		fLOGD_IF ("%s: %s\n", MTD_PROC_FILENAME, strerror (errno));
		goto failed;
	}

	buf [nbytes] = 0;

	/*
	 * Parse the contents of the file, which looks like:
	 *
	 *     # cat /proc/mtd
	 *     dev:    size   erasesize  name
	 *     mtd0: 00080000 00020000 "bootloader"
	 *     mtd1: 00400000 00020000 "mfg_and_gsm"
	 *     mtd2: 00400000 00020000 "0000000c"
	 *     mtd3: 00200000 00020000 "0000000d"
	 *     mtd4: 04000000 00020000 "system"
	 *     mtd5: 03280000 00020000 "userdata"
	 */
	for (ptr = buf; nbytes > 0;)
	{
		mtdname [0] = 0;
		mtdnum = -1;

		matches = sscanf (ptr, "mtd%d: %x %x \"%63[^\"]", & mtdnum, & mtdsize, & mtderasesize, mtdname);

		/*
		 * This will fail on the first line, which just contains
		 * column headers.
		 */
		if ((matches == 4) && (strcmp (mtdname, partition_name) == 0))
		{
			mn->device_index = mtdnum;
			mn->size = mtdsize;
			mn->erase_size = mtderasesize;
			snprintf (mn->path, sizeof (mn->path), "/dev/mtd/mtd%d", mtdnum);
			mn->path [sizeof (mn->path) - 1] = 0;
			return 0;
		}

		/*
		 * Eat the line.
		 */
		while ((nbytes > 0) && (*ptr != '\n'))
		{
			ptr ++;
			nbytes --;
		}
		if (nbytes > 0)
		{
			ptr ++;
			nbytes --;
		}
	}

failed:;
	return -1;
}

int mtd_open (MtdPartition *mn, const char *partition_name)
{
	if ((! mn) || (! partition_name))
	{
		fLOGE ("mtd_open(): invalid argument!\n");
		return -1;
	}

	if (find_partition_path (mn, partition_name) < 0)
	{
		fLOGD_IF ("mtd_open(): cannot find MTD partition [%s]!\n", partition_name);
		return -1;
	}

	mn->fd = open (mn->path, O_RDWR);

	if (mn->fd < 0)
	{
		fLOGE ("mtd_open(): %s: %s\n", mn->path, strerror (errno));
		return -1;
	}

	return 0;
}

int mtd_read (MtdPartition *mn, loff_t offset, long length, void *pdata)
{
	long bytesread;
	char *oneblock = NULL, *data = pdata;
	int ret = -1;

	if ((! mn) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("mtd_read(): invalid argument!\n");
		goto end;
	}

	if (lseek64 (mn->fd, 0, SEEK_SET) < 0)
	{
		fLOGE ("mtd_read(): lseek64(): %s\n", strerror (errno));
		goto end;
	}

	oneblock = malloc (mn->erase_size);

	if (! oneblock)
	{
		fLOGE ("mtd_read(): malloc failed!\n");
		goto end;
	}

	bytesread = 0;

	/*
	 * Seek with bad block check.
	 */
	while (offset >= (loff_t) mn->erase_size)
	{
		if (read_block (mn, mn->fd, oneblock))
			goto end;

		offset -= (loff_t) mn->erase_size;
	}

	/*
	 * Read fragment bytes.
	 */
	if (offset != 0)
	{
		if (read_block (mn, mn->fd, oneblock))
			goto end;

		bytesread = (loff_t) mn->erase_size - offset;

		if (bytesread > length)
			bytesread = length;

		memcpy (data, oneblock + offset, bytesread);
	}

	/*
	 * Read whole blocks.
	 */
	while ((length - bytesread) >= (long) mn->erase_size)
	{
		if (read_block (mn, mn->fd, data + bytesread))
			goto end;

		bytesread += mn->erase_size;
	}

	/*
	 * Read fragment bytes.
	 */
	if (bytesread < length)
	{
		if (read_block (mn, mn->fd, oneblock))
			goto end;

		memcpy (data + bytesread, oneblock, (length - bytesread));
	}

	ret = 0;
end:;
	if (oneblock)
	{
		free (oneblock);
	}
	return ret;
}

int mtd_write (MtdPartition *mn, loff_t offset, long length, void *pdata)
{
	long byteswrote;
	char *oneblock = NULL, *data = pdata;
	int ret = -1;

	if ((! mn) || (offset < 0) || (length <= 0) || (! pdata))
	{
		fLOGE ("mtd_write(): invalid argument!\n");
		return -1;
	}

	if (lseek64 (mn->fd, 0, SEEK_SET) < 0)
	{
		fLOGE ("mtd_write(): lseek64(): %s\n", strerror (errno));
		goto end;
	}

	oneblock = malloc (mn->erase_size);

	if (! oneblock)
	{
		fLOGE ("mtd_write(): malloc failed!\n");
		goto end;
	}

	byteswrote = 0;

	/*
	 * Seek with bad block check.
	 */
	while (offset >= (loff_t) mn->erase_size)
	{
		if (read_block (mn, mn->fd, oneblock))
			goto end;

		offset -= (loff_t) mn->erase_size;
	}

	/*
	 * Write fragment bytes.
	 */
	if (offset != 0)
	{
		loff_t pos = lseek64 (mn->fd, 0, SEEK_CUR);

		if (read_block (mn, mn->fd, oneblock))
			goto end;

		if (lseek64 (mn->fd, pos, SEEK_SET) < 0)
		{
			fLOGE ("mtd_write(): lseek64(): %s\n", strerror (errno));
			goto end;
		}

		byteswrote = (loff_t) mn->erase_size - offset;

		if (byteswrote > length)
			byteswrote = length;

		memcpy (oneblock + offset, data, byteswrote);

		if (write_block (mn, mn->fd, oneblock))
			goto end;
	}

	/*
	 * Write whole blocks.
	 */
	while ((length - byteswrote) >= (long) mn->erase_size)
	{
		if (write_block (mn, mn->fd, data + byteswrote))
			goto end;

		byteswrote += mn->erase_size;
	}

	/*
	 * Write fragment bytes.
	 */
	if (byteswrote < length)
	{
		loff_t pos = lseek64 (mn->fd, 0, SEEK_CUR);

		if (read_block (mn, mn->fd, oneblock))
			goto end;

		if (lseek64 (mn->fd, pos, SEEK_SET) < 0)
		{
			fLOGE ("mtd_write(): lseek64(): %s\n", strerror (errno));
			goto end;
		}

		memcpy (oneblock, data + byteswrote, (length - byteswrote));

		if (write_block (mn, mn->fd, oneblock))
			goto end;
	}

	ret = 0;
end:;
	if (oneblock)
	{
		free (oneblock);
	}
	return ret;
}

int mtd_close (MtdPartition *mn)
{
	if (! mn)
	{
		fLOGE ("mtd_close(): invalid argument!\n");
		return -1;
	}
	if (mn->fd >= 0)
	{
		close (mn->fd);
	}
	return 0;
}

int mtd_pagesize (MtdPartition *mn)
{
	int page_count = 64;
	int page_size = -1;

	if (mn != NULL)
	{
		page_size = mn->erase_size / page_count;

		fLOGD_IF ("use mtd erasesize = %d (0x%X), pagesize = %d (0x%X)\n", mn->erase_size, mn->erase_size, page_size, page_size);

		return page_size;
	}

	return PAGESIZE_UNKNOWN;
}

static int mtd_misc_debugflags_offset (MtdPartition *mn)
{
	int pagesize = mtd_pagesize (mn);

	switch (pagesize)
	{
	case PAGESIZE_2K:
		return MTD_MISC_DEBUGFLAGS_OFFSET_2K;
	case PAGESIZE_4K:
		return MTD_MISC_DEBUGFLAGS_OFFSET_4K;
	default:
		fLOGE ("mtd_misc_debugflags_offset(): unknown pagesize %d!\n", pagesize);
	}

	return OFFSET_UNKNOWN;
}

long mtd_misc_debugflags_read (MtdPartition *mn, int *pdata)
{
	int offset;

	if (! mn)
		return -1;

	if (! pdata)
		return MTD_MISC_DEBUGFLAGS_DATA_LEN;

	if ((offset = mtd_misc_debugflags_offset (mn)) == OFFSET_UNKNOWN)
		return -1;

	if (mtd_read (mn, (loff_t) offset, MTD_MISC_DEBUGFLAGS_DATA_LEN, (void *) pdata) < 0)
	{
		fLOGE ("mtd_misc_debugflags_read(): failed!\n");
		return -1;
	}
	return 0;
}

long mtd_misc_debugflags_write (MtdPartition *mn, int *pdata)
{
	int offset;

	if (! mn)
		return -1;

	if (! pdata)
		return MTD_MISC_DEBUGFLAGS_DATA_LEN;

	if ((offset = mtd_misc_debugflags_offset (mn)) == OFFSET_UNKNOWN)
		return -1;

	if (mtd_write (mn, (loff_t) offset, MTD_MISC_DEBUGFLAGS_DATA_LEN, (void *) pdata) < 0)
	{
		fLOGE ("mtd_misc_debugflags_write(): failed!\n");
		return -1;
	}
	return 0;
}
