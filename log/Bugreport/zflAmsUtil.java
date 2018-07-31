
package com.android.server.am;
import static com.android.server.am.ActivityManagerService.DUMP_MEM_OOM_ADJ;
import static com.android.server.am.ActivityManagerService.DUMP_MEM_OOM_LABEL;
import static com.android.server.am.ActivityManagerService.DUMP_MEM_OOM_COMPACT_LABEL;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.concurrent.atomic.AtomicBoolean;

import android.os.Process;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.util.Slog;

import com.zfl.customization.zflCustomizationManager;
import com.zfl.customization.zflCustomizationReader;

final class zflAmsUtil {
    static final String TAG = "zflAmsUtil";
    static final int EVENT_CUSTOMIZE = 30750; // A free number refer to /etc/event-log-tags

    private static boolean initialized;
    static boolean hasVzwLogger;
    static boolean supportSRLTE;
    static boolean supportSyncCall;
    static boolean supportThermalControl;
    static boolean isStockUI;
    static boolean isGP;
    static boolean isSense;
    static String extraSenseVersion;
    static String[] firstBgProcWhiteList;


    static int HOME_APP_ADJ = 600;
    static int SERVICE_ADJ = 500;


    private static long totalMemory = -1;
    private static final int HIGH_MEM_MB_SIZE = 1300;
    private static android.util.ArraySet<String> zflImportantProcesses;


    static {
        init();
    }

    static void init() {
        try {
            if (initialized) {
                return;
            }
            initialized = true;
            loadCustomizationConfig();
            if (!isStockUI) {
                initzflAdj();
                initzflImportantProcesses();
            }
        } catch (Throwable t) {
            Slog.w(TAG, t);
        }
    }

    private static void loadCustomizationConfig() {
        zflCustomizationManager manager = zflCustomizationManager.getInstance();
        if (manager == null) {
            Slog.w(TAG, "Cannot get zflCustomizationManager instance");
            return;
        }

        zflCustomizationReader reader = manager.getCustomizationReader(
                "Android_App_Framework", zflCustomizationManager.READER_TYPE_XML, false);
        if (reader != null) {
            firstBgProcWhiteList = reader.readStringArray(
                    "AMS_feature_FirstBackgroundProcess_white_list", null);
            supportThermalControl = reader.readBoolean(
                    "AMS_feature_thermal_control", supportThermalControl);
        } else {
            Slog.w(TAG, "Cannot get Android_App_Framework customization reader");
        }

        reader = manager.getCustomizationReader("System",
                zflCustomizationManager.READER_TYPE_XML, false);
        if (reader != null) {
            extraSenseVersion = reader.readString("extra_sense_version", extraSenseVersion);
            if (extraSenseVersion == null) {
                isStockUI = true;
            } else if (extraSenseVersion.endsWith("gp")) {
                isGP = true;
            } else {
                isSense = true;
            }
            supportSRLTE = reader.readBoolean("support_SRLTE", supportSRLTE);
            supportSyncCall = reader.readBoolean("support_synccall", supportSyncCall);
        } else {
            Slog.w(TAG, "Cannot get System customization reader");
        }

        reader = manager.getCustomizationReader("VZWQualityLogger",
                zflCustomizationManager.READER_TYPE_XML, false);
        if (reader != null) {
            hasVzwLogger = reader.readBoolean("support_quality_logger", false);
        } else {
            Slog.w(TAG, "Cannot get VZWQualityLogger reader");
        }

        Slog.d(TAG, "loadCustomizationConfig completed");
    }

    //++Fine tune oom adj for zfl app
    static void initzflAdj() {
        // This function must be called earlier for ProcessList.
        HOME_APP_ADJ = 500;
        SERVICE_ADJ = 600;
    }

