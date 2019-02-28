#define ATRACE_TAG ATRACE_TAG_VIEW
#define LOG_TAG "Memory"

#include <memutils/MemoryMeasureHelper.h>
#include <utils/Log.h>
#include <utils/SystemClock.h>

#include <cutils/properties.h>

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <utils/Trace.h>
#include <dirent.h>

namespace android {

PageMapItem::PageMapItem(const char * name, long vss=0, long rss=0, long pss=0, long swap=0, long pswap = 0, long sharedClean=0, long sharedDirty=0,
		long privateClean=0, long privateDirty=0, long referenced=0, long anonymous=0,
		long anonHugePages=0, long kernelPagesize=0, long mMUPageSize=0,
		long locked=0) {
	init(name,vss,rss,pss,swap,pswap,sharedClean,sharedDirty,privateClean,privateDirty,
        referenced,anonymous,anonHugePages,kernelPagesize,mMUPageSize,locked);
}

void PageMapItem::setName(const char * name) {
    memset( mName,0,sizeof(NAME_BUF_SIZE+1));
    strncpy(mName,name,sizeof(NAME_BUF_SIZE));
}
void PageMapItem::combine(PageMapItem * item) {
    mVss += item->getVss();
    mRss += item->getRss();
    mPss += item->getPss();
    mSharedClean += item->getSharedClean();
    mSharedDirty += item->getSharedDirty();
    mPrivate_Clean += item->getPrivate_Clean();
    mPrivate_Dirty += item->getPrivate_Dirty();
    mReferenced += item->getReferenced();
    mAnonymous += item->getAnonymous();
    mAnonHugePages += item->getAnonHugePages();
    mSwap += item->getSwap();
    mPSwap += item->getPSwap();
    mKernelPageSize += item->getKernelPageSize();
    mMMUPageSize += item->getMMUPageSize();
    mLocked += item->getLocked();
}
void PageMapItem::combineMainValues(PageMapItem * item) {
    // for performance only count vss, rss and pss value
    mVss += item->getVss();
    mRss += item->getRss();
    mPss += item->getPss();
    mSwap += item->getSwap();
    mPSwap += item->getPSwap();
}
void PageMapItem::init(const char * name, long vss=0, long rss=0, long pss=0, long swap=0, long pswap = 0, long sharedClean=0, long sharedDirty=0,
		long privateClean=0, long privateDirty=0, long referenced=0, long anonymous=0,
		long anonHugePages=0, long kernelPagesize=0, long mMUPageSize=0,
		long locked=0) {
    setName(name);
    mVss = vss;
    mRss = rss;
    mPss = pss;
    mSharedClean = sharedClean;
    mSharedDirty = sharedDirty;
    mPrivate_Clean = privateClean;
    mPrivate_Dirty = privateDirty;
    mReferenced = referenced;
    mAnonymous = anonymous;
    mAnonHugePages = anonHugePages;
    mSwap = swap;
    mPSwap = pswap;
    mKernelPageSize = kernelPagesize;
    mMMUPageSize = mMUPageSize;
    mLocked = locked;
}
void PageMapItem::clear() {
    mName[0] = '\0';
    mVss = mRss = mPss = mSharedClean = mSharedDirty = mPrivate_Clean = mPrivate_Dirty = 0;
    mReferenced = mAnonymous = mAnonHugePages = mSwap = mPSwap = mKernelPageSize = mMMUPageSize = 0;
    mLocked = 0;
}
bool PageMapItem::toArray(long ** out) {
    const int num_of_fields = 14;//the number of fields dumped by /proc/xxx/smaps
    int size = sizeof(long) * num_of_fields;
    long* newArrary = (long *)malloc(size); // malloc memory, caller need free this memory.
    if(!newArrary) return false;
    memset(newArrary,0,size);
    newArrary[0] = mVss;
    newArrary[1] = mRss;
    newArrary[2] = mPss;
    newArrary[3] = mSharedClean;
    newArrary[4] = mSharedDirty;
    newArrary[5] = mPrivate_Clean;
    newArrary[6] = mPrivate_Dirty;
    newArrary[7] = mReferenced;
    newArrary[8] = mAnonymous;
    newArrary[9] = mAnonHugePages;
    //newArrary[10] = mSwap;
    newArrary[10] = mPSwap;
    newArrary[11] = mKernelPageSize;
    newArrary[12] = mMMUPageSize;
    newArrary[13] = mLocked;
    *out = newArrary;
    return true;
}

#define TOTAL_MEMORY "TOTAL"
#define QCT_GRAPHIC_DEV "/dev/kgsl-3d0"
#define QCT_GRAPHIC_ION_DEV_1 "anon_inode:ion_share_fd"
#define QCT_GRAPHIC_ION_DEV_2 "anon_inode:dmabuf"
#define NV_GRAPHIC_DEV_NVMAP "/dev/nvmap"
#define NV_GRAPHIC_DEV_KNVMAP "/dev/knvmap"
#define STE_GRAPHIC_DEV "/dev/mali"

static unsigned int TOTAL_MEMORY_HASH = MemoryMeasureHelper::BKDRHash(TOTAL_MEMORY);
static unsigned int QCT_GRAPHIC_DEV_HASH = MemoryMeasureHelper::BKDRHash(QCT_GRAPHIC_DEV);
static unsigned int QCT_GRAPHIC_ION_DEV_1_HASH = MemoryMeasureHelper::BKDRHash(QCT_GRAPHIC_ION_DEV_1);
static unsigned int QCT_GRAPHIC_ION_DEV_2_HASH = MemoryMeasureHelper::BKDRHash(QCT_GRAPHIC_ION_DEV_2);
static unsigned int NV_GRAPHIC_DEV_NVMAP_HASH = MemoryMeasureHelper::BKDRHash(NV_GRAPHIC_DEV_NVMAP);
static unsigned int NV_GRAPHIC_DEV_KNVMAP_HASH = MemoryMeasureHelper::BKDRHash(NV_GRAPHIC_DEV_KNVMAP);
static unsigned int STE_GRAPHIC_DEV_HASH = MemoryMeasureHelper::BKDRHash(STE_GRAPHIC_DEV);

static int mPlatform = MemoryMeasureHelper::detectPlatform();

MemoryMeasureHelper::MemoryMeasureHelper(void) {
}
// BKDR Hash Function
unsigned int MemoryMeasureHelper::BKDRHash(const char *str)
{
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned int hash = 0;

	while (*str)
	{
		hash = hash + seed + (*str++);
	}

	return (hash & 0x7FFFFFFF);
}

// Should sync with outut of show_smap function in kernel/fs/proc/task_mmu.c
enum CheckTypeEnum { PAGEMAP_NAME, VSS, RSS,PSS,SHARED_CLEAN,SHARED_DIRTY,PRIVATE_CLEAN,PRIVATE_DIRTY,
	REFERENCED,ANONYMOUS,ANON_HUGE_PAGES,SWAP,SWAPPSS,PSWAP,KERNEL_PAGESIZE,MMU_PAGESIZE,LOCKED };
DefaultKeyedVector<unsigned int, PageMapItem*> MemoryMeasureHelper::getProcessPageMapItems(int pid,bool allValues=false) {
	// default allValues = false, only count main values(vss, rss and pss).
	// allValues = true, will count all page map.
	char filename[64];
	 ATRACE_NAME("GetProcessSmap");
	snprintf(filename, sizeof(filename), "/proc/%d/smaps", pid);
	FILE * file = fopen(filename, "r");
	if (!file) {
		ALOG(LOG_DEBUG, "MemoryLogUtil","No smaps file");
		return NULL;
	}

	const int nameBufferLength = 256;
	char line[nameBufferLength];
	char name[nameBufferLength];
	CheckTypeEnum checkFlag = PAGEMAP_NAME;
	bool finishPageMap = false;
	PageMapItem* tempItem = new PageMapItem();

	long vss, rss, pss, sum_vss, sum_rss, sum_pss;
	long sharedClean, sharedDirty, sum_sharedClean, sum_sharedDirty;
	long privateClean, privateDirty, sum_privateClean, sum_privateDirty;
	long referenced, sum_referenced;
	long anonymous, anonHugePages, sum_anonymous, sum_anonHugePages;
	long swap, sum_swap;
	long pswap, sum_pswap;
	long kernelPagesize, sum_kernelPagesize;
	long mMUPageSize, sum_mMUPageSize;
	long locked, sum_locked;

	vss = rss = pss = sum_vss = sum_rss = sum_pss = 0;
	sharedClean = sharedDirty = sum_sharedClean = sum_sharedDirty = 0;
	privateClean = privateDirty = sum_privateClean = sum_privateDirty = 0;
	referenced = sum_referenced = 0;
	anonymous = anonHugePages = sum_anonymous = sum_anonHugePages = 0;
	swap = sum_swap = 0;
	pswap = sum_pswap = 0;
	kernelPagesize = sum_kernelPagesize = 0;
	mMUPageSize = sum_mMUPageSize = 0;
	locked = sum_locked = 0;

	long value = 0;
	int count = 0;

	DefaultKeyedVector<unsigned int, PageMapItem *> map;
	while (fgets(line, sizeof(line), file)) {
		if (checkFlag == PAGEMAP_NAME) {
			count = sscanf(line, "%*lx-%*lx %*s %*lx %*s %*d %s", name);
			//ALOGW("MemoryMeasureHelper::getProcessPageMapItems sscanf count=%d",count);
			//ALOGW("%s",line);

			// found name, count = 1,
			// format error, count = 0
			// format right but can't found name, count = -1

        	// example: count = 1
        	// 40029000-4002a000 r--s 00169000 b3:20 1914       /system/framework/services.jar

        	// example: count = 0
        	// Memory	Size:                  4 kB

        	// example: count = -1
        	// 400f6000-400f8000 rw-p 00000000 00:00 0

			if (count == -1) // can't found name
				strncpy(name, "[anonymous]", nameBufferLength);

			if (count == 1 || count == -1) {
				tempItem->clear(); // reuse tempItem object
				vss = rss = pss = 0;
				sharedClean = sharedDirty = 0;
				privateClean = privateDirty = 0;
				referenced = 0;
				anonymous = anonHugePages = 0;
				swap = 0;
				pswap = 0;
				kernelPagesize = 0;
				mMUPageSize = 0;
				locked = 0;
				value = 0;
				checkFlag = VSS;
				continue;
			}
		}
		if (checkFlag == VSS && sscanf(line, "Size: %ld kB", &value) == 1) {
			vss = value;
			sum_vss += value;
			checkFlag = RSS;
		} else if (checkFlag == RSS
				&& sscanf(line, "Rss: %ld kB", &value) == 1) {
			rss = value;
			sum_rss += value;
			checkFlag = PSS;
		} else if (checkFlag == PSS
				&& sscanf(line, "Pss: %ld kB", &value) == 1) {
			pss = value;
			sum_pss += value;
			(allValues) ? checkFlag = SHARED_CLEAN : checkFlag = SWAP;
		} else if (checkFlag == SWAP
				&& sscanf(line, "Swap: %ld kB", &value) == 1) {
			swap = value;
			sum_swap += value;
			if (mPlatform == PLATFORM_QCT){
				checkFlag = SWAPPSS;
			}else if (mPlatform == PLATFORM_MTK){
				checkFlag = PSWAP;
			} else {
				if (allValues)
					checkFlag = KERNEL_PAGESIZE;
				else
					checkFlag = LOCKED;
			}
		} else if (checkFlag == SWAPPSS
				&& sscanf(line, "SwapPss: %ld kB", &value) == 1){
			pswap = value;
			sum_pswap += value;
			(allValues) ? checkFlag = KERNEL_PAGESIZE : checkFlag = LOCKED;
		} else if (checkFlag == PSWAP
				&& sscanf(line, "PSwap: %ld kB", &value) == 1){
	        pswap = value;
		    sum_pswap += value;
		    (allValues) ? checkFlag = KERNEL_PAGESIZE : checkFlag = LOCKED;
		} else {
			if (allValues) {
				if (checkFlag == SHARED_CLEAN && sscanf(line, "Shared_Clean: %ld kB", &value) == 1) {
					sharedClean = value;
					sum_sharedClean += value;
					checkFlag = SHARED_DIRTY;
				} else if (checkFlag == SHARED_DIRTY && sscanf(line, "Shared_Dirty: %ld kB", &value) == 1) {
					sharedDirty = value;
					sum_sharedDirty += value;
					checkFlag = PRIVATE_CLEAN;
				} else if (checkFlag == PRIVATE_CLEAN && sscanf(line, "Private_Clean: %ld kB", &value) == 1) {
					privateClean = value;
					sum_privateClean += value;
					checkFlag = PRIVATE_DIRTY;
				} else if (checkFlag == PRIVATE_DIRTY && sscanf(line, "Private_Dirty: %ld kB", &value) == 1) {
					privateDirty = value;
					sum_privateDirty += value;
					checkFlag = REFERENCED;
				} else if (checkFlag == REFERENCED && sscanf(line, "Referenced: %ld kB", &value) == 1) {
					referenced = value;
					sum_referenced += value;
					checkFlag = ANONYMOUS;
				} else if (checkFlag == ANONYMOUS && sscanf(line, "Anonymous: %ld kB", &value) == 1) {
					anonymous = value;
					sum_anonymous += value;
					checkFlag = ANON_HUGE_PAGES;
				} else if (checkFlag == ANON_HUGE_PAGES && sscanf(line, "AnonHugePages: %ld kB", &value) == 1) {
					anonHugePages = value;
					sum_anonHugePages += value;
					checkFlag = SWAP;
				} else if (checkFlag == KERNEL_PAGESIZE && sscanf(line, "KernelPageSize: %ld kB", &value) == 1) {
					kernelPagesize = value;
					sum_kernelPagesize += value;
					checkFlag = MMU_PAGESIZE;
				} else if (checkFlag == MMU_PAGESIZE
						&& sscanf(line, "MMUPageSize: %ld kB", &value) == 1) {
					mMUPageSize = value;
					sum_mMUPageSize += value;
					checkFlag = LOCKED;
				} else if (checkFlag == LOCKED
						&& sscanf(line, "Locked: %ld kB", &value) == 1) {
					locked = value;
					sum_locked += value;
					checkFlag = PAGEMAP_NAME;
					finishPageMap = true;
				}
			} else {
				if (checkFlag == LOCKED && strstr(line, "L")) {
					if (sscanf(line, "Locked: %ld kB", &value) == 1) {
						checkFlag = PAGEMAP_NAME;
						finishPageMap = true;
					}
				}
			}

		}
		if (finishPageMap) {
			//ALOGW("MemoryMeasureHelper::getProcessPageMapItems name=%s,vss=%ld,rss=%ld,pss=%ld",name,vss,rss,pss);
			if (allValues) {
				tempItem->init(name, vss, rss, pss, swap, pswap, sharedClean, sharedDirty,
						privateClean, privateDirty, referenced, anonymous,
						anonHugePages, kernelPagesize, mMUPageSize,
						locked);
			} else {
				tempItem->init(name, vss, rss, pss, swap, pswap);
			}

			unsigned int hash = BKDRHash(name);
			PageMapItem * pageMapItem = map.valueFor(hash);
			if (pageMapItem == NULL) {
				map.add(hash, tempItem);
				tempItem = new PageMapItem();
			} else {
				if (allValues) {
					pageMapItem->combine(tempItem);
				} else {
					pageMapItem->combineMainValues(tempItem);
				}
			}
			finishPageMap = false;
		}

	}
	fclose(file);
	file = NULL;
	tempItem->clear();
	if (allValues) {
		tempItem->init(TOTAL_MEMORY, sum_vss, sum_rss, sum_pss, sum_swap, sum_pswap, sum_sharedClean,
				sum_sharedDirty, sum_privateClean, sum_privateDirty,
				sum_referenced, sum_anonymous, sum_anonHugePages,
				sum_kernelPagesize, sum_mMUPageSize, sum_locked); // setup to total PageMapItem
	} else {
		tempItem->init(TOTAL_MEMORY, sum_vss, sum_rss, sum_pss, sum_swap, sum_pswap); // setup to total PageMapItem
	}
	map.add(TOTAL_MEMORY_HASH, tempItem); // add total PageMapItem to hash map;
	return map;
}

long MemoryMeasureHelper::getProcessTotalMemory(int pid) {
	long result = 0;
	DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
	//ALOGW("MemoryMeasureHelper::getProcessTotalMemory map count=%lu", map.size());
	result = getProcessPssOnlyFromMap(map);
	result += getGraphicsMemoryFromMap(map,pid);
	mapClear(map);
	return result;
}
bool MemoryMeasureHelper::getDetailRegionMemory(int pid, const char * name, long** out) {
    bool result = false;
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,true); // count all page
    PageMapItem * pageMapItem = map.valueFor(BKDRHash(name));
    if(pageMapItem != NULL) {
    	result = pageMapItem->toArray(out);
	}
    mapClear(map);
    return result;
}

