
#ifndef UTILS_MEMORY_MEASURE_HELPER_H
#define UTILS_MEMORY_MEASURE_HELPER_H

#include <utils/KeyedVector.h>
//#include <utils/threads.h>

namespace android {

class PageMapItem {
private:
    static const int NAME_BUF_SIZE = 100;
    void setName(const char * name);
public:
    inline PageMapItem() { clear();};
    PageMapItem(const char * name, long vss, long rss, long pss, long swap, long pswap, long sharedClean, long sharedDirty,
            long privateClean, long privateDirty, long referenced, long anonymous,
            long anonHugePages, long kernelPagesize, long mMUPageSize,
            long locked);

    inline const char * getName () { return mName; };
    inline int getVss () { return mVss; };
    inline int getRss () { return mRss; };
    inline int getPss () { return mPss; };
    inline int getSharedClean () { return mSharedClean; };
    inline int getSharedDirty () { return mSharedDirty; };
    inline int getPrivate_Clean () { return mPrivate_Clean; };
    inline int getPrivate_Dirty () { return mPrivate_Dirty; };
    inline int getReferenced () { return mReferenced; };
    inline int getAnonymous () { return mAnonymous; };
    inline int getAnonHugePages () { return mAnonHugePages; };
    inline int getSwap () { return mSwap; };
    inline int getPSwap () { return mPSwap; };
    inline int getKernelPageSize () { return mKernelPageSize; };
    inline int getMMUPageSize () { return mMMUPageSize; };
    inline int getLocked () { return mLocked; };
    bool toArray(long ** out);
    void combine(PageMapItem * item);
    void combineMainValues(PageMapItem * item);
    void init(const char * name, long vss, long rss, long pss, long swap, long pswap, long sharedClean, long sharedDirty,
            long privateClean, long privateDirty, long referenced, long anonymous,
            long anonHugePages, long kernelPagesize, long mMUPageSize,
            long locked);
    void clear();

private:
    char mName[NAME_BUF_SIZE+1];
    long mVss;
    long mRss;
    long mPss;
    long mSharedClean;
    long mSharedDirty;
    long mPrivate_Clean;
    long mPrivate_Dirty;
    long mReferenced;
    long mAnonymous;
    long mAnonHugePages;
    long mSwap;
    long mPSwap;
    long mKernelPageSize;
    long mMMUPageSize;
    long mLocked;
};

class MemoryMeasureHelper {

private:

    MemoryMeasureHelper();

public:
    static long getProcessTotalMemory(int pid);
    static bool getDetailRegionMemory(int pid, const char * name, long ** out);
    static long getRegionPssMemory(int pid, const char * name);
    static bool getQCTProcessMemoryForLowMemDetail(int pid, long ** out);
    static bool getNVProcessMemoryForLowMemDetail(int pid, long ** out);
    static bool getSTEProcessMemoryForLowMemDetail(int pid, long ** out);
    static bool getBRCMProcessMemoryForLowMemDetail(int pid, long ** out);
    static bool getMTKProcessMemoryForLowMemDetail(int pid, long ** out);
    static bool getProcessMemoryForLowMemDetail(int pid, long ** out);
    static long getProcessPssOnly(int pid);
    static long getProcessGraphicsMemory(int pid);
    static unsigned int BKDRHash(const char *str);
    static int getPlatformType();
    static int detectPlatform();
    static const int PLATFORM_NONE = 0;
    static const int PLATFORM_NV = 1;
    static const int PLATFORM_QCT = 2;
    static const int PLATFORM_STE = 3;
    static const int PLATFORM_BRCM = 4;
    static const int PLATFORM_MTK = 5;
private:
    static DefaultKeyedVector<unsigned int, PageMapItem*> getProcessPageMapItems(int pid, bool allValues);
    static long getProcessPssOnlyFromMap(DefaultKeyedVector<unsigned int, PageMapItem*> map);
    static long getGraphicsMemoryFromMap(DefaultKeyedVector<unsigned int, PageMapItem*> map,int pid);
    static void getProcessName(int pid,char* name);
    static long getNVProcessGraphicsMemory(int pid);
    static void mapClear(DefaultKeyedVector<unsigned int, PageMapItem*> map);
};

}

#endif // UTILS_MEMORY_MEASURE_HELPER_H