    static void swapAdjLabel() {
        // This function must be called after those arrays are initialized.
        if (isStockUI) {
            return;
        }
        try {
            if (DUMP_MEM_OOM_ADJ.length > 10) {
                int serviceAdj = DUMP_MEM_OOM_ADJ[9];
                DUMP_MEM_OOM_ADJ[9] = DUMP_MEM_OOM_ADJ[10];
                DUMP_MEM_OOM_ADJ[10] = serviceAdj;
            }
            if (DUMP_MEM_OOM_LABEL.length > 10) {
                String serviceLabel = DUMP_MEM_OOM_LABEL[9];
                DUMP_MEM_OOM_LABEL[9] = DUMP_MEM_OOM_LABEL[10];
                DUMP_MEM_OOM_LABEL[10] = serviceLabel;
            }
            if (DUMP_MEM_OOM_COMPACT_LABEL.length > 10) {
                String serviceLabel = DUMP_MEM_OOM_COMPACT_LABEL[9];
                DUMP_MEM_OOM_COMPACT_LABEL[9] = DUMP_MEM_OOM_COMPACT_LABEL[10];
                DUMP_MEM_OOM_COMPACT_LABEL[10] = serviceLabel;
            }
        } catch (Exception e) {
            Slog.w(TAG, e);
        }
    }
    //--Fine tune oom adj for zfl app

    //++Check dying process
    private static int sDyingProcMethod;
    private static final int BY_DYING_PROC = 1;
    private static final int BY_PROC_STAT = 2;
    private static final int DYING_PROCESSES_COLUMN_COUNT = 10 * 2;
    private static final String DYING_PROC = "/proc/dying_processes";
    private static int[] DYING_PROCESSES_FORMAT;
    private static int[] PROC_STAT_FORMAT;

    private synchronized static void initDyingMethod() {
        if (sDyingProcMethod != 0) {
            return;
        }
        if (new File(DYING_PROC).canRead()) {
            sDyingProcMethod = BY_DYING_PROC;
            DYING_PROCESSES_FORMAT = new int[DYING_PROCESSES_COLUMN_COUNT];
            for (int i = 0; i < DYING_PROCESSES_COLUMN_COUNT; i += 2) {
                DYING_PROCESSES_FORMAT[i] = android.os.Process.PROC_OUT_LONG | ':';
                DYING_PROCESSES_FORMAT[i + 1] = android.os.Process.PROC_OUT_LONG | '\n';
            }
        } else {
            sDyingProcMethod = BY_PROC_STAT;
            PROC_STAT_FORMAT = new int[31];
            java.util.Arrays.fill(PROC_STAT_FORMAT, Process.PROC_SPACE_TERM);
            PROC_STAT_FORMAT[1] |= Process.PROC_PARENS; // Process name enclosed in parentheses
            PROC_STAT_FORMAT[2] |= Process.PROC_OUT_STRING; // Process state (D/R/S/T/Z)
            PROC_STAT_FORMAT[30] |= Process.PROC_OUT_STRING; // Bit mask of pending signals
        }
        Slog.d(TAG, "Dying proc method=" + sDyingProcMethod);
    }

    private static boolean isDyingProcess(int pid) {
        boolean dying = false;
        final String[] outStats = new String[2];
        final String stat = "/proc/" + pid + "/stat";
        if (Process.readProcFile(stat, PROC_STAT_FORMAT, outStats, null, null)) {
            String state = outStats[0];
            String pendingSignal = outStats[1];
            if ("Z".equals(state)) {
                Slog.d(TAG, pid + " is zombie state");
                dying = true;
            } else {
                int ps = Integer.parseInt(pendingSignal);
                if ((ps & (1 << 8)) != 0) {
                    Slog.d(TAG, pid + " has pending signal 9");
                    dying = true;
                }
            }
        } else {
            Slog.d(TAG, "Failed to read " + stat);
        }
        return dying;
    }

    static boolean isDyingProcess(int pid, long lastStartTime) {
        try {
            if (sDyingProcMethod == 0) {
                initDyingMethod();
            }
            if (sDyingProcMethod == BY_PROC_STAT) {
                return isDyingProcess(pid);
            }
            final long[] outLongs = new long[DYING_PROCESSES_COLUMN_COUNT];
            if (!Process.readProcFile(DYING_PROC, DYING_PROCESSES_FORMAT, null, outLongs, null)) {
                long[] outVal = new long[1];
                String[] pattern = new String[] { pid + ":" };
                Process.readProcLines(DYING_PROC, pattern, outVal);
                outLongs[0] = outVal[0];
            }
            if (outLongs[0] == 0) {
                Slog.d(TAG, DYING_PROC + " no record");
                return false;
            }
            for (int i = 0; i < DYING_PROCESSES_COLUMN_COUNT; i += 2) {
                long dpid = outLongs[i];
                if (pid == dpid) {
                    long jiffyFromSignalTime = outLongs[i + 1];
                    long now = SystemClock.uptimeMillis();
                    long deadTime = now - 10 * jiffyFromSignalTime; // unit of jiffy is 1/100s
                    Slog.d(TAG, "isDyingProcess: deadTime="
                            + deadTime + " lastStartTime=" + lastStartTime);
                    if (deadTime > lastStartTime) {
                        Slog.d(TAG, "isDyingProcess: dying proc="
                                + dpid + ":" + jiffyFromSignalTime);
                        return true;
                    } else {
                        Slog.d(TAG, "isDyingProcess: dead past proc="
                                + dpid + ":" + jiffyFromSignalTime);
                    }
                }
            }
        } catch (Exception e) {
            Slog.w(TAG, e);
        }
        return false;
    }
    //--Check dying process