long MemoryMeasureHelper::getRegionPssMemory(int pid, const char * name) {
    long result = 0;
	DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(BKDRHash(name));
    if(pageMapItem != NULL) {
    	result = pageMapItem->getPss();
	}
    mapClear(map);
    return result;
}

bool MemoryMeasureHelper::getQCTProcessMemoryForLowMemDetail(int pid,long ** out) {
	// will fill-in total_pss,process_pss_only, kgsl_vss and iOn_pss to array
    const int num_of_fields = 5;//the count of array
    int size = sizeof(long) * num_of_fields;
    long* newArray = (long *)malloc(size);
    if(!newArray) return false;

    ATRACE_NAME("GetQCTProc");

    memset(newArray,0,size);
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //total_pss
    if(pageMapItem != NULL) {
    	newArray[0] = pageMapItem->getPss();
	}
    newArray[1] = getProcessPssOnlyFromMap(map); // process_pss_only

    pageMapItem = map.valueFor(QCT_GRAPHIC_DEV_HASH); //kgsl_vss
    if(pageMapItem != NULL) {
    	newArray[2] = pageMapItem->getVss();
	}
    pageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_1_HASH); //iOn_vss
    if(pageMapItem != NULL) {
        newArray[3] = pageMapItem->getVss();
	}
    pageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_2_HASH); //iOn_vss
    if(pageMapItem != NULL) {
        newArray[3] += pageMapItem->getVss();
	}
    pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //SwapPss
    if(pageMapItem != NULL) {
        newArray[4] = pageMapItem->getPSwap();
	}
    *out = newArray;
    mapClear(map);
    return true;
}
bool MemoryMeasureHelper::getNVProcessMemoryForLowMemDetail(int pid, long ** out) {
	// will fill-in total_pss,process_pss_only to array
    const int num_of_fields = 3;//the count of array
    int size = sizeof(long) * num_of_fields;
    long* newArray = (long *)malloc(size);
    if(!newArray) return false;

    memset(newArray,0,size);
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //total_pss
    if(pageMapItem != NULL) {
    	newArray[0] = pageMapItem->getPss();
       newArray[1] = getProcessPssOnlyFromMap(map); // process_pss_only
       newArray[2] = pageMapItem->getSwap();
	}
    *out = newArray;
    mapClear(map);
    return true;
}

