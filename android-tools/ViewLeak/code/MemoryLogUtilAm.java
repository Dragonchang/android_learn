package com.android.server.am;

import java.io.File;
import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Vector;
import java.util.Map.Entry;

import android.os.Process;
import android.os.SystemClock;
import android.util.Slog;
import android.util.Log;

public class MemoryLogUtilAm {
    private final static String TAG = "MemoryLogUtilAm";
    private static final int DURATION_DUMP_LOW_MEMORY = 20 * 1000;
    private static final int DURATION_DUMP_AM_LOW_MEMORY = 10 * 60 * 1000;
    private static final int NUM_EXTRAM_MEMINFO = 16;
    private static final long sLargeRamCriteria = (long) 1.5 * 1024 * 1024;

    private static long sLastTimeDumpLowMemory = 0;
    private static long sKernelStack;
    private static long sPageTables;
    private static long sKGSLAlloc;
    private static long sIONTotal;
    private static long sIONInUse;
    private static long sFreeCma;
    private static long sTotalCma;
    private static long sSwapTotal;
    private static long sSwapFree;
    private static long sZramInUse;
    private static long sSwapCached;
    private static long sBuffers;
    private static long sShmem;
    private static long sSlab;
    private static long sTotal;
    private static long sCached;
    private static long sFree;
    private static long sVmallocUsed;

    private static boolean sIsLargeRam = false;
    private static boolean sIsDumping = false;

    static final int PLATFORM_NONE = 0;
    static final int PLATFORM_NV = 1;
    static final int PLATFORM_QCT = 2;
    static final int PLATFORM_BRCM = 3;
    static final int PLATFORM_MTK_PVR = 4;
    static final int PLATFORM_MTK_MALI = 5;

    static final int MIN_DUMP_ADJ = 600;
    static final int FORCE_DUMP_ADJ = 400;
    static int mPlatform = PLATFORM_NONE;

    static void init() {
        com.android.internal.util.MemInfoReader memInfoReader = new com.android.internal.util.MemInfoReader();
        memInfoReader.readMemInfo();
        Log.d(TAG, "MemUti_INIT_Start");
        long total = memInfoReader.getTotalSizeKb();
        if (total > sLargeRamCriteria)
            sIsLargeRam = true;

        File file = new File("/sys/kernel/debug/nvmap/iovmm/clients");
        if (file.exists())
            mPlatform = PLATFORM_NV;
        else {
            file = new File("/d/pvr/pid/");
            if (file.exists())
                mPlatform = PLATFORM_MTK_PVR;
            else {
                file = new File("/d/mali/mem/");
                File file1 = new File("/d/mali0/ctx/");
                if (file.exists() || file1.exists())
                    mPlatform = PLATFORM_MTK_MALI;
                else {
                    file = new File("/system/lib/libbrcm_ril.so");
                    if (file.exists())
                        mPlatform = PLATFORM_BRCM;
                    else
                        mPlatform = PLATFORM_QCT;
                }
            }
        }
    }

    static String dumpLowMemoryLog(ArrayList<ProcessRecord> lruProcesses, ProcessRecord TOP_APP) {
        return dumpLowMemoryLog(lruProcesses, TOP_APP, null);
    }

    // Dump low memory log for root cause analysis.
    static String dumpLowMemoryLog(ArrayList<ProcessRecord> lruProcesses, ProcessRecord TOP_APP, ProcessRecord dyingProc) {
        try {
            boolean isZflLauncherKilled = false;
            boolean forceDump = false;
            long timeDiff = SystemClock.elapsedRealtime() - sLastTimeDumpLowMemory;

            if (dyingProc != null) {
                if (dyingProc.curAdj <= ProcessList.HOME_APP_ADJ && "com.Zfl.launcher".equals(dyingProc.processName)) {
                    isZflLauncherKilled = true;
                } else if (!"home".equals(dyingProc.adjType)) {
                    for (ProcessRecord pr : lruProcesses) {
                        if (pr.curAdj > MIN_DUMP_ADJ) {
                            if (sIsLargeRam && (timeDiff > DURATION_DUMP_AM_LOW_MEMORY || sLastTimeDumpLowMemory == 0)) {
                                return doDump(lruProcesses, TOP_APP, dyingProc, isZflLauncherKilled, true);
                            } else
                                return null;
                        }
                    }
                } else {
                    forceDump = true;
                }
            }

            if (!isZflLauncherKilled && !forceDump && (timeDiff < DURATION_DUMP_LOW_MEMORY && timeDiff > 0)) {
                return null;
            }

            return doDump(lruProcesses, TOP_APP, dyingProc, isZflLauncherKilled, true);

        } catch (Exception e) {
            Log.d(TAG, "Error while dumping low memory logs.", e);
            sIsDumping = false;
            return null;
        }
    }