    //++Enhance ANR information
    static final String LOG_HISTORY_DIR = "/data/zflloghistory";
    static final String ANR_HISTORY_DIR = LOG_HISTORY_DIR + "/anr_history";
    static final String ANR_HISTORY_FILE = ANR_HISTORY_DIR + "/anr_history.txt";

    static void setPathPermissions(String path, int mode) {
        android.os.FileUtils.setPermissions(path, mode, -1, -1);
    }

    static void writeAnrHistory(CharSequence info, File traces, boolean debug) {
        int historyLimit = SystemProperties.getInt("persist.sys.anr_history_count", 0);
        final File historyFile;
        if (historyLimit > 0) {
            historyLimit = Math.min(historyLimit, 30);
            Slog.i(TAG, "ANR history separated limit " + historyLimit);
            File historyDir = new File(ANR_HISTORY_DIR);
            android.os.FileUtils.deleteOlderFiles(historyDir, historyLimit - 1, 0);
            historyFile = new File(historyDir,
                    "ah_" + new SimpleDateFormat("yyyy_MMdd_HHmmss").format(new Date()) + ".txt");
        } else {
            if (!debug) {
                return;
            }
            historyFile = new File(ANR_HISTORY_FILE);
        }
        Slog.i(TAG, "Writing traces to ANR history");
        File dir = new File(ANR_HISTORY_DIR);
        if (!dir.exists()) {
            boolean success = dir.mkdirs();
            Slog.i(TAG, "Create dir " + ANR_HISTORY_DIR + ":" + success);
        }
        setPathPermissions(LOG_HISTORY_DIR, 0777);
        setPathPermissions(ANR_HISTORY_DIR, 0777);
        try (FileOutputStream output = new FileOutputStream(historyFile, true)) {
            if (info != null) {
                output.write(info.toString().getBytes());
            }
            if (traces != null) {
                try (FileInputStream input = new FileInputStream(traces)) {
                    input.getChannel().transferTo(0, traces.length(), output.getChannel());
                }
            }
            setPathPermissions(historyFile.getAbsolutePath(), 0644);
        } catch (Exception e) {
            Slog.w(TAG, "Unable to write ANR history", e);
        }
    }

    static void printLastLineIfTruncated(int level, String tag, String info) {
        final int infoLength = info.length();
        if (infoLength > 4000) {
            int lastLineStart = info.lastIndexOf("\n", infoLength - 16) + 1;
            if (lastLineStart > 0) {
                Slog.println(level, tag, info.substring(lastLineStart, infoLength));
            }
        }
    }

    static String printCpuInfo(com.android.internal.os.ProcessCpuTracker cpuTracker,
            String tag, StringBuilder anrInfo, long anrTime, boolean current) {
        String currentLoad;
        String cpuInfo;
        synchronized (cpuTracker) {
            currentLoad = cpuTracker.printCurrentLoad();
            cpuInfo = cpuTracker.printCurrentState(anrTime);
        }
        anrInfo.append(currentLoad);
        anrInfo.append(cpuInfo);
        String info;
        if (current) {
            info = anrInfo.toString();
        } else {
            info = cpuInfo;
            Slog.e(tag, currentLoad);
        }
        Slog.e(tag, info);
        printLastLineIfTruncated(android.util.Log.ERROR, tag, info);
        return cpuInfo;
    }