bool MemoryMeasureHelper::getSTEProcessMemoryForLowMemDetail(int pid, long ** out) {
    const int num_of_fields = 4;//the count of array
    int size = sizeof(long) * num_of_fields;
    long* newArray = (long *)malloc(size);
    if(!newArray) return false;

    memset(newArray,0,size);
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //total_pss
    if(pageMapItem != NULL) {
        newArray[0] = pageMapItem->getPss();
        newArray[1] = getProcessPssOnlyFromMap(map); // process_pss_only
        newArray[3] = pageMapItem->getSwap();

        pageMapItem = map.valueFor(STE_GRAPHIC_DEV_HASH);
        if(pageMapItem != NULL) {
            newArray[2] = pageMapItem->getVss();
   	    }
	}
    *out = newArray;
    mapClear(map);
    return true;
}

bool MemoryMeasureHelper::getBRCMProcessMemoryForLowMemDetail(int pid, long ** out) {
    const int num_of_fields = 3;//the count of array
    int size = sizeof(long) * num_of_fields;
    long* newArray = (long *)malloc(size);
    if(!newArray) return false;

    memset(newArray,0,size);
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //total_pss
    if(pageMapItem != NULL) {
       newArray[0] = pageMapItem->getPss();
       newArray[1] = getProcessPssOnlyFromMap(map); // process_pss_only
       newArray[2] = pageMapItem->getSwap();
	}
    *out = newArray;
    mapClear(map);
    return true;
}

