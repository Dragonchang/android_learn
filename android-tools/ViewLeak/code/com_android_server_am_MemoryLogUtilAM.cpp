#define LOG_TAG "MemoryLogUtilAm"

#include <android_runtime/AndroidRuntime.h>
//#include "JNIHelp.h"
#include <nativehelper/JNIHelp.h>
#include <fcntl.h>
#include <memutils/MemoryMeasureHelper.h>

using namespace android;

#define PID_INFO_BUFFER_LENGTH                  1024
#define PPID_BUFFER_LENGTH                      24
#define PID_INFO_COLUMN_PID                     0
#define PID_INFO_COLUMN_COMMAND                 1
#define PID_INFO_COLUMN_STATE                   2
#define PID_INFO_COLUMN_PPID                    3

namespace android {
String8 convertJString(JNIEnv* env, jstring str) {
    String8 str8;
    if (str == NULL) {
        jniThrowNullPointerException(env, NULL);
        return str8;
    }

    const jchar* str16 = env->GetStringCritical(str, 0);
    if (str16) {
        str8 = String8(reinterpret_cast<const char16_t*>(str16),
                        env->GetStringLength(str));
        env->ReleaseStringCritical(str, str16);
    }
    return str8;
}

static char* get_process_info(int pid, char column, char* field, int length) {

    char pid_info[PID_INFO_BUFFER_LENGTH];
    int fd, read_byte, i = 0;
    char *field_to_copy_head, *field_to_copy_tail;

    *field = '\0';
    sprintf(pid_info, "/proc/%d/stat", pid);

    errno = 0;
    fd = open(pid_info, O_RDONLY);

    if (fd < 0) {
        ALOGW("Fail to open %s to read information. errno: %d, errmsg: %s", pid_info, errno, strerror(errno));
        errno = 0;
        return field;
    }

    errno = 0;
    read_byte = read(fd, pid_info, PID_INFO_BUFFER_LENGTH - 1);

    if (read_byte < 0) {
        ALOGE("Fail to read information of %d. errno: %d, errmsg: %s", pid, errno, strerror(errno));
        errno = 0;
        return field;
    }
    /*read sucess, check pid_info*/
    close(fd);
    pid_info[read_byte] = '\0';

    field_to_copy_head = pid_info;

    while (i < column && *field_to_copy_head != '\0') {
        if (*field_to_copy_head++ == ' ')
            i++;
    }

    /*Since the total number of fields is depedent on kernel version, we can't identify if it is a invalid
     arugment issue or other error. We just dump message here*/
    if ('\0' == *field_to_copy_head) {
        ALOGE("No content of the column \'%d\'of the process info is found", column);
        ALOGE("Pid: %d, Process info: %s, read byte: %d", pid, pid_info, read_byte);
        return field;
    }

    field_to_copy_tail = field_to_copy_head;
    while (*field_to_copy_tail != ' ' && *field_to_copy_tail != '\0') {
        field_to_copy_tail++;
    }

    if ('\0' == *field_to_copy_tail && '\n' == field_to_copy_tail[-1]) {
        field_to_copy_tail--;
    }

    *field_to_copy_tail = '\0';

    if (length < field_to_copy_tail - field_to_copy_head + 1) {
        ALOGE("Size of the buffer to get process info is too small !! (%d / %ld)", length, (long)(field_to_copy_tail - field_to_copy_head + 1));
        ALOGE("Process info to read - column: %d, info: %s", column, field_to_copy_head);
        return field;
    }

    /*We don't use strncpy because we have checked the buffer size*/
    strcpy(field, field_to_copy_head);

    return field;
}

static int getPpidByPid(int pid) {
    char field[PPID_BUFFER_LENGTH];
    get_process_info(pid, PID_INFO_COLUMN_PPID, field, PPID_BUFFER_LENGTH);

    return atoi(field);
}
static jstring android_server_am_MemoryLogUtilAm_dumpHeader(JNIEnv* env,
        jobject clazz) {
	String8 result;
    int platform = MemoryMeasureHelper::getPlatformType();
    if (platform == MemoryMeasureHelper::PLATFORM_NV) {
        result.append("     OOM_ADJ         ADJ_REASON    PID   PPID   PSS_Only        PSS       SWAP Process");
    } else if (platform == MemoryMeasureHelper::PLATFORM_QCT) {
        result.append("     OOM_ADJ         ADJ_REASON    PID   PPID   PSS_Only   KGSL_VSS    ION_VSS     Memory       SWAP Process");
    } else if (platform == MemoryMeasureHelper::PLATFORM_STE) {
        result.append("     OOM_ADJ         ADJ_REASON    PID   PPID   PSS_Only   Mali_Vss     Memory       SWAP Process");
    } else if (platform == MemoryMeasureHelper::PLATFORM_BRCM) {
        result.append("     OOM_ADJ         ADJ_REASON    PID   PPID   PSS_Only     Memory       SWAP Process");
    } else if (platform == MemoryMeasureHelper::PLATFORM_MTK) {
        result.append("     OOM_ADJ         ADJ_REASON    PID   PPID   PSS_Only   Mali_Vss    ION_VSS     Memory      PSWAP Process");
    } else {
        result.append("OOM_ADJ        ADJ_REASON    PID   PPID        PSS       SWAP Process");
    }
    ALOG(LOG_DEBUG, LOG_TAG, "%s", result.string());

    return env->NewStringUTF(result.string());
}

static jstring android_server_am_MemoryLogUtilAm_dumpProcessStats(JNIEnv* env,
        jobject clazz, jint pid, jint oom_adj, jstring name, jstring reason,
        jstring service, jstring provider, jboolean ishomekilled) {
    String8 home_restart;
    char filename[64];
    char line[256];
    char *process_name = NULL;

    int platform = MemoryMeasureHelper::getPlatformType();

//    snprintf(filename, sizeof(filename), "/proc/%d/oom_adj", pid);
//    FILE *file = fopen(filename, "r");
//    if (!file)
//        return NULL;
//    int oom_adj = -100;
//    if (fgets(line, sizeof(line), file))
//        sscanf(line, "%d", &oom_adj);
//    fclose(file);

    int oom_score_adj  = oom_adj;
    oom_adj = 16; /// unknown oom_adj
    if ( oom_score_adj  < 0 ) {
         if ( oom_score_adj  == -1000)
               oom_adj  = -17;
         else if ( oom_score_adj  == -900)
               oom_adj  = -16;
         else if ( oom_score_adj  == -800)
               oom_adj  = -12;
         else if ( oom_score_adj  == -700)
               oom_adj  = -11;
          else
               oom_adj  = -99;   ///invalid adj
     } else {
           if ( oom_score_adj  < 1000) {
               oom_adj  =  oom_score_adj / 100;
               if ( oom_score_adj > 900 && oom_score_adj <= 906 )
                    oom_adj = oom_adj + (oom_score_adj - 900);
           }
     }

    String8 name8;
    if (name != NULL) {
        name8 = convertJString(env, name);
        process_name = (char*) name8.string();
    } else {
        snprintf(filename, sizeof(filename), "/proc/%d/cmdline", pid);
        FILE *file = fopen(filename, "r");
        if (!file)
            return env->NewStringUTF(home_restart.string());
        fscanf(file, "%255s", line);
        if (strlen(line) != 0) {
            process_name = (char*) malloc(strlen(line) + 1);
            memset(process_name, 0, strlen(line) + 1);
            strcpy(process_name, line);
        } else {
            process_name = (char*) malloc(7 + 1); //unknown string length = 7
            memset(process_name, 0, 7 + 1);
            strcpy(process_name, "unknown");
        }
        fclose(file);
    }

    String8 extraInfo;
    String8 service8 = convertJString(env, service);
    String8 provider8 = convertJString(env, provider);

    if (!service8.isEmpty()) {
        extraInfo += " => SERVICE ";
        extraInfo += service8;
    }

    if (!provider8.isEmpty()) {
        extraInfo += " => PROVIDER ";
        extraInfo += provider8;
    }

    int ppid = getPpidByPid(pid);

    String8 reason8 = convertJString(env, reason);

    long pss = 0;
    long pss_only = 0;
    long swap = 0;
    long* memInfoArray = NULL;
    if (platform == MemoryMeasureHelper::PLATFORM_NV) {
        // will get total_pss,process_pss_only in memInfoArray
        if (MemoryMeasureHelper::getNVProcessMemoryForLowMemDetail(pid,
                &memInfoArray) == true) {
            pss = memInfoArray[0] << 10; // KBytes to Bytes, equals * 1024
            if (pss > 0){
                pss_only = memInfoArray[1] << 10;
                swap = memInfoArray[2] << 10;
                ALOG(LOG_DEBUG, LOG_TAG,
                    "%7d(%3d)  %16s %6d %6d %10ld %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss_only, pss, swap, process_name, extraInfo.string());
            }
        }
    } else if (platform == MemoryMeasureHelper::PLATFORM_QCT) {
        // will get total_pss,process_pss_only, kgsl_vss and iOn_pss in memInfoArray
        if (MemoryMeasureHelper::getQCTProcessMemoryForLowMemDetail(pid,
                &memInfoArray) == true) {
            pss = memInfoArray[0] << 10;
            if (pss > 0){
                pss_only = memInfoArray[1] << 10;
                long kgsl_vss = memInfoArray[2] << 10;
                long ion_vss = memInfoArray[3] << 10;
                swap = memInfoArray[4] << 10;
                //pss_only = pss - kgsl_pss - ion_pss;
                home_restart.append(String8::format("%7d(%3d)   %16s %6d %6d %10ld %10ld %10ld %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss_only, kgsl_vss, ion_vss,pss_only + kgsl_vss, swap,process_name, extraInfo.string()));
                ALOG(LOG_DEBUG, LOG_TAG, "%s", home_restart.string());
            }
        }
    } else if (platform == MemoryMeasureHelper::PLATFORM_STE) {
        // will get total_pss,process_pss_only in memInfoArray
        if (MemoryMeasureHelper::getSTEProcessMemoryForLowMemDetail(pid,
                &memInfoArray) == true) {
            pss = memInfoArray[0] << 10;
            if (pss > 0){
                pss_only = memInfoArray[1] << 10;
                long mali_vss = memInfoArray[2] << 10;
                swap = memInfoArray[3] << 10;
                ALOG(LOG_DEBUG, LOG_TAG,
                    "%7d(%3d)   %16s %6d %6d %10ld %10ld %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss_only, mali_vss, pss_only + mali_vss, swap, process_name, extraInfo.string());
            }
        }
    } else if (platform == MemoryMeasureHelper::PLATFORM_BRCM) {
        // will get total_pss,process_pss_only in memInfoArray
        if (MemoryMeasureHelper::getBRCMProcessMemoryForLowMemDetail(pid,
                &memInfoArray) == true) {
            pss = memInfoArray[0] << 10;
            if (pss > 0){
                pss_only = memInfoArray[1] << 10;
                swap = memInfoArray[2] << 10;
                ALOG(LOG_DEBUG, LOG_TAG,
                    "%7d(%3d)   %16s %6d %6d %10ld %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss_only, pss, swap, process_name, extraInfo.string());
            }
        }
    } else if (platform == MemoryMeasureHelper::PLATFORM_MTK) {
        // will get total_pss,process_pss_only, kgsl_vss and iOn_pss in memInfoArray
        if (MemoryMeasureHelper::getMTKProcessMemoryForLowMemDetail(pid,
                &memInfoArray) == true) {
            pss = memInfoArray[0] << 10;
            if (pss > 0){
                pss_only = memInfoArray[1] << 10;
                long ion_vss = memInfoArray[2] << 10;
                long pswap = memInfoArray[3] << 10;
                long mali_vss = memInfoArray[4]; /// GL mtrack /d/mali0/gpu_memory  or /d/mali0/ctx/<pid>_num/mem_profile for 6755  or /d/mali/ memory use for 6752, 6572, 6573
                //ALOG(LOG_DEBUG, LOG_TAG, "MTK 6755 mali: %ld",  mali_vss);
                //pss_only = pss - mali_pss - ion_pss;
                home_restart.append(String8::format("%7d(%3d)   %16s %6d %6d %10ld %10ld %10ld %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss_only, mali_vss, ion_vss, pss_only + mali_vss, pswap,process_name, extraInfo.string()));
                //home_restart.append(String8::format("%7d(%3d)   %16s %6d %6d %10ld %10ld %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss_only, ion_vss,pss_only, pswap,process_name, extraInfo.string()));
                ALOG(LOG_DEBUG, LOG_TAG, "%s", home_restart.string());
            }
        }
    } else {
        if (MemoryMeasureHelper::getProcessMemoryForLowMemDetail(pid,
                &memInfoArray) == true) {
            pss = memInfoArray[0] << 10;
            if (pss > 0){
                swap = memInfoArray[1] << 10;
                ALOG(LOG_DEBUG, LOG_TAG,
                    "%7d(%3d)   %16s %6d %6d %10ld %10ld %s %s", oom_score_adj, oom_adj, reason8.string(), pid, ppid, pss, swap, process_name, extraInfo.string());
            }
        }
    }
    free(memInfoArray);

    if (name == NULL)
        free(process_name);
    if (pss == 0)
        return NULL;

    return env->NewStringUTF(home_restart.string());
}

static jlong android_server_am_MemoryLogUtilAm_getRegionMemory(JNIEnv* env, jobject clazz, jint pid, jstring name)
{
    String8 name8 = convertJString(env, name);
    const char* nativeName = name8.string();

    jlong pss = (jlong)MemoryMeasureHelper::getRegionPssMemory(pid,nativeName);

    // Return the Pss value in bytes, not kilobytes
    return pss << 10;
}

static jintArray android_server_am_MemoryLogUtilAm_getDetailRegionMemory(JNIEnv* env, jobject clazz, jint pid, jstring name)
{
    const int num_of_fields = 14;//the number of fields dumped by /proc/xxx/smaps
    long *fields = NULL;

    jintArray newArray = env->NewIntArray(num_of_fields);
    if(newArray == NULL) return NULL;

    String8 name8 = convertJString(env, name);
    const char* nativeName = name8.string();
    ALOG(LOG_DEBUG, LOG_TAG,"Dump region %s of %d ", nativeName,pid);
    if(MemoryMeasureHelper::getDetailRegionMemory(pid,nativeName,&fields) == true) {
        env->SetIntArrayRegion(newArray, 0, num_of_fields, (jint*)fields);
        free(fields);
    }
    else {
        return NULL;
    }

    //env->SetIntArrayRegion(newArray, 0, num_of_fields, (jint*)fields);

    // Return the values in kilobytes
    return newArray;
}


static const JNINativeMethod methods[] =
        {
                { "dumpProcessStats",
                        "(IILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;",
                        (void*) android_server_am_MemoryLogUtilAm_dumpProcessStats },
                { "dumpHeader", "()Ljava/lang/String;",
                        (void*) android_server_am_MemoryLogUtilAm_dumpHeader },
                {"getRegionMemory",      "(ILjava/lang/String;)J",
                        (void*)android_server_am_MemoryLogUtilAm_getRegionMemory},
                        {"getDetailRegionMemory", "(ILjava/lang/String;)[I",
                        (void*)android_server_am_MemoryLogUtilAm_getDetailRegionMemory},
        };

const char* const kClassPathName = "com/android/server/am/MemoryLogUtilAm";

int register_android_server_am_MemoryLogUtilAm(JNIEnv* env) {
    jclass clazz;
    clazz = env->FindClass(kClassPathName);
    if (clazz == NULL) {
        ALOGE("[AutoProf] Can't find com/android/server/am/MemoryLogUtilAm");
        return -1;
    }
    return AndroidRuntime::registerNativeMethods(env, kClassPathName, methods,
            NELEM(methods));
}
}
;
// namespace android