    static File dumpBinderTransactions() {
        String tracesPath = SystemProperties.get("dalvik.vm.stack-trace-file", null);
        if (tracesPath == null || tracesPath.length() == 0) {
            return null;
        }

        File tracesDir = new File(tracesPath).getParentFile();
        if (!tracesDir.exists()) {
            tracesDir.mkdirs();
            setPathPermissions(tracesDir.getPath(), 0777);
        }

        File binderLog = new File(tracesDir, "binder.txt");
        try {
            if (binderLog.exists()) {
                binderLog.delete();
            }
            binderLog.createNewFile();
            dumpBinderTransactions(binderLog);
        } catch (Exception e) {
            Slog.w(TAG, e);
        }
        return binderLog;
    }

    static File dumpStackTraces(boolean clearTraces, ArrayList<Integer> firstPids,
            com.android.internal.os.ProcessCpuTracker processCpuTracker,
            android.util.SparseArray<Boolean> lastPids, ArrayList<Integer> nativePids) {

        File binderLog = dumpBinderTransactions();
        File tracesFile = ActivityManagerService.dumpStackTraces(
                clearTraces, firstPids, processCpuTracker, lastPids, nativePids);

        if (binderLog != null && tracesFile != null) {
            try (FileOutputStream output = new FileOutputStream(tracesFile, true)) {
                try (FileInputStream input = new FileInputStream(binderLog)) {
                    input.getChannel().transferTo(0, binderLog.length(), output.getChannel());
                }
                binderLog.delete();
            } catch (Exception e) {
                Slog.w(TAG, e);
            }
        }
        return tracesFile;
    }

    static final char SYSTRACE_REQUEST_DUMP = '1';
    static final char SYSTRACE_REQUEST_FORCE_DUMP = '3';
    static final char SYSTRACE_REQUEST_PAUSE = '4';

    static final int SYSTRACE_STATUS_OFF = 0;
    static final int SYSTRACE_STATUS_ON = 1;
    static final int SYSTRACE_STATUS_UNKNOWN = 2;

    static void writeSystraceTrigger(char action) {
        com.android.server.Watchdog.writeTraceTrigger(TAG, action);
    }

    static int pauseSystraceIfNeeded() {
        if (!SystemProperties.getBoolean("ro.framework.tracepoint", true)) {
            Slog.d(TAG, "Skip systrace dump, ro.framework.tracepoint is false");
            return SYSTRACE_STATUS_OFF;
        }

        if (SystemProperties.getInt("persist.mtk.aee.mode", 4) != 4) {
            Slog.d(TAG, "Skip systrace dump, aee mode has enabled");
            return SYSTRACE_STATUS_OFF;
        }

        boolean traceLogEnabled = false;
        final String path = "/sys/kernel/debug/tracing/tracing_on";
        try {
            if ("1".equals(libcore.io.IoUtils.readFileAsString(path).trim())) {
                traceLogEnabled = true;
            }
        } catch (Exception e) {
            Slog.w(TAG, "Read " + path + " failed: " + e);
            return SYSTRACE_STATUS_UNKNOWN;
        }

        if (!traceLogEnabled) {
            Slog.d(TAG, "Skip systrace dump, trace log is originally disabled");
            return SYSTRACE_STATUS_OFF;
        }

        // Disable trace data in order to keep the crime scene.
        writeSystraceTrigger(SYSTRACE_REQUEST_PAUSE);
        return SYSTRACE_STATUS_ON;
    }

    static class AnrAgent {
        private static class AnrRecord {
            final ProcessRecord app;
            final ActivityRecord activity;
            final ActivityRecord parent;
            final boolean aboveSystem;
            final String annotation;
            final long timestamp;

            AnrRecord(ProcessRecord app, ActivityRecord activity,
                    ActivityRecord parent, boolean aboveSystem, String annotation) {
                this.app = app;
                this.activity = activity;
                this.parent = parent;
                this.aboveSystem = aboveSystem;
                this.annotation = annotation;
                timestamp = SystemClock.uptimeMillis();
            }
        }

        AnrAgent(AppErrors appErrors, boolean debug) {
            mAppErrors = appErrors;
            DEBUG = debug;
        }

        private final boolean DEBUG;
        private final AppErrors mAppErrors;
        private final ArrayList<AnrRecord> mAnrRecords = new ArrayList<>();
        private final AtomicBoolean mRunning = new AtomicBoolean(false);