bool MemoryMeasureHelper::getMTKProcessMemoryForLowMemDetail(int pid,long ** out) {
	// will fill-in total_pss,process_pss_only, kgsl_vss and iOn_pss to array
    const int num_of_fields = 5;//the count of array
    int size = sizeof(long) * num_of_fields;
    long* newArray = (long *)malloc(size);
    if(!newArray) return false;

    memset(newArray,0,size);
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //total_pss
    if(pageMapItem != NULL) {
        newArray[0] = pageMapItem->getPss();
	}
    newArray[1] = getProcessPssOnlyFromMap(map); // process_pss_only

    pageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_2_HASH); //iOn_vss
    if(pageMapItem != NULL) {
        newArray[2] += pageMapItem->getVss();
	}
    pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //PSwap
    if(pageMapItem != NULL) {
        newArray[3] = pageMapItem->getPSwap();
	}
    newArray[4] = getGraphicsMemoryFromMap(map,pid);
    *out = newArray;
    mapClear(map);
    return true;
}

bool MemoryMeasureHelper::getProcessMemoryForLowMemDetail(int pid, long ** out) {
    const int num_of_fields = 2;//the count of array
    int size = sizeof(long) * num_of_fields;
    long* newArray = (long *)malloc(size);
    if(!newArray) return false;

    memset(newArray,0,size);
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    PageMapItem * pageMapItem = map.valueFor(TOTAL_MEMORY_HASH); //total_pss
    if(pageMapItem != NULL) {
       newArray[0] = pageMapItem->getPss();
       newArray[1] = pageMapItem->getSwap();
    }
    *out = newArray;
    mapClear(map);
    return true;
}