    static void dumpInBackground(final ArrayList<ProcessInfo> allProcList, final String TOP_APP) {
        new Thread("DumpMemoryLog"){
            @Override
            public void run(){
                Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
                doDump(allProcList, TOP_APP, false);
            }
        }.start();
    }

    static String doDump(ArrayList<ProcessRecord> lruProcesses, ProcessRecord TOP_APP, ProcessRecord dyingProc,
            boolean isZflLauncherKilled, boolean doInBg) {
        // Protect not to generate two dumping threads at the same time to avoid CPU busy
        if (sIsDumping){
            return null;
        } else
            sIsDumping = true;

        sLastTimeDumpLowMemory =  SystemClock.elapsedRealtime();
        ArrayList<ProcessInfo> allProcList = new ArrayList<ProcessInfo>();
        Vector<Integer> processIDs =  new Vector<Integer>();
        StringBuilder serviceInfo = new StringBuilder();
        StringBuilder providerInfo = new StringBuilder();

        for (ProcessRecord rec : lruProcesses) {
            boolean isBinded = false;
            if (rec.adjSource != null && rec.adjTarget != null) {
                isBinded = true;
            }
            serviceInfo.setLength(0);
            providerInfo.setLength(0);
            if (rec.adjType.contains("service")) {
                for (ServiceRecord sr : rec.services) {
                    if (isBinded && !sr.name.toString().equals(rec.adjTarget.toString())) {
                        continue;
                    }

                    long duration = SystemClock.uptimeMillis() - sr.executingStart;
                    serviceInfo.append(sr.name.getClassName());
                    if (rec.adjSource != null)
                        serviceInfo.append(" <- ").append(((ProcessRecord) rec.adjSource).toShortString());
                    serviceInfo.append(" For ").append(duration).append(" ms.");
                }
            } else if (rec.adjType.contains("provider")) {
                Iterator<ContentProviderRecord> iter = rec.pubProviders.values().iterator();
                while (iter.hasNext()) {
                    ContentProviderRecord cpRec = iter.next();
                    if (isBinded && !cpRec.name.toString().equals(rec.adjTarget.toString())) {
                        continue;
                    }

                    providerInfo.append(cpRec.name.getClassName());
                    if (rec.adjSource != null)
                        providerInfo.append(" <- ").append(((ProcessRecord) rec.adjSource).toShortString());
                    providerInfo.append(".");
                }
            }

            allProcList.add(new ProcessInfo (rec.pid, rec.curAdj, rec.processName, rec.adjType, serviceInfo.toString(), providerInfo.toString()));
            processIDs.add(rec.pid);
        }

        int[] pids = Process.getPids("/proc", null);
        for (int id : pids) {
            if (id != -1 && processIDs.indexOf(id) == -1) {
                allProcList.add(new ProcessInfo (id, -1000, null, "native", "", ""));
            }
        }

        if (doInBg){
            dumpInBackground(allProcList, TOP_APP == null ? "" : TOP_APP.processName);
            return "";
        } else{
            return doDump(allProcList,  TOP_APP == null ? "" : TOP_APP.processName, isZflLauncherKilled);
        }
    }

    static String doDump(ArrayList<ProcessInfo>  allProc, String TOP_APP, boolean isZflLauncherKilled){
        StringBuilder result = new StringBuilder();
        try{
            String log;
            Vector<Integer> processIDs = null;
            processIDs = new Vector<Integer>();
            Log.d(TAG, "dump begin");

            if (!TOP_APP.equals("")) {
                log = "TOP_APP= " + TOP_APP;
                Log.d(TAG, log);
                if (isZflLauncherKilled)
                    result.append(log).append("\r\n");
            }
            if (isZflLauncherKilled)
                result.append(dumpHeader()).append("\r\n");
            else
                dumpHeader();

            for (ProcessInfo info : allProc){
                log = dumpProcessStats(info.mPid, info.mAdj, info.mName, info.mAdjType, info.mServiceinfo, info.mProviderInfo, isZflLauncherKilled);
                if (isZflLauncherKilled && log != null && log.length() > 0) {
                    result.append(log).append("\r\n");
                }
            }

            /* Get RAM information according to following order.
                 1. Get most info from file node /proc/meminfo in readExtraMeminfo().
                      If some values are 0 (not implemented yet), we must get them from the following.
                 2. Get HW acceleration memory(mtk), CMA, zram, vmalloc from related file nodes in readExtraMeminfo().
                 3. Get ION memory use from related file nodes in dumpMemoryLogFromFile().
                 4. Get HW acceleration memory(mtk) from related file nodes in dumpMtkGraphic().
                 5. Dump RAM info in dumpMemInfo() */
            readExtraMemInfo();
            if (isZflLauncherKilled) {
                result.append(dumpMemoryLogFromFile(isZflLauncherKilled));
                result.append(dumpMtkGraphic(isZflLauncherKilled));
                result.append(dumpMemInfo());
            } else {
                dumpMemoryLogFromFile(isZflLauncherKilled);
                dumpMtkGraphic(isZflLauncherKilled);
                dumpMemInfo();
            }

            dumpNVAllocationLog();
            Log.d(TAG, "dump end");
        } catch(Exception e) {
            Log.d(TAG, "Error while dumping low memory logs.", e);
        } finally{
            sIsDumping = false;
        }
        return result.toString();
    }