        void appNotResponding(ProcessRecord app, ActivityRecord activity,
                ActivityRecord parent, boolean aboveSystem, String annotation) {
            if (DEBUG) Slog.d(TAG, "Append anr " + app);
            synchronized (mAnrRecords) {
                mAnrRecords.add(new AnrRecord(app, activity, parent, aboveSystem, annotation));
            }
            startIfNeeded();
        }

        private void startIfNeeded() {
            try {
                if (mRunning.compareAndSet(false, true)) {
                    new AnrDumpThread().start();
                }
            } catch (Throwable t) {
                Slog.w(TAG, t);
            }
        }

        private AnrRecord next() {
            synchronized (mAnrRecords) {
                return mAnrRecords.isEmpty() ? null : mAnrRecords.remove(0);
            }
        }

        private class AnrDumpThread extends Thread {
            AnrDumpThread() {
                super("AnrDumpThread");
            }

            void handleAnr() {
                AnrRecord r;
                while ((r = next()) != null) {
                    final long startTime = SystemClock.uptimeMillis();
                    final long delay = startTime - r.timestamp;
                    if (DEBUG && delay > 5000) {
                        Slog.d(TAG, "Late anr " + delay + "ms " + r.app);
                    }
                    if (delay > 3 * 60 * 1000) {
                        Slog.d(TAG, "Skip dump " + r.app + " expired " + delay + "ms");
                        continue;
                    }
                    final int status = DEBUG ? pauseSystraceIfNeeded() : 0;
                    mAppErrors.appNotRespondingImpl(
                            r.app, r.activity, r.parent, r.aboveSystem, r.annotation);
                    if (status != SYSTRACE_STATUS_OFF) {
                        writeSystraceTrigger(status == SYSTRACE_STATUS_ON
                                ? SYSTRACE_REQUEST_FORCE_DUMP : SYSTRACE_REQUEST_DUMP);
                    }
                    Slog.w(TAG, "Complete ANR of " + r.app.processName + ", "
                            + (SystemClock.uptimeMillis() - startTime) + "ms");
                }
            }

            @Override
            public void run() {
                try {
                    handleAnr();
                } catch (Throwable t) {
                    Slog.w(TAG, t);
                }
                mRunning.set(false);
                final boolean remaining;
                synchronized (mAnrRecords) {
                    remaining = !mAnrRecords.isEmpty();
                }
                if (remaining) {
                    Slog.d(TAG, "AdThr is done but still remains record");
                    startIfNeeded();
                }
            }
        }
    }
    //--[Debug]Enhance ANR information

    //++[Debug]Enhance watchdog timeout information
    static void dumpAllProcessStack(String extraInfo,
            ArrayList<ProcessRecord> lruProcesses, final int MY_PID) {
        android.util.EventLog.writeEvent(EVENT_CUSTOMIZE, "dumpAllProcessStack, " + extraInfo);
        Slog.v(TAG, "Dumping all processes");
        ArrayList<Integer> firstPids = new ArrayList<>(64);
        firstPids.add(MY_PID);
        for (int i = lruProcesses.size() - 1; i >= 0; i--) {
            final ProcessRecord r;
            try { // No lock service here because it may be held.
                r = lruProcesses.get(i);
            } catch (Exception ignored) {
                continue;
            }
            if (r != null && r.thread != null) {
                int pid = r.pid;
                if (pid > 0 && pid != MY_PID) {
                    firstPids.add(pid);
                }
            }
        }
        File tracesFile = dumpStackTraces(false, firstPids, null, null, null);
        final String info = "======================\nDump All Process Stack";
        writeAnrHistory(extraInfo == null ? info : (info + "\n" + extraInfo), tracesFile, true);
    }
    //--[Debug]Enhance watchdog timeout information

    //++[Debug]Add BinderDumper to track ANR and watchdog
    private static final String TAG_BD = "BinderDumper";

    static void dumpBinderTransactions(File file) {
        Slog.i(TAG_BD, "dumpBinderTransactions begin");
        final DumpThread thread = new DumpThread(file);
        thread.start();

        final long timeout = 5000;
        final long start = SystemClock.uptimeMillis();
        synchronized (thread) {
            if (!thread.mFinish) {
                try {
                    thread.wait(timeout);
                } catch (InterruptedException e) {
                }
                thread.mFinish = true;
            }
        }
        if (SystemClock.uptimeMillis() - start >= timeout) {
            Slog.w(TAG_BD, "Dump 5s timeout");
        }
        Slog.i(TAG_BD, "dumpBinderTransactions end");
    }