long MemoryMeasureHelper::getProcessGraphicsMemory(int pid) {
    long result = 0;
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    result = getGraphicsMemoryFromMap(map,pid);
    mapClear(map);
    return result;
}
long MemoryMeasureHelper::getProcessPssOnly(int pid) {
    long  result = 0;
    DefaultKeyedVector<unsigned int, PageMapItem*> map = getProcessPageMapItems(pid,false);
    result = getProcessPssOnlyFromMap(map);
    mapClear(map);
    return result;
}

enum CheckFlagEnum { TARGET_PID, SURFACEFLINGER, OTHERS };
long MemoryMeasureHelper::getNVProcessGraphicsMemory(int pid) {
	FILE *file = fopen("/sys/kernel/debug/nvmap/iovmm/allocations", "r");
	if (file == NULL) {
		return 0;
	}
	//reference nvmap_dev.c
	//output format is "%-18s %-18s %8u %10u %8x %8x\n"
	char line[256] = { 0 };
	long procGraphicsMem = 0;
	char type[19] = { 0 }; //length = 18 + 1 NULL
	char procName[19] = { 0 }; //length = 18 + 1 NULL
	int getPid = 0;
	long size = 0;
	long flag = 0;
	long handle = 0;
	DefaultKeyedVector<long, long> surfaceflingerMemMap;
	DefaultKeyedVector<long, long> procMemMap;
	bool surfaceFound = false;
	bool targetPidFound = false;
	CheckFlagEnum curFlag = OTHERS;
	while (fgets(line, sizeof(line), file)) {
		if (strlen(line) == 0) continue;

		if (sscanf(line, "%d", &getPid) == 1) { // Make sure first field is integer
			if (curFlag != OTHERS) {
				if (sscanf(line, "%d %ld %lx %lx", &getPid, &size, &flag,&handle) == 4) {
					if (curFlag == SURFACEFLINGER) {
						surfaceflingerMemMap.add(handle, size);
					} else if (curFlag == TARGET_PID) {
						procMemMap.add(handle, size);
					}
				}
			}
		} else {
			if (surfaceFound == true && targetPidFound == true)
				break; // get all need info, so exit read file.

			if (sscanf(line, "%s %s %d %ld", type, procName, &getPid, &size) == 4) {
				curFlag = OTHERS;
				if (strcmp(procName, "surfaceflinger") == 0) {
					surfaceFound = true;
					curFlag = SURFACEFLINGER;
				} else {
					if (getPid == pid) {
						targetPidFound = true;
						curFlag = TARGET_PID;
						procGraphicsMem = size; // set process total graphics memory include surface.
					}
				}
			}
		}

	}
	fclose(file);
	file = NULL;

	long procGraphicsMemInSurfaceflinger = 0;

	size_t i, count;
	count = surfaceflingerMemMap.size();

	ssize_t foundKeyindex = -1;
	for (i = 0; i < count; i++) {
		handle = surfaceflingerMemMap.keyAt(i);
		foundKeyindex = procMemMap.indexOfKey(handle);
		if (foundKeyindex >= 0) {
			procGraphicsMemInSurfaceflinger += procMemMap.valueAt(foundKeyindex);
		}
	}
	surfaceflingerMemMap.clear();
	procMemMap.clear();
	return (procGraphicsMem - procGraphicsMemInSurfaceflinger) << 10; // convert to KBytes
}