    static String dumpMtkGraphic(boolean isZflLauncherKilled) {
        if (mPlatform == PLATFORM_MTK_PVR) {
            return dumpMtkPvrGraphic(isZflLauncherKilled);
        } else if (mPlatform == PLATFORM_MTK_MALI) {
            return dumpMtkMaliGraphic(isZflLauncherKilled);
        }

        return "";
    }

    static String getProcessNameFromPid(int pid) {
        File file = new File("/proc/" + pid + "/cmdline");
        BufferedReader br = null;
        if (file.exists()) {
            try {
                String line;
                br = new BufferedReader(new FileReader(file));
                char[] temp = new char[50];
                while ((br.read(temp)) != -1) {
                    String proc = new String(temp);
                    Log.d(TAG, "Process name : " + proc);
                    return proc;
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
        }
        return "";
    }

    static String dumpMemInfo() {
        StringBuilder  result = new StringBuilder();
        //readExtraMemInfo();

        result.append("RAM: ").append(getTotal()).append(" KB Total, ")
                      .append(getFree()).append(" KB Free, ")
                      .append(getBuffers()).append(" KB Buffers, ")
                      .append(getCached()).append(" KB Cached, ")
                      .append(getShmem()).append(" KB Shmem, ")
                      .append(getSlab()).append(" KB Slab, ")
                      .append(getKernelStack()).append(" KB KernelStack, ")
                      .append(getPageTable()).append(" KB PageTable, ")
                      .append(getKGSLAlloc()).append(" KB KGSL_ALLOC, ")
                      .append(getIONToal()).append(" KB ION_Total, ")
                      .append(getIONInUse()).append(" KB ION_InUse, ")
                      .append(getTotalCma()).append(" KB TotalCma, ")
                      .append(getFreeCma()).append(" KB FreeCma, ")
                      .append(getSwapTotal()).append(" KB SwapTotal, ")
                      .append(getSwapFree()).append(" KB SwapFree, ")
                      .append(getZramInUse()).append(" KB ZramInUse, ")
                      .append(getSwapCached()).append(" KB SwapCached, ")
                      .append(getVmallocUsed()).append(" KB VmallocUsed.");

        Log.d(TAG, result.toString());

        return result.append("\r\n").toString();
    }

    private static void readExtraMemInfo() {
        sKernelStack = 0L;
        sPageTables = 0L;
        sKGSLAlloc = 0L;
        sIONTotal = 0L;
        sIONInUse = 0L;
        sFreeCma = 0L;
        sTotalCma = 0L;
        sSwapTotal = 0L;
        sSwapFree = 0L;
        sZramInUse = 0L;
        sSwapCached = 0L;
        sBuffers = 0L;
        sShmem = 0L;
        sSlab = 0L;
        sTotal = 0L;
        sFree = 0L;
        sCached =0L;
        sVmallocUsed = 0L;

        int count = 0;
        String line;
        BufferedReader br = null;
        try {
            br = new BufferedReader(new FileReader("/proc/meminfo"));
            while ((line = br.readLine()) != null && count < NUM_EXTRAM_MEMINFO) {
                if (line.startsWith("KernelStack:")) {
                    sKernelStack = getValue(line);
                    count++;
                } else if (line.startsWith("PageTables:")) {
                    sPageTables = getValue(line);
                    count++;
                } else if (line.toLowerCase().startsWith("kgslalloc:") || line.toLowerCase().startsWith("kgsl_alloc:")) {
                    sKGSLAlloc = getValue(line);
                    count++;
                } else if (line.startsWith("IonTotal:") || line.startsWith("ION_ALLOC:")) {
                    sIONTotal = getValue(line);
                    count++;
                } else if (line.toLowerCase().startsWith("ioninuse:")) {
                    sIONInUse = getValue(line);
                    count++;
                } else if (line.startsWith("FreeCma:") || line.startsWith("CmaFree:")) {
                    sFreeCma = getValue(line);
                    count++;
                } else if (line.startsWith("SwapTotal:")) {
                    sSwapTotal = getValue(line);
                    count++;
                } else if (line.startsWith("SwapFree:")) {
                    sSwapFree = getValue(line);
                    count++;
                } else if (line.startsWith("ZramAlloc:")) {
                    sZramInUse = getValue(line);
                    count++;
                } else if (line.startsWith("SwapCached:")) {
                    sSwapCached = getValue(line);
                    count++;
                } else if (line.startsWith("Buffers:")) {
                    sBuffers = getValue(line);
                    count++;
                } else if (line.startsWith("Shmem:")) {
                    sShmem = getValue(line);
                    count++;
                } else if (line.startsWith("Slab:")) {
                    sSlab = getValue(line);
                    count++;
                } else if (line.startsWith("MemTotal:")) {
                    sTotal = getValue(line);
                    count++;
                } else if (line.startsWith("MemFree:")) {
                    sFree = getValue(line);
                    count++;
                } else if (line.startsWith("Cached:")) {
                    sCached = getValue(line);
                    count++;
                }
            }
        } catch (Exception e) {
        } finally {
            if (br != null)
                try {
                    br.close();
                } catch (IOException e) {
                }
        }
        //Get total HW acceleration memory use in MTK  6755
        if (sKGSLAlloc == 0L && mPlatform == PLATFORM_MTK_MALI) {
            File file = null;
            String split[];
            int len;
            long tmpKGSLAlloc = 0;
            /*   /d/mali0/gpu_memory
            FORMAT
            mali0                   8237
            kctx-0xffffff8000317000        291      10822
            kctx-0xffffff801072b000       4242       2183*/

            try {
                file = new File("/d/mali0/gpu_memory");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        split = line.trim().split("\\s+");
                        if(split.length == 2 && split[0].equals("mali0")) {
                            tmpKGSLAlloc = Long.parseLong(split[1].trim());
                            break;
                        }
                    }
                    if (tmpKGSLAlloc > 0)
                        sKGSLAlloc =  tmpKGSLAlloc << 2 ; //4KB page size to KB
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
            Log.d(TAG, "Gets  Total  KGSL AllOC: " + sKGSLAlloc  + " KB \r\n");
        }

        if (sTotalCma == 0L || sFreeCma == 0L) {
            File file = null;
            boolean isCmaBlock = false;
            String split[];
            int len;
            int cmaIndex= -1;
            long tmpFreeCma = 0;
            long tmpTotalCma = 0;
            try {
                file = new File("/proc/pagetypeinfo");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        if  ( line.startsWith("Number of blocks type") ){
                            isCmaBlock = true;
                            cmaIndex = line.indexOf("CMA");
                        } else if (false == isCmaBlock) {
                            cmaIndex = line.indexOf("CMA");
                            if (cmaIndex < 0) continue;
                            split = line.substring(cmaIndex + 4).trim().split("\\s+");
                            len = split.length;
                            if (len > 11)  len = 11;
                            for (int i = 0; i < len; i++)
                                tmpFreeCma += Long.parseLong(split[i].trim()) << i;
                        } else {
                            if (cmaIndex < 0)  continue;
                            tmpTotalCma += Long.parseLong(line.substring(cmaIndex, cmaIndex+3).trim());
                        }
                    }
                    if (sFreeCma == 0)
                        sFreeCma = tmpFreeCma << 2; //4KB page size to KB
                    if (sTotalCma == 0)
                        sTotalCma = tmpTotalCma << 12; //4MB to KB
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
            Log.d(TAG, "Gets  Total  CMA: " + sTotalCma  + " KB, "  +  "Free CMA: " +  sFreeCma  + " KB\r\n");
        }

        File file = null;
        if (sZramInUse == 0L) {
            try {
                file = new File("/sys/block/zram0/mem_used_total");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        sZramInUse += Long.parseLong(line.trim()) / 1024L;
                    }
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
            try {
                file = new File("/sys/block/zram1/mem_used_total");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        sZramInUse += Long.parseLong(line.trim()) / 1024L;
                    }
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
            try {
                file = new File("/sys/block/zram2/mem_used_total");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        sZramInUse += Long.parseLong(line.trim()) / 1024L;
                    }
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
            try {
                file = new File("/sys/block/zram3/mem_used_total");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        sZramInUse += Long.parseLong(line.trim()) / 1024L;
                    }
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }

            try {
                file = new File("/proc/vmallocinfo");
                if (file.exists()) {
                    br = new BufferedReader(new FileReader(file));
                    while ((line = br.readLine()) != null) {
                        if (line.contains("ioremap") || line.contains("map_lowmem"))
                            continue;
                        String split[] = line.split("\\s+");
                        sVmallocUsed += Long.parseLong(split[1]) / 1024L;
                    }
                }
            } catch (Exception e) {
            } finally {
                if (br != null)
                    try {
                        br.close();
                    } catch (IOException e) {
                    }
            }
        }
    }

    private static long getValue(String line) {
        String[] split = line.split("\\s+");
        long value = 0;
        value = Long.parseLong(split[1]);
        return value;
    }

    private static long getKernelStack() {
        return sKernelStack;
    }

    private static long getPageTable() {
        return sPageTables;
    }

    private static long getKGSLAlloc() {
        return sKGSLAlloc;
    }

    private static long getIONToal() {
        return sIONTotal;
    }

    private static long getIONInUse() {
        return sIONInUse;
    }

    private static long getTotalCma() {
        return sTotalCma;
    }

    private static long getFreeCma() {
        return sFreeCma;
    }

    private static long getSwapTotal() {
        return sSwapTotal;
    }

    private static long getSwapFree() {
        return sSwapFree;
    }

    private static long getZramInUse() {
        return sZramInUse;
    }

    private static long getSwapCached() {
        return sSwapCached;
    }

    private static long getBuffers() {
        return sBuffers;
    }

    private static long getShmem() {
        return sShmem;
    }

    private static long getSlab() {
        return sSlab;
    }

    private static long getTotal() {
        return sTotal;
    }

    private static long getFree() {
        return sFree;
    }

    private static long getCached() {
        return sCached;
    }

    private static long getVmallocUsed() {
        return sVmallocUsed;
    }

    static String dumpMemoryLogFromFile(boolean isHomeKilled) {
        StringBuilder result = new StringBuilder();
        BufferedReader input = null;
        String line = null;
        String path = null;

        if (mPlatform == PLATFORM_NV) {
            path = "/sys/kernel/debug/nvmap/iovmm/clients";
        } else if (mPlatform == PLATFORM_BRCM) {
            path = "/d/ion/ion-system-extra";
        }

        if (path != null) {
            try {
                input = new BufferedReader(new FileReader(path));
                while ((line = input.readLine()) != null) {
                    Log.d(TAG, mPlatform == PLATFORM_NV ? line : "ion-extra : " + line);
                }
            } catch (Exception e) {
                Slog.e(TAG, "Error logging memory file " + path, e);
            } finally {
                if (input != null)
                    try {
                        input.close();
                    } catch (IOException e) {
                    }
            }
        }

        path = "/d/ion/iommu";
        try {
            File file = new File(path);
            if (file.exists()) {
                HashMap<String, Long> allocMap = new HashMap<String, Long>();
                HashMap<String, IonInfo> infoMap = new HashMap<String, IonInfo>();
                StringBuilder sb;
                input = new BufferedReader(new FileReader(path));
                while ((line = input.readLine()) != null) {
                    if (!line.contains("client              pid") && !line.contains("client          creator")) {
                        String[] split = line.split("\\s+");
                        if (split.length == 4) {
                            sb = new StringBuilder();
                            final String key = sb.append(split[1]).append("_").append(split[2]).toString();
                            if (!allocMap.containsKey(key))
                                allocMap.put(key, Long.parseLong(split[3]));
                            else
                                allocMap.put(key, allocMap.get(key) + Long.parseLong(split[3]));
                            continue;
                        }
                        if (split.length == 5) {
                            sb = new StringBuilder();
                            final String key = sb.append(split[1]).append(" ").append(split[2]).toString();
                            if (!infoMap.containsKey(key))
                                infoMap.put(key, new IonInfo(split[1], split[2], Long.parseLong(split[3])));
                            else
                                infoMap.get(key).mSize += Long.parseLong(split[3]);
                            continue;
                        }
                    }

                    if (line.startsWith("Total bytes currently")) {
                        for (String key : allocMap.keySet()) {
                            int index = key.lastIndexOf("_");
                            String client = key.substring(0, index);
                            String pid = key.substring(index + 1);
                            String tmp = "iommu : "
                                    + String.format("%1$16s %2$16s %3$16d", client, pid, allocMap.get(key));
                            Log.d(TAG, tmp);
                            if (isHomeKilled)
                                result.append(tmp).append("\r\n");
                        }
                    }
                    if (line.contains("client          creator")) {
                        String tmp = "iommu : " + line.replace("size (hex)", "      size");
                        Log.d(TAG, tmp);
                        if (isHomeKilled)
                            result.append(tmp).append("\r\n");
                        continue;
                    }
                    Log.d(TAG, "iommu : " + line);
                    if (isHomeKilled)
                        result.append("iommu : " + line).append("\r\n");
                }
                for (String key : infoMap.keySet()) {
                    String tmp = "iommu : "
                            + String.format("%1$16s %2$16s %3$14d", infoMap.get(key).mClient,
                                    infoMap.get(key).mCreator, infoMap.get(key).mSize);
                    Log.d(TAG, tmp);
                    if (isHomeKilled)
                        result.append(tmp).append("\r\n");
                }
            }
        } catch (Exception e) {
            Slog.e(TAG, "Error logging memory file " + path, e);
        } finally {
            if (input != null)
                try {
                    input.close();
                } catch (IOException e) {
                }
        }

        path = "/d/ion/heaps/system";
        try {
            File file = new File(path);
            if (file.exists()) {
                Map<ProcessStatus, Long> processStatusMap = new HashMap<ProcessStatus, Long>();
                ProcessStatus process;
                input = new BufferedReader(new FileReader(path));
                while ((line = input.readLine()) != null) {
                    String[] split = line.split("\\s+");

                    if (split.length == 6 || split.length == 5 || split.length == 4) {
                        String trim = line.trim();
                        if (trim.startsWith("client              pid") || trim.startsWith("total orphaned")
                                || trim.startsWith("deferred free")) {
                            Log.d(TAG, "ion-heaps : " + line);
                            if (isHomeKilled)
                                result.append("ion-heaps : " + line).append("\r\n");
                            continue;
                        }
                        long memory = Long.parseLong(split[3]);
                        process = new ProcessStatus(split[1], Integer.parseInt(split[2]));
                        if (processStatusMap.containsKey(process))
                            processStatusMap.put(process, processStatusMap.get(process) + memory);
                        else {
                            processStatusMap.put(process, memory);
                        }
                    } else {
                        if (!processStatusMap.isEmpty()) {
                            for (ProcessStatus key : processStatusMap.keySet()) {
                                String tmp = String.format("ion-heaps : %1$16s  %2$15d  %3$15d", key.getProcessName(),
                                        key.getPid(), processStatusMap.get(key));
                                Log.d(TAG, tmp);
                                if (isHomeKilled)
                                    result.append(tmp).append("\r\n");
                            }
                            processStatusMap.clear();
                        }
                        Log.d(TAG, "ion-heaps : " + line);
                        if (isHomeKilled)
                            result.append("ion-heaps : " + line).append("\r\n");
                    }
                }
            }
        } catch (Exception e) {
            Slog.e(TAG, "Error logging memory file " + path, e);
        } finally {
            if (input != null)
                try {
                    input.close();
                } catch (IOException e) {
                }
        }

        path = "/d/ion/heaps/ion_mm_heap";
        try {
            File file = new File(path);
            if (file.exists()) {
                int indexClient = -1;
                int indexlDbgName = -1;
                int indexPid = -1;
                int indexSize = -1;
                boolean isMatch = false;
                Map<ProcessStatus, Long> processStatusMap = new HashMap<ProcessStatus, Long>();
                ProcessStatus process;
                String tmp = "";
                long tmpIONInUse = 0;
                long tmpIONTotal = 0;

                input = new BufferedReader(new FileReader(path));
                while ((line = input.readLine()) != null) {
                    if (line.startsWith("mm_heap_freelist total_size")) {
                        tmp = "ion_mm_heap : " + line;
                        Log.d(TAG, tmp);
                        if (isHomeKilled)
                            result.append(tmp).append("\r\n");
                        break;
                    }
                    if (!isMatch && line.startsWith("          client(")) {
                        indexClient = line.indexOf("client(") + 7;
                        indexlDbgName = line.indexOf("dbg_name)") + 9;
                        indexPid = line.indexOf("pid") + 3;
                        indexSize = line.indexOf("size") + 4;
                        if (indexClient > 0 && indexlDbgName > 0 && indexPid > 0 && indexSize > 0)
                            isMatch = true;

                        tmp = "ion_mm_heap : " + line.replace("address", "");
                        Log.d(TAG, tmp);
                        if (isHomeKilled)
                            result.append(tmp).append("\r\n");
                        continue;
                    }

                    if (isMatch && line.indexOf("(") + 1 == indexClient && line.indexOf(")") + 1 == indexlDbgName) {
                        try {
                            long memory = Long.parseLong(line.substring(indexPid + 1, indexSize).trim());
                            String client = line.substring(0, indexClient).trim()
                                    + line.substring(indexClient + 1, indexlDbgName).trim();
                            int pid = Integer.parseInt(line.substring(indexlDbgName + 1, indexPid).trim());
                            process = new ProcessStatus(client, pid);
                            if (processStatusMap.containsKey(process))
                                processStatusMap.put(process, processStatusMap.get(process) + memory);
                            else {
                                processStatusMap.put(process, memory);
                            }
                        } catch (Exception e) {
                            tmp = "ion_mm_heap : " + line;
                            Log.d(TAG, tmp);
                            if (isHomeKilled)
                                result.append(tmp).append("\r\n");
                        }
                    } else if (line.trim().startsWith("--------------------------------")) {
                        if (!processStatusMap.isEmpty()) {
                            for (ProcessStatus key : processStatusMap.keySet()) {
                                tmp = String.format("ion_mm_heap : %1$34s %2$16d  %3$15d", key.getProcessName(),
                                        key.getPid(), processStatusMap.get(key));
                                Log.d(TAG, tmp);
                                if (isHomeKilled)
                                    result.append(tmp).append("\r\n");
                            }
                            processStatusMap.clear();
                        }
                        tmp = line;
                        Log.d(TAG, tmp);
                        if (isHomeKilled)
                            result.append(tmp).append("\r\n");
                    } else {
                        String[] strs = line.trim().split("\\s+");
                        if (strs.length == 10 && strs[1].trim().equals("order")) {
                            tmpIONTotal += (Long.parseLong(strs[8].trim()) >> 10); //Byte to KB
                            //Log.d(TAG, "getIonTotal :" + line + " length :"+ strs.length + " ,order  match:" +  strs[1]  +  "IonTotal string:" + strs[8] );
                        }
                        else if (  strs.length == 2 &&  strs[0].trim().equals("total" ) ){
                            tmpIONInUse = Long.parseLong(strs[1].trim()) >>10; //Byte to KB
                            //Log.d(TAG, "getIonUsed :" + " total ion use:" +  strs[1] );
                        }
                        tmp = "ion_mm_heap : " + line;
                        Log.d(TAG, tmp);
                        if (isHomeKilled)
                            result.append(tmp).append("\r\n");
                    }
                }
                if (sIONTotal == 0)
                    sIONTotal = tmpIONTotal + tmpIONInUse;
                if (sIONInUse == 0)
                    sIONInUse = tmpIONInUse;
            }
        } catch (Exception e) {
        } finally {
            if (input != null)
                try {
                    input.close();
                } catch (IOException e) {
                }
        }
        return result.toString();
    }

    static void dumpNVAllocationLog() {
        if (mPlatform != PLATFORM_NV)
            return;

        File nvmapFile = new File("/sys/kernel/debug/nvmap/iovmm/allocations");
        BufferedReader input = null;
        String line = null;
        Map<String, Long> NVMap = new HashMap<String, Long>();
        boolean isFirst = true;
        int pid = 0;
        String processName = "";

        try {
            input = new BufferedReader(new InputStreamReader(new FileInputStream(nvmapFile)));
            while ((line = input.readLine()) != null) {
                String[] strs = line.trim().split("\\s+");
                if (strs.length < 3)
                    continue;
                if (strs[2].equals("PID"))
                    continue;
                if (strs[0].equals("total")) {
                    Iterator<Entry<String, Long>> it = NVMap.entrySet().iterator();
                    while (it.hasNext()) {
                        Entry<String, Long> entry = it.next();
                        Log.d(TAG, "NVMapInfo, Type: " + entry.getKey() + ", Size: " + entry.getValue());
                    }

                    Log.d(TAG, "NVMapInfo, Total: " + strs[2]);
                    break;
                }
                if (!strs[0].equals("0")) {
                    if (!isFirst) {
                        Iterator<Entry<String, Long>> it = NVMap.entrySet().iterator();
                        while (it.hasNext()) {
                            Entry<String, Long> entry = it.next();
                            Log.d(TAG, "NVMapInfo, Type: " + entry.getKey() + ", Size: " + entry.getValue());
                        }
                    }
                    NVMap.clear();
                    processName = strs[1];
                    pid = Integer.parseInt(strs[2]);
                    int value = Integer.parseInt(strs[3]);
                    Log.d(TAG, "NVMapInfo, processName: " + processName + ", pid: " + pid + ", total: " + value);
                    isFirst = false;
                } else {
                    long value = Long.parseLong(strs[1]);

                    String typeKey = "";
                    int length = strs[2].length();
                    if (length < 5) {
                        typeKey = "0";
                    } else {
                        typeKey = strs[2].substring(0, length - 4);
                    }

                    Long mapValue = NVMap.get(typeKey);
                    if (mapValue == null) {
                        NVMap.put(typeKey, value);
                    } else {
                        NVMap.put(typeKey, (mapValue + value));
                    }
                }
            }
        } catch (Exception e) {
            Slog.e(TAG, "Error logging mvmap allocation file " + nvmapFile.getPath(), e);
        } finally {
            if (input != null)
                try {
                    input.close();
                } catch (IOException e) {
                }
        }
    }

    static String dumpMtkPvrGraphic(boolean isZflLauncherKilled) {
        StringBuilder sb = new StringBuilder();
        BufferedReader br = null;
        String tmp = "";
        long  tmpKGSLAlloc = 0;
        try {
            String parentPath = "/d/pvr/pid/";
            File file = new File(parentPath);
            if (file.exists()) {
                String[] list = file.list();
                tmp = "MTK_Pvr       Pid          HW Accel";
                Log.d(TAG, tmp);
                if (isZflLauncherKilled)
                    sb.append(tmp).append("\r\n");
                for (int i = 0; i < list.length; ++i) {
                    String procPath = parentPath + "/" + list[i];
                    File process = new File(procPath);
                    if (process.isDirectory()) {
                        File gl = new File(procPath + "/process_stats");
                        if (gl.exists()) {
                            try{
                                br = new BufferedReader(new FileReader(gl));
                                String line;
                                long value = 0L;
                                while ((line = br.readLine()) != null) {
                                    if (line.startsWith("MemoryUsageKMalloc ")
                                            || line.startsWith("MemoryUsageAllocPTMemoryUMA ")
                                            || line.startsWith("MemoryUsageAllocGPUMemUMA ")) {
                                        value += Long.parseLong(line.substring(line.lastIndexOf(" ") +1));
                                    }
                                }
                                tmpKGSLAlloc += value;
                                tmp = String.format("MTK_Pvr  %1$8s  %2$16d", list[i], value);
                                Log.d(TAG, tmp);
                                if (isZflLauncherKilled)
                                    sb.append(tmp).append("\r\n");
                            } catch (Exception e){}
                            finally {
                                if (br != null) {
                                    try {
                                        br.close();
                                    } catch (IOException e) {
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } catch (Exception e) {}

        if (0 == sKGSLAlloc)
            sKGSLAlloc = tmpKGSLAlloc >> 10; // Byte to KB
        return sb.toString();
    }

    static String dumpMtkMaliGraphic(boolean isZflLauncherKilled) {
        StringBuilder sb = new StringBuilder();
        String tmp = "";
        BufferedReader br = null;
        long  tmpKGSLAlloc = 0;

        try {
            int mtkMaliDir = 0;
            String parentPath = "/d/mali/mem/";
            String memFileName = "";
            File file = new File(parentPath);

            if (file.exists() && file.isDirectory())
                mtkMaliDir = 1;
            else {
                parentPath = "/d/mali0/ctx/";
                file = new File(parentPath);
                if (file.exists() && file.isDirectory()) {
                    mtkMaliDir = 2;
                    memFileName = "/mem_profile";
                }
            }

            //if (file.exists() && file.isDirectory()) {
            if (mtkMaliDir > 0) {
                String[] list = file.list();
                tmp = "MTK_Mali       Tid          HW Accel  Process";
                Log.d(TAG, tmp);
                if (isZflLauncherKilled)
                    sb.append(tmp).append("\r\n");
                for (int i = 0; i < list.length; ++i) {
                    String procPath = parentPath + list[i] + memFileName;
                    File process = new File(procPath);
                    if (process.exists()) {
                        try {
                            br = new BufferedReader(new FileReader(process));
                            String line;
                            String processName = "";
                            long value = 0L;
                            boolean isFoundProcessName = false;
                            while ((line = br.readLine()) != null) {
                                if (!isFoundProcessName) {
                                    processName = line.substring(0, line.lastIndexOf(":"));
                                    isFoundProcessName = true;
                                }
                                if (line.startsWith("Total allocated memory:")) {
                                    value = Long.parseLong(line.substring(line.lastIndexOf(" ") + 1));
                                       tmpKGSLAlloc += value;
                                    break;
                                }
                            }
                            tmp = String.format("MTK_Mali  %1$8s  %2$16d  ", list[i], value) + processName;
                            Log.d(TAG, tmp);
                            if (isZflLauncherKilled)
                                sb.append(tmp).append("\r\n");
                        } catch(Exception e){ }
                        finally {
                            if (br != null) {
                                try {
                                    br.close();
                                } catch (IOException e) {
                                }
                            }
                        }
                    }
                }
            }
        } catch (Exception e) {}

        Log.d(TAG, "Count total Mali alloc:" +  (tmpKGSLAlloc >>10)  + " KB\r\n");
        if (0 == sKGSLAlloc)
            sKGSLAlloc = tmpKGSLAlloc  >> 10; // Byte to KB

        return sb.toString();
    }

    private static class ProcessStatus {
        private String mProcessName;
        private int mPid;

        public ProcessStatus(String process, int pid) {
            mProcessName = process;
            mPid = pid;
        }

        public String getProcessName() {
            return mProcessName;
        }

        public int getPid() {
            return mPid;
        }

        @Override
        public int hashCode() {
            return (mPid + mProcessName).hashCode();
        }

        @Override
        public boolean equals(Object others) {
            if (mProcessName.equals(((ProcessStatus) others).getProcessName())
                    && mPid == ((ProcessStatus) others).getPid())
                return true;
            return false;
        }
    }

    private static class IonInfo {
        String mClient;
        String mCreator;
        long mSize;

        public IonInfo(String client, String creator, long size) {
            mClient = client;
            mCreator = creator;
            mSize = size;
        }
    }

    private static class ProcessInfo{
        public int mPid;
        public int mAdj;
        public String mName;
        public String mAdjType;
        public String mServiceinfo;
        public String mProviderInfo;

        public ProcessInfo(int pid, int adj, String name, String type, String serviceInfo, String providerInfo){
            mPid = pid;
            mAdj = adj;
            mName = name;
            mAdjType = type;
            mServiceinfo = serviceInfo;
            mProviderInfo =   providerInfo;
        }
    }

    public static final native String dumpProcessStats(int pid, int adj, String name, String reason, String serviceInfo,
            String providerInfo, boolean isHomeKilled);

    public static final native String dumpHeader();

    /**
     * @hide
     */
    public static final native long getRegionMemory(int pid, String region);

    /**
     * Gets the all fields from smaps for a given process, in kilobytes.
     *
     * @param pid
     *            the process to the fields for * @param region the region name
     *            to the fields for
     * @return all fields for the given region of process in kilobytes, the
     *         array size is guaranteed to be fixed (size of 14) it will be null
     *         while insufficent memory and io error use PROC_MEM_XXX to
     *         retrieve desired data from result array
     * @hide
     */
    public static final native int[] getDetailRegionMemory(int pid, String region);
}