    private static class DumpThread extends Thread {
        final File mFile;
        volatile boolean mFinish;

        DumpThread(File file) {
            super(TAG_BD);
            mFile = file;
        }

        void dump() throws java.io.IOException {
            final SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
            final String[] binderInfoPath = {
                    "/sys/kernel/debug/binder/failed_transaction_log",
                    "/sys/kernel/debug/binder/transaction_log",
                    "/sys/kernel/debug/binder/transactions",
                    "/sys/kernel/debug/binder/stats",
                    "/sys/kernel/debug/binder/state"
            };
            String info = "[DUMP BINDER INFO START at "
                    + formatter.format(new Date()) + "]\n";
            try (FileOutputStream output = new FileOutputStream(mFile, true)) {
                output.write(info.getBytes());
                byte[] buf = new byte[8192];
                int bytesRead;
                for (String path : binderInfoPath) {
                    if (mFinish) {
                        output.write("\nAbort\n".getBytes());
                        break;
                    }
                    Slog.v(TAG_BD, "Dump " + path);
                    try (FileInputStream input = new FileInputStream(path)) {
                        while ((bytesRead = input.read(buf)) != -1) {
                            output.write(buf, 0, bytesRead);
                            if (mFinish) {
                                break;
                            }
                        }
                    }
                }
                info = "[DUMP BINDER INFO END at " + formatter.format(new Date()) + "]\n";
                output.write(info.getBytes());
            }
        }

        @Override
        public void run() {
            try {
                dump();
            } catch (Throwable e) {
                Slog.w(TAG_BD, e);
            }
            synchronized (this) {
                mFinish = true;
                notifyAll();
            }
        }
    }
    //--[Debug]Add BinderDumper to track ANR and watchdog

    //++Process importance refine
    static void initzflImportantProcesses() {
        zflImportantProcesses = new android.util.ArraySet<>();
        if (firstBgProcWhiteList != null) {
            for (String procName : firstBgProcWhiteList) {
                zflImportantProcesses.add(procName);
            }
        }
    }

    static boolean iszflImportantProcesses(String name) {
        final boolean inWhitelist = zflImportantProcesses != null && zflImportantProcesses.contains(name);
        // Set zfl camera as zflImportantProcesses to prevent OOP if available memory >= 1.3G
        // requested by U31 Azar Huang
        final boolean iszflCamera = isHighMemDevice() && name != null && name.startsWith("com.zfl.camera");
        return inWhitelist || iszflCamera;
    }

    static long getTotalRamSize() {
        if (totalMemory <= -1) {
            totalMemory = Process.getTotalMemory() / 1048576;
            Slog.i(TAG, "TotalMemory: " + totalMemory);
        }
        return totalMemory;
    }

    static boolean isHighMemDevice() {
        return getTotalRamSize() >= HIGH_MEM_MB_SIZE;
    }
    //--Process importance refine

    //++Generic log
    /**
      * Show caller pid, class name and line number excepts AMS and common framework related files
      * The class name information shows when caller is system_server itself
      * @param prefix prefix to put in front
      * @return a string describing the pid (and the caller class name)
      */
    static String getCaller() {
        return getCaller(android.os.Binder.getCallingPid());
    }

    static String getCaller(int callingPid) {
        if (callingPid != ActivityManagerService.MY_PID)
            return "";

        StringBuilder sb = new StringBuilder();
        final StackTraceElement[] callStack = Thread.currentThread().getStackTrace();
        for (int i = 4; i < callStack.length; i++) { // callStack[4] is the caller of the method that called getCallers()
            StackTraceElement caller = callStack[i];
            String className = caller.getClassName();
            if (!className.startsWith("com.android.server.am")
                    && !className.startsWith("android.app") && !className.startsWith("android.content")
                    && !className.startsWith("java")) {
                sb.append(" " + className + ":" + caller.getLineNumber());
                break;
            }
        }
        return sb.toString();
    }
    //--Generic log
}
//--Add zflAmsUtil