long MemoryMeasureHelper::getProcessPssOnlyFromMap(DefaultKeyedVector<unsigned int, PageMapItem*> map) {
	if (map.isEmpty() == true) return 0;
	long result = 0;

	PageMapItem * totalPageMapItem = map.valueFor(TOTAL_MEMORY_HASH);
	if (totalPageMapItem != NULL) {
		result = totalPageMapItem->getPss();
	}

	if (mPlatform == PLATFORM_QCT) {
		// QTC: pss - kgsl_pss - iOn_pss
		PageMapItem * qctPageMapItem = map.valueFor(QCT_GRAPHIC_DEV_HASH);
		if (qctPageMapItem != NULL) {
			result -= qctPageMapItem->getPss(); // - kgsl_pss
			PageMapItem * iOnPageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_1_HASH);
			if (iOnPageMapItem != NULL) {
				result -= iOnPageMapItem->getPss(); // - iOn_pss
			}
			iOnPageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_2_HASH);
			if (iOnPageMapItem != NULL) {
				result -= iOnPageMapItem->getPss(); // - iOn_pss
			}
		}
	} else if (mPlatform == PLATFORM_NV) {
		// NV: pss - nvmap_pss - knvmap_pss
		PageMapItem * nvmapPageItem = map.valueFor(NV_GRAPHIC_DEV_NVMAP_HASH);
		PageMapItem * knvmapPageItem = map.valueFor(NV_GRAPHIC_DEV_KNVMAP_HASH);
		if (nvmapPageItem != NULL) {
			result -= nvmapPageItem->getPss(); // - nvmap_pss
		}
		if (knvmapPageItem != NULL) {
			result -= knvmapPageItem->getPss(); // - knvmap_pss
		}
	} else if (mPlatform == PLATFORM_STE) {
		// STE: pss - mali_pss, but mali_pss in normal case always is 0
		PageMapItem * stePageItem = map.valueFor(STE_GRAPHIC_DEV_HASH);
		if (stePageItem != NULL) {
			result -= stePageItem->getPss(); // - mali_pss
		}
	} else if (mPlatform == PLATFORM_BRCM) {
		// pss - dmabuf_pss, but dmabuf_pss in normal case always is 0
		PageMapItem * bcmPageItem = map.valueFor(QCT_GRAPHIC_ION_DEV_2_HASH);
		if (bcmPageItem != NULL) {
			result -= bcmPageItem->getPss(); // - ion pss
		}
	} else if (mPlatform == PLATFORM_MTK) {
		// MTK: pss - ion_pss
		PageMapItem * iOnPageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_2_HASH);
		if (iOnPageMapItem != NULL) {
		    result -= iOnPageMapItem->getPss(); // - iOn_pss
		}
	}
	return result;
}
#define PROCESS_NAME_LEN 256
long MemoryMeasureHelper::getGraphicsMemoryFromMap(DefaultKeyedVector<unsigned int, PageMapItem*> map,int pid) {
	if (map.isEmpty() == true) return 0;
	long result = 0;

	if (mPlatform == PLATFORM_QCT) {
		// QTC: graphics = kgsl_vss
		// if process name is "/system/bin/surfaceflinger"
		// graphics = kgsl_vss + iOn_rss
		PageMapItem * qctPageMapItem = map.valueFor(QCT_GRAPHIC_DEV_HASH);
		if (qctPageMapItem != NULL) {
			result = qctPageMapItem->getVss(); // + kgsl vss(graphics vss)
			char process_name[PROCESS_NAME_LEN];
			getProcessName(pid, process_name);
			if (strcmp(process_name, "/system/bin/surfaceflinger") == 0) {
				PageMapItem * iOnPageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_1_HASH);
				if (iOnPageMapItem != NULL) {
					result += iOnPageMapItem->getRss(); // + iOn_rss
				}
				iOnPageMapItem = map.valueFor(QCT_GRAPHIC_ION_DEV_2_HASH);
				if (iOnPageMapItem != NULL) {
					result += iOnPageMapItem->getRss(); // + iOn_rss
				}
			}
		}
	} else if (mPlatform == PLATFORM_NV) {
		// NV: graphics = graphics_memory;
		result = getNVProcessGraphicsMemory(pid); // + graphics_memory
	} else if (mPlatform == PLATFORM_STE) {
		// STE: graphics = mali_vss
		PageMapItem * stePageItem = map.valueFor(STE_GRAPHIC_DEV_HASH);
		if (stePageItem != NULL) {
			result = stePageItem->getVss(); // + mali_vss
		}
	} else if (mPlatform == PLATFORM_MTK) {
		// MTK: graphics    /d/pvr/<pid>/process_stats for 6795
		// MTK: graphics    /d/mali/mem/<pid> for 6752, 6572, 6573
		// MTK: graphics     /d/mali0/ctx/<pid>_num/mem_profile // for  6755
		// 6755 impements mem track and save info in /d/mali0/gpu_memory
		char line[500];
		int curPid;
		int len;
		char filename[300];
		bool isFound = false;
		int   maliDirType = -1;
		const char* maliDirStr[3] = {"/d/mali/mem/","/d/mali0/ctx/","/d/pvr/pid/"};
		const char* maliEndStr[3] = {"","/mem_profile","/process_stats"};
		DIR* maliDir;
		dirent* dp;
		FILE *pfd = NULL;

		pfd = fopen("/d/mali0/gpu_memory", "r");
		if (pfd != NULL ){
			/* FORMAT
			mali0                   8237
			kctx-0xffffff8000317000        291      10822
			kctx-0xffffff801072b000       4242       2183*/
			while (0 != fgets(line, 500, pfd)) //while
			{
				if (line[0] == ' ' && line[1] == ' ') {
					result = 0;
					len = sscanf(line, "  %*s %ld %d\n",  &result, &curPid);
					if (len == 2 && curPid == pid) {
						result  =  result  << 12; //page(4KB) to Bytes
						//ALOGW("MTK platform:  PID =%d, get Graphics Memory (GL mtrack): %ld Bytes", pid,  result );
						break;
					}else{
						result = 0;
					}
				}
			}
			fclose(pfd);
			return result;
		}else{
			maliDir  = opendir(maliDirStr[0]);
			if (maliDir != NULL ){
				maliDirType = 0;
			}else {
				maliDir = opendir(maliDirStr[1]);
				if (maliDir != NULL){
					maliDirType = 1;
				}else {
					maliDir = opendir(maliDirStr[2]);
					if (maliDir != NULL){
						maliDirType = 2;
					}
				}
			}
		}

		if ( maliDirType < 0 )  return 0;
		while ((dp = readdir(maliDir)) != NULL){
			curPid = atoi(dp->d_name);
			//ALOGW("MTK platform: get Graphics Memory : existed file %s pid : %d  target  pid = %d", dp->d_name, curPid, pid );
			if (curPid == pid) {
				//ALOGW("MTK platform: get Graphics Memory : found successfully : %s",  filename);
				sprintf(filename,"%s%s%s", maliDirStr[maliDirType],dp->d_name,maliEndStr[maliDirType]);
				//ALOGW("MTK platform: get Graphics Memory : found successfully : %s mtk dir type = %d",  filename, maliDirType);
				isFound = true;
				break;
			}
		}

		closedir(maliDir);
		if (false == isFound ) return 0;
		pfd = fopen(filename, "r");
		if (pfd == NULL) {
			return 0;
		}

		result = 0;
		if (maliDirType == 2){
			long value;
			while (0 != fgets(line, 500, pfd)){
				if (strstr(line, "MemoryUsageKMalloc ") || strstr(line, "MemoryUsageAllocPTMemoryUMA ") || strstr(line, "MemoryUsageAllocGPUMemUMA ")) {
					value = 0;
					sscanf(line, "%*s %ld", &value);
					result += value;
				}
			}
		}else{
			while (0 != fgets(line, 500, pfd)){
				if (strstr(line, "Total allocated memory:") != NULL ){
					sscanf(line, "Total allocated memory: %ld", &result);
					// ALOGW("MTK platform: get Graphics Memory : %ld ",  result );
					break;
				}
			}
		}
		fclose(pfd);
	}
	return result;
}
void MemoryMeasureHelper::getProcessName(int pid,char* name) {
    if(pid < 0 || name == NULL) return;
    char filename[64] = {0};
    snprintf(filename, sizeof(filename), "/proc/%d/cmdline", pid);

    FILE * file = fopen(filename, "r");
    if (file == NULL) {
        snprintf(name, PROCESS_NAME_LEN, "unknown");
    }
    else {
        fscanf(file, "%63s", name);
        fclose(file);
        file = NULL;
    }
}
void MemoryMeasureHelper::mapClear(DefaultKeyedVector<unsigned int, PageMapItem*> map) {
    if(map.isEmpty() == true) return;
    int size = map.size();
    //ALOGE("MemoryMeasureHelper::mapClear size = %d", size);
    for (int i = size -1; i >= 0; --i) {
        PageMapItem* item = map.valueAt(i);
        //ALOGE("MemoryMeasureHelper::mapClear delete index=%d", i);
        delete item;
    }
	map.clear();
}
int MemoryMeasureHelper::detectPlatform()
{
    // It need to add new platform type if there are other platform in the future.

	int result = PLATFORM_NONE;
	char platform[PROPERTY_VALUE_MAX];

	property_get("ro.board.platform", platform, "unknown");

	FILE *file = NULL;	
	if (strstr(platform, "msm") == platform || strstr(platform, "apq") == platform || strstr(platform, "sdm") == platform) {
		result = PLATFORM_QCT;
	} else if (property_get("ro.mediatek.platform", platform, NULL) > 0){
		result = PLATFORM_MTK;
	} else {
		file = fopen("/sys/kernel/debug/nvmap/iovmm/clients", "r");
		if (file) {
			result = PLATFORM_NV;
			fclose(file);
		} else {
			file = fopen("/dev/mali", "r");
			if (file) {
				result = PLATFORM_STE;
				fclose(file);
			} else {
				file = fopen("/system/lib/libbrcm_ril.so", "r");
				if (file) {
					result = PLATFORM_BRCM;
					fclose(file);
				}
			}
		}
	}
	file = NULL;
	return result;
}
int MemoryMeasureHelper::getPlatformType() {
	return mPlatform;
}
} // namespace android

