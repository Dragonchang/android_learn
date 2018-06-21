#define	LOG_TAG	"STT:lib"

#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <errno.h>
#include <jni.h>
#include <cutils/properties.h>
#include <cutils/log.h>

#include <linux/capability.h>
//#include <sys/capability.h>
#include <sys/prctl.h>

#include "libcommon.h"
#include "lib.h"
#include "jniutil.h"

//-------------------------------------------------------
//hdmi+

#include <sys/ioctl.h>

#define HDMI_IOCTL_MAGIC 'h'
#define HDMI_GET_CONNECTION _IOR(HDMI_IOCTL_MAGIC, 1, int)
//hdmi-
//-------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif
//-------------------------------------------------------
//hdmi+

static jint getHdmiCableState(JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
    //+ HDMI_GET_CONN_STATE
    int hdmifd = open("/dev/hdmi", O_RDWR);
    int hdmiConnVal = -1;
    int retry = 0;
    int retrunValue = -1;

    LOGE("%s: hdmifd = [%d]\n", __FUNCTION__, hdmifd);

    while (ioctl(hdmifd, HDMI_GET_CONNECTION, &hdmiConnVal) < 0) {
        if(retry < 3){
            usleep(500);
        	    retry++;
                LOGE("%s: HDMI_GET_CONNECTION failed, retry = [%d]\n", __FUNCTION__, retry);
            continue;
        }
            return -1;
    }
    LOGE("%s: HDMI_GET_CONNECTION success, hdmiConnVal = [%d]\n", __FUNCTION__, hdmiConnVal);

    retrunValue = hdmiConnVal; // HDMI_GET_CONN_STATE = hdmiConnVal

    close(hdmifd);

    return retrunValue;
}

//hdmi-
//-------------------------------------------------------

#include "headers/conf.h"
#include "headers/fio.h"
#include "headers/dir.h"
#include "headers/cpu.h"
#include "headers/board.h"
#include "headers/battery.h"
#include "headers/time.h"
#include "headers/gps_test.h"
#include "headers/partition.h"
#include "headers/input.h"
#include "headers/hw_libs.h"
#include "src/expire.c"

/* native methods */

static jlong fileOpen (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring filename)
{
	char *name;
	long addr = 0;
	name = jstring2cs (env, filename);
	if (name)
	{
		addr = (long) file_open (name);
		free (name);
	}
	return addr;
}

static jlong fileOpenInputEvent (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring key)
{
	char buffer [64];
	char *pkey;
	long addr = 0;
	pkey = jstring2cs (env, key);
	if (pkey)
	{
		if (find_input_device (pkey, buffer, sizeof (buffer)) == 0)
		{
			addr = (long) file_open (buffer);
		}
		free (pkey);
	}
	return addr;
}

static void fileClose (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	if (p) file_close (p);
}

static jstring fileRead (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jint timeout_ms)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	char buffer [512];
	memset (buffer, 0, sizeof (buffer));
	if (p) file_read (p, buffer, sizeof (buffer), timeout_ms);
	LOGV ("fileRead [%ld][%s]", (long) fd, buffer);
	return cs2jstring (env, buffer);
}

static jstring fileReadRaw (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jint timeout_ms)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	char buffer [512];
	int length = 0;
	memset (buffer, 0, sizeof (buffer));
	if (p) length = file_read (p, buffer, sizeof (buffer), timeout_ms);
	LOGV ("fileReadRaw [%ld][%s]", (long) fd, buffer);
	if (length <= 0) return NULL;
	return cs2jstringraw (env, buffer, length);
}

static jint fileReadInputEvent (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jint timeout_ms, jobject ievt)
{
	struct input_event ie;
	FILEIO *p = (FILEIO *) ((long) fd);
	memset (& ie, 0, sizeof (ie));
	if (p && (file_read (p, (char *) & ie, sizeof (ie), timeout_ms) > 0))
	{
		LOGV ("fileReadInputEvent [%ld], type = %d, code=%d, value=%d", (long) fd, ie.type, ie.code, ie.value);
		setIntField (env, ievt, "mType",	(jint) ie.type);
		setIntField (env, ievt, "mCode",	(jint) ie.code);
		setIntField (env, ievt, "mValue",	(jint) ie.value);
		return 0;
	}
	return -1;
}

static jint fileWrite (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jstring data)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	char *pdata = jstring2cs (env, data);
	int ret = -1;
	LOGV ("fileWrite [%s]", pdata);
	if (p && pdata) ret = file_write (p, pdata, strlen (pdata));
	if (pdata) free (pdata);
	return ret;
}

static jint fileWriteRaw (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jstring data, jlong length)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	char *pdata = jstring2csraw (env, data);
	int ret = -1;
	LOGV ("fileWriteRaw [%s]", pdata);
	if (p && pdata) ret = file_write (p, pdata, length);
	if (pdata) free (pdata);
	return ret;
}

static void fileInterrupt (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	if (p) file_interrupt (p);
}

static int ioctlJNI (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd, jint value1, jint value2)
{
	FILEIO *p = (FILEIO *) ((long) fd);
	int ret = -1;
	if (p) ret = ioctl (p->fd, value1, value2);
	LOGV ("ioctlJNI [%d][%d][%d]", ret, value1, value2);

	if( p && ret < 0 && errno == 14)	// bad address
	{
		ret = ioctl (p->fd, value1, &value2);
		LOGV ("bad-address, try again! ioctlJNI [%d][%d][addr:%p]", ret, value1, (void*)&value2);

		if( p && ret < 0 && errno == 22)	// Invalid value
		{
			struct diag_logging_mode_param_t_pd params_pd;
			params_pd.req_mode  = value2;
			params_pd.mode_param = DIAG_MD_NORMAL;
			params_pd.pd_mask = 0;
			params_pd.peripheral_mask = 2;
			ret =  ioctl(p->fd, value1, &params_pd, sizeof(struct diag_logging_mode_param_t_pd), NULL, 0, NULL, NULL);
			if (ret == 0)
			{
				LOGV ("Send ioctl new struct pass, ioctlJNI [%d][%d][%d]", ret, value1, value2);
			}
			else if (ret == -1)
			{
				LOGV ("PD mask struct return -1, try to using old format.[%d][%d][%d][addr:%p]",ret, value1, value2, (void*)&params_pd);
				struct diag_logging_mode_param_t params;
				params.req_mode  = value2;
				params.mode_param = DIAG_MD_NORMAL;
				params.peripheral_mask = 0;
				ret =  ioctl(p->fd, value1, &params, sizeof(struct diag_logging_mode_param_t), NULL, 0, NULL, NULL);
				LOGV ("Old function struct return results =[%d][%d][%d][addr:%p]",ret, value1, value2, (void*)&params);
			}
			else
			{
				LOGV ("Stuct return unknown state");
				LOGV ("Invalid value, try again! ioctlJNI [%d][%d][%d][addr:%p]", ret, value1, value2, (void*)&value2);
			}
		}
	}

	return ret;
}

static jfloat nativeGetCpuUsage (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jint UNUSED_VAR (reserved))
{
	return cpu_usage (NULL);
}

static jint getNativeTime (JNIEnv *env, jclass UNUSED_VAR (clazz), jobject ti)
{
	int yr, mh, dy, hr, me, sd, wd;

	if (get_native_time (& yr, & mh, & dy, & hr, & me, & sd, & wd) < 0)
		return -1;

	setIntField (env, ti, "mYear",		(jint) yr);
	setIntField (env, ti, "mMonth",		(jint) mh);
	setIntField (env, ti, "mDay",		(jint) dy);
	setIntField (env, ti, "mHour",		(jint) hr);
	setIntField (env, ti, "mMinute",	(jint) me);
	setIntField (env, ti, "mSecond",	(jint) sd);
	setIntField (env, ti, "mWeekDay",	(jint) wd);

	return 1;
}

static jstring getMbVersion (JNIEnv *env, jclass UNUSED_VAR (clazz), jint UNUSED_VAR (reserved))
{
	extern int get_mb_version (char *version, int len);

	char buf [128];

	if (get_mb_version (buf, sizeof (buf)) < 0)
	{
		strcpy (buf, "Cannot open MFG!");
	}

	return cs2jstring (env, buf);
}

static jint readMiscConfig (JNIEnv *env, jclass UNUSED_VAR (clazz), jintArray array)
{
	PARTITION pn;

	char data [MISC_DEBUGFLAGS_MAX_COUNT * sizeof (int)];
	long data_length;
	int ret = -1;

	if (array == NULL)
		return MISC_DEBUGFLAGS_COUNT;

	if (partition_open (& pn, "misc") < 0)
		goto end;

	data_length = partition_misc_debugflags_read (& pn, NULL);

	if ((data_length <= 0) || (data_length > (long) sizeof (data)))
	{
		LOGE ("readMiscConfig(): invalid data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
		goto end;
	}

	if (data_length != (long) (MISC_DEBUGFLAGS_COUNT * sizeof (int)))
	{
		/*
		 * just warning, keep going
		 */
		LOGW ("readMiscConfig(): unexpected data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
	}

	memset (data, 0, sizeof (data));

	if (partition_misc_debugflags_read (& pn, (int *) data) < 0)
		goto end;

	(*env)->SetIntArrayRegion (env, array, 0, MISC_DEBUGFLAGS_COUNT, (jint *) data);

	ret = 0;

end:;
	partition_close (& pn);
	return ret;
}

static jint writeMiscConfig (JNIEnv *env, jclass UNUSED_VAR (clazz), jintArray array)
{
	PARTITION pn;

	char data [MISC_DEBUGFLAGS_MAX_COUNT * sizeof (int)];
	long data_length;
	int *arraydata;
	int ret = -1;

	if (array == NULL)
		return MISC_DEBUGFLAGS_COUNT;

	if (partition_open (& pn, "misc") < 0)
		goto end;

	data_length = partition_misc_debugflags_read (& pn, NULL);

	if ((data_length <= 0) || (data_length > (long) sizeof (data)))
	{
		LOGE ("writeMiscConfig(): invalid data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
		goto end;
	}

	if (data_length != (long) (MISC_DEBUGFLAGS_COUNT * sizeof (int)))
	{
		/*
		 * just warning, keep going
		 */
		LOGW ("writeMiscConfig(): unexpected data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
	}

	memset (data, 0, sizeof (data));

	if (partition_misc_debugflags_read (& pn, (int *) data) < 0)
		goto end;

	arraydata = (*env)->GetIntArrayElements (env, array, NULL);

	memcpy (data, arraydata, MISC_DEBUGFLAGS_COUNT * sizeof (int));

	(*env)->ReleaseIntArrayElements (env, array, arraydata, 0);

	if (partition_misc_debugflags_write (& pn, (int *) data) < 0)
		goto end;

	ret = 0;

end:;
	partition_close (& pn);
	return ret;
}

static jint clearMiscConfig (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	PARTITION pn;

	char data [MISC_DEBUGFLAGS_MAX_COUNT * sizeof (int)];
	long data_length;
	int ret = -1;

	if (partition_open (& pn, "misc") < 0)
		goto end;

	memset (data, 0, sizeof(data));

	data_length = partition_misc_debugflags_read (& pn, NULL);

	if ((data_length <= 0) || (data_length > (long) sizeof (data)))
	{
		LOGE ("clearMiscConfig(): invalid data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
		goto end;
	}

	if (data_length != (long) (MISC_DEBUGFLAGS_COUNT * sizeof (int)))
	{
		/*
		 * just warning, keep going
		 */
		LOGW ("clearMiscConfig(): unexpected data length %ld! (expected %ld)\n", data_length, MISC_DEBUGFLAGS_COUNT * (long) sizeof (int));
	}

	if (partition_misc_debugflags_read (& pn, (int *) data) < 0)
		goto end;

	memset (data, 0, sizeof (data));

	if (partition_misc_debugflags_write (& pn, (int *) data) < 0)
		goto end;

	ret = 0;

end:;
	partition_close (& pn);
	return ret;
}

static jint readMfgOffsetInt (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), int offset)
{
	PARTITION pn;

	int ret = 0;

	if (partition_open (& pn, "mfg") < 0)
		goto end;

	if (partition_read (& pn, offset, sizeof (int), & ret) < 0)
		goto end;

end:;
	partition_close (& pn);
	return ret;
}

static jint writeMfgOffsetInt (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jint offset, jint value)
{
	PARTITION pn;

	int ret = -1;

	if (partition_open (& pn, "mfg") < 0)
		goto end;

	if (partition_write (& pn, offset, sizeof (int), & value) < 0)
		goto end;

	ret = 0;

end:;
	partition_close (& pn);
	return ret;
}

static jboolean isAutoStartBitSet (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	return partition_is_autostart_bit_set ();
}

static jint getFileStat (JNIEnv *env, jclass UNUSED_VAR (clazz), jobject st)
{
	struct stat s;
	char path [256] = "";
	char buf [256] = "";

	getStringField (env, st, "mPath", path, sizeof (path));

	if (readlink (path, buf, sizeof (buf)) < 0)
	{
		setStringField (env, st, "mLinkPath", NULL);
	}
	else
	{
		buf [sizeof (buf) - 1] = 0;

		setStringField (env, st, "mLinkPath", buf);
	}

	if ((path [0] == 0) || (stat (path, & s) != 0))
	{
		if (path [0]) LOGE ("stat [%s]: %s", path, strerror (errno));
		return -1;
	}

	setIntField (env, st, "mMode",	(jint) s.st_mode);
	setIntField (env, st, "mUid",	(jint) s.st_uid);
	setIntField (env, st, "mGid",	(jint) s.st_gid);
	setIntField (env, st, "mSize",	(jint) s.st_size);

	return 0;
}

static jint enableSensor (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jstring UNUSED_VAR (keyword), jboolean UNUSED_VAR (enable))
{
#if 0
	char *key = jstring2cs (env, keyword);
	int ret = -1;

	if (key)
	{
		ret = hw_sensor_enable (key, enable ? 1 : 0);
		free (key);
	}

	return ret;
#else
	return -1;
#endif
}

static jlong getImsiTstmpNative (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring imsi)
{
	PARTITION pn;
	IMSI_Data imsi_data, *ptr;

	char data [sizeof (IMSI_Data) * IMSI_ENTRY_COUNT];
	int i ,len;
	time_t ret_tamp = 0;
	const int imsi_digit = sizeof (imsi_data.imsi);

	if (partition_open (& pn, "misc") < 0)
		goto end;

	if (partition_misc_usim_read (& pn, data, sizeof (data)) < 0)
		goto end;

	ptr = (IMSI_Data *) data;

	len = (*env)->GetStringLength (env, imsi);
	len = (len > imsi_digit) ? imsi_digit : len;

	(*env)->GetStringUTFRegion (env, imsi, 0, len, imsi_data.imsi);

	for (i = 0; i < IMSI_ENTRY_COUNT; i ++)
	{
		LOGD ("IMSI %s with timestamp %ld\n", ptr->imsi, ptr->timestamp);

		if (strncmp (ptr->imsi, imsi_data.imsi, len) == 0)
		{
			ret_tamp = ptr->timestamp;
			break;
		}

		ptr ++;
	}

end:;
	partition_close (& pn);
	return (jlong) ret_tamp;
}

static jboolean setImsiTstmpNative (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring imsi)
{
	PARTITION pn;
	IMSI_Data imsi_data, *ptr;

	char data [sizeof (IMSI_Data) * IMSI_ENTRY_COUNT];
	int ret = 0, len, i;
	const int imsi_digit = sizeof (imsi_data.imsi);

	if (partition_open (& pn, "misc") < 0)
		goto end;

	if (partition_misc_usim_read (& pn, data, sizeof (data)) < 0)
		goto end;

	ptr = (IMSI_Data *) data;

	len = (*env)->GetStringLength (env, imsi);
	len = (len > imsi_digit) ? imsi_digit : len;

	(*env)->GetStringUTFRegion (env, imsi, 0, len, imsi_data.imsi);
	imsi_data.timestamp = time ((time_t *) NULL);

	for (i = 0; i < IMSI_ENTRY_COUNT; i ++)
	{
		//insert new record
		if (ptr->imsi [0] == 0xff)
		{
			*ptr = imsi_data;
			break;
		}

		//update record
		if (strncmp (ptr->imsi, imsi_data.imsi, len) == 0)
		{
			ptr->timestamp = imsi_data.timestamp;
			/*
			if (imsi_data.timestamp < ptr->timestamp)
				ptr->timestamp = -1;
			*/
			break;
		}

		ptr ++;
	}

	if (i == IMSI_ENTRY_COUNT)
	{
		memmove (ptr - IMSI_ENTRY_COUNT, ptr - (IMSI_ENTRY_COUNT - 1), sizeof (IMSI_Data) * (IMSI_ENTRY_COUNT - 1));
		*--ptr = imsi_data;
	}

	if (partition_misc_usim_write (& pn, data, sizeof (data)) < 0)
		goto end;

	ret = 1;

end:;
	partition_close (& pn);
	return ret;
}

static jboolean clearImsiTstmpNative (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	PARTITION pn;
	IMSI_Data *ptr;

	char data [sizeof (IMSI_Data) * IMSI_ENTRY_COUNT];
	int i;
	int ret = 0;

	if (partition_open (& pn, "misc") < 0)
		goto end;

	ptr = (IMSI_Data *) data;

	for (i = 0; i < IMSI_ENTRY_COUNT; i ++)
	{
		ptr->imsi [0] = -1;
		ptr->timestamp = -1;
		ptr ++;
	}

	if (partition_misc_usim_write (& pn, data, sizeof (data)) < 0)
		goto end;

	ret = 1;

end:;
	partition_close (& pn);
	return ret;
}

static jstring getUserName (JNIEnv *env, jclass UNUSED_VAR (clazz), jint uid)
{
	struct passwd *p = getpwuid ((uid_t) uid);

	if (p) return cs2jstring (env, p->pw_name);

	return NULL;
}

static jstring getGroupName (JNIEnv *env, jclass UNUSED_VAR (clazz), jint gid)
{
	struct group *p = getgrgid ((gid_t) gid);

	if (p) return cs2jstring (env, p->gr_name);

	return NULL;
}

static jlong confNew (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring filename)
{
	char *name;
	long addr = 0;
	if ((name = jstring2cs (env, filename)) != NULL)
	{
		addr = (long) conf_new (name);
	}
	fLOGD ("confNew(): %p [%s]", (void *) addr, name);
	if (name) free (name);
	return addr;
}

static jlong confLoad (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring filename)
{
	char *name;
	long addr = 0;
	if ((name = jstring2cs (env, filename)) != NULL)
	{
		addr = (long) conf_load_from_file (name);

		if (addr == (long) NULL)
		{
			addr = (long) conf_new (name);
		}
	}
	fLOGD ("confLoad(): %p [%s]", (void *) addr, name);
	if (name) free (name);
	return addr;
}

static jboolean confSave (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	fLOGD ("confSave(): %p", (void *) ((long) fd));

	return (conf_save_to_file ((CONF *) ((long) fd)) == 0) ? 1 : 0;
}

static void confClose (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	fLOGD ("confClose(): %p", (void *) ((long) fd));

	conf_destroy ((CONF *) ((long) fd));
}

static jstring confGetNative (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jstring name)
{
	char *key_name, *value = NULL;
	if ((key_name = jstring2cs (env, name)) != NULL)
	{
		value = (char *) conf_get ((CONF *) ((long) fd), key_name, NULL);
		free (key_name);
	}
	return value ? cs2jstring (env, value) : NULL;
}

static jboolean confSet (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jstring name, jstring value)
{
	char *key_name, *key_value;
	int ret = 0;
	if ((key_name = jstring2cs (env, name)) != NULL)
	{
		key_value = value ? jstring2cs (env, value) : NULL;
		ret = (conf_set ((CONF *) ((long) fd), key_name, key_value) == 0) ? 1 : ((conf_remove ((CONF *) ((long) fd), key_name) == 0) ? 1 : 0);
		if (key_name) free (key_name);
		if (key_value) free (key_value);
	}
	return ret;
}

static void confClear (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	conf_remove_all ((CONF *) ((long) fd));
}

static void confSort (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	conf_sort ((CONF *) ((long) fd), strcmp);
}

static void confDebugDump (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	conf_dump ((CONF *) ((long) fd));
}

static jint confCount (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz), jlong fd)
{
	return conf_count ((CONF *) ((long) fd));
}

static jstring confGetPairNative (JNIEnv *env, jclass UNUSED_VAR (clazz), jlong fd, jint index)
{
	jstring ret;
	char *cs;
	int len;

	len = conf_get_pair ((CONF *) ((long) fd), index, NULL, 0);

	ret = 0;

	if ((cs = malloc ((len > 0) ? len : 1)) == NULL)
	{
		ret = cs2jstring (env, "");
	}
	else
	{
		if (len > 0)
		{
			conf_get_pair ((CONF *) ((long) fd), index, cs, len);
		}
		else
		{
			cs [0] = 0;
		}

		ret = cs2jstring (env, cs);

		free (cs);
	}
	return ret;
}

static jstring getUsbStorageDirectory (JNIEnv *env, jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_usb_storage ();

	if (! ext)
	{
		ext = "/mnt/usb";
	}

	return cs2jstring (env, ext);
}

static jint getUsbStorageState (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_usb_storage ();

	if (! ext)
	{
		ext = "/mnt/usb";
	}

	fLOGD ("use usb storage [%s]", ext);

	return dir_storage_state (ext);
}

static jstring getExternalStorageDirectory (JNIEnv *env, jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_external_storage ();

	if (! ext)
	{
		ext = "/sdcard";
	}

	return cs2jstring (env, ext);
}

static jint getExternalStorageState (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_external_storage ();

	if (! ext)
	{
		ext = "/sdcard";
	}

	fLOGD ("use external storage [%s]", ext);

	return dir_storage_state (ext);
}

static jstring getPhoneStorageDirectory (JNIEnv *env, jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_phone_storage ();

	if (! ext)
	{
		ext = "/emmc";
	}

	return cs2jstring (env, ext);
}

static jint getPhoneStorageState (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_phone_storage ();

	if (! ext)
	{
		ext = "/emmc";
	}

	fLOGD ("use phone storage [%s]", ext);

	return dir_storage_state (ext);
}

static jstring getStorageDirectory (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring storage_name)
{
	const char *path;
	char *name;

	if ((storage_name == NULL) || ((name = jstring2cs (env, storage_name)) == NULL))
	{
		path = NULL;
	}
	else
	{
		path = dir_get_known_storage (name);
		free (name);
	}

	return path ? cs2jstring (env, path) : NULL;
}

static jint getStorageState (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring storage_name)
{
	const char *path;
	char *name;

	if ((storage_name == NULL) || ((name = jstring2cs (env, storage_name)) == NULL))
	{
		path = NULL;
	}
	else
	{
		path = dir_get_known_storage (name);
		free (name);
	}

	return path ? dir_storage_state (path) : 0;
}

static jstring getLargerExternalStorage (JNIEnv *env, jclass UNUSED_VAR (clazz))
{
	const char *ext = dir_get_larger_storage ();

	if (! ext)
	{
		ext = "";
	}

	return cs2jstring (env, ext);
}

static jstring getEnv (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring name)
{
	char *env_name, *value = NULL;
	if ((env_name = jstring2cs (env, name)) != NULL)
	{
		value = getenv (env_name);
		free (env_name);
	}
	return value ? cs2jstring (env, value) : NULL;
}

static jboolean setEnv (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring name, jstring value)
{
	char *env_name = NULL, *env_value = NULL;
	int ret = -1;
	if (((env_name = jstring2cs (env, name)) != NULL) && ((env_value = jstring2cs (env, value)) != NULL))
	{
		ret = setenv (env_name, env_value, 1);
	}
	if (env_name) free (env_name);
	if (env_value) free (env_value);
	return (ret == 0);
}

static char *dummy_buffer = NULL;
static long dummy_length = 0;

static void createDummyFileFree (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	if (dummy_buffer)
	{
		free (dummy_buffer);
		dummy_buffer = NULL;
		dummy_length = 0;
	}
}

static jlong createDummyFilePreAlloc (JNIEnv *env, jclass clazz, jlong length)
{
	if (length < 0)
		return length;

	if (dummy_buffer)
	{
		createDummyFileFree (env, clazz);
	}

	dummy_length = 0;

	do
	{
		dummy_buffer = malloc (length);

		if (dummy_buffer)
			break;

		length >>= 1;
	}
	while (length > 0);

	if (dummy_buffer)
	{
		dummy_length = length;
	}

	return dummy_length;
}

static jboolean createDummyFile (JNIEnv *env, jclass UNUSED_VAR (clazz), jstring path, jlong size)
{
	char *filepath = jstring2cs (env, path);
	int ret = 0;
	if (filepath)
	{
		int fd = open (filepath, O_WRONLY | O_CREAT, 0666);

		if (fd < 0)
		{
			LOGE ("createDummyFile: open: %s!\n", strerror (errno));
		}
		else
		{
		#if 0
			if (ftruncate (fd, (off_t) size) < 0)
			{
				LOGE ("createDummyFile: ftruncate: %s!\n", strerror (errno));
			}
			else
			{
				off_t off = lseek (fd, (off_t) size - 1, SEEK_SET);

				if (off == (off_t) -1)
				{
					LOGE ("createDummyFile: lseek: %s!\n", strerror (errno));
				}
				else if (write (fd, "E", 1) < 0)
				{
					LOGE ("createDummyFile: write: %s!\n", strerror (errno));
				}
				else
				{
					ret = 1;
				}
			}
		#else
			char *pdata;
			int local = 0;

			if (dummy_buffer && (dummy_length == size))
			{
				pdata = dummy_buffer;
			}
			else
			{
				pdata = malloc (size);
				local = 1;
			}

			if (! pdata)
			{
				LOGE ("createDummyFile: malloc failed!\n");
			}
			else if (write (fd, pdata, size) < 0)
			{
				LOGE ("createDummyFile: write: %s!\n", strerror (errno));
			}
			else
			{
				ret = 1;
			}

			/*
			 * do not check (pdata == dummy_buffer) because the dummy_buffer would be freed before
			 */
			if (pdata && local)
			{
				free (pdata);
			}
		#endif

			fsync (fd);
			close (fd);
		}

		free (filepath);
	}
	return ret;
}

#if 0
enum SDIMG_FORMAT
{
	SDIMG_LR,
	SDIMG_RLR,
	SDIMG_NULL
};

#define	S3D_LIB	"libSDHeadTracking.so"

static void *s3d_handle = NULL;

static int (*s3d_SDinitSingleViewPoint) () = NULL;
static int (*s3d_SDCloseSingleViewPoint) () = NULL;
static int (*s3d_SDWriteFixdPoint) () = NULL;
static int (*s3d_SetWindowPos) (float fLeft,float fTop,float fRight,float fBottom, enum SDIMG_FORMAT Imgformat) = NULL;
static int (*s3d_SetEyepos) (float ex,float ey,float ez,float addxoff) = NULL;

static void S3D_Destroy (JNIEnv *env, jclass clazz)
{
	if (s3d_handle)
	{
		s3d_SDinitSingleViewPoint = NULL;
		s3d_SDCloseSingleViewPoint = NULL;
		s3d_SDWriteFixdPoint = NULL;
		s3d_SetWindowPos = NULL;
		s3d_SetEyepos = NULL;

		LOGD ("dlclose " S3D_LIB " ...");

		dlclose (s3d_handle);
		s3d_handle = NULL;
	}
}

static void S3D_Init (JNIEnv *env, jclass clazz)
{
	if (s3d_handle == NULL)
	{
		LOGD ("dlopen " S3D_LIB " ...");

		s3d_handle = dlopen (LIB_DIR S3D_LIB, RTLD_LAZY);

		if (! s3d_handle)
		{
			LOGE (LIB_DIR S3D_LIB ": %s", dlerror ());
		}
		else
		{
			s3d_SDinitSingleViewPoint = dlsym (s3d_handle, "SDinitSingleViewPoint");
			s3d_SDCloseSingleViewPoint = dlsym (s3d_handle, "SDCloseSingleViewPoint");
			s3d_SDWriteFixdPoint = dlsym (s3d_handle, "SDWriteFixdPoint");
			s3d_SetWindowPos = dlsym (s3d_handle, "SetWindowPos");
			s3d_SetEyepos = dlsym (s3d_handle, "SetEyepos");
		}
	}
}

static jint S3D_SDinitSingleViewPoint (JNIEnv *env, jclass clazz)
{
	if (s3d_SDinitSingleViewPoint)
	{
		return s3d_SDinitSingleViewPoint ();
	}
	LOGE ("SDinitSingleViewPoint: symbol was not loaded\n");
	return -1;
}

static jint S3D_SDCloseSingleViewPoint (JNIEnv *env, jclass clazz)
{
	if (s3d_SDCloseSingleViewPoint)
	{
		return s3d_SDCloseSingleViewPoint ();
	}
	LOGE ("SDCloseSingleViewPoint: symbol was not loaded!\n");
	return -1;
}

static jint S3D_SDWriteFixdPoint (JNIEnv *env, jclass clazz)
{
	if (s3d_SDWriteFixdPoint)
	{
		return s3d_SDWriteFixdPoint ();
	}
	LOGE ("SDWriteFixdPoint: symbol was not loaded!\n");
	return -1;
}

static jint S3D_SetWindowPos (JNIEnv *env, jclass clazz, jfloat fLeft, jfloat fTop, jfloat fRight, jfloat fBottom, jint Imgformat)
{
	if (s3d_SetWindowPos)
	{
		return s3d_SetWindowPos (fLeft, fTop, fRight, fBottom, Imgformat);
	}
	LOGE ("SetWindowPos: symbol was not loaded!\n");
	return -1;
}

static jint S3D_SetEyepos (JNIEnv *env, jclass clazz, jfloat ex, jfloat ey, jfloat ez, jfloat addxoff)
{
	if (s3d_SetEyepos)
	{
		return s3d_SetEyepos (ex, ey, ez, addxoff);
	}
	LOGE ("SetEyepos: symbol was not loaded!\n");
	return -1;
}
#endif

static jstring getBoardName (JNIEnv *env, jclass UNUSED_VAR (clazz))
{
	char buffer [BOARD_NAME_LEN];

	get_board_name (buffer, sizeof (buffer));

	return cs2jstring (env, buffer);
}

static void check_caps (void)
{
#if 0
	struct __user_cap_header_struct cap_header_data;
	cap_user_header_t cap_header = & cap_header_data;

	struct __user_cap_data_struct cap_data_data;
	cap_user_data_t cap_data = & cap_data_data;

	cap_header->pid = getpid ();
	cap_header->version = 0x19980330 /* _LINUX_CAPABILITY_VERSION or _LINUX_CAPABILITY_VERSION_1 */;

	if (capget (cap_header, cap_data) < 0)
	{
		LOGE ("capget: %s", strerror (errno));
	}
	else
	{
		LOGD ("cap effective=0x%x, permitted=0x%x, inheritable=0x%x\n", cap_data->effective, cap_data->permitted, cap_data->inheritable);
	}

	int caps = 0xFFFFFFFF;
	cap_data->effective = caps;
	cap_data->permitted = caps;

	if (capset (cap_header, cap_data) < 0)
	{
		LOGE ("capset: %s", strerror (errno));
	}
#endif
}

static void uMask0 (JNIEnv *UNUSED_VAR (env), jclass UNUSED_VAR (clazz))
{
	mode_t mask = umask (0);

	LOGD ("old umask = %o", mask);

	check_caps ();
}


/* native method table */

static JNINativeMethod method_table[] = {
	{ "fileOpen",			"(Ljava/lang/String;)J",				(void *) fileOpen },
	{ "fileOpenInputEvent",		"(Ljava/lang/String;)J",				(void *) fileOpenInputEvent },
	{ "fileRead",			"(JI)Ljava/lang/String;",				(void *) fileRead },
	{ "fileReadRaw",		"(JI)Ljava/lang/String;",				(void *) fileReadRaw },
	{ "fileReadInputEvent",		"(JILcom/htc/android/ssdtest/infoclaz/InputEvent;)I",	(void *) fileReadInputEvent },
	{ "fileWrite",			"(JLjava/lang/String;)I",				(void *) fileWrite },
	{ "fileWriteRaw",		"(JLjava/lang/String;J)I",				(void *) fileWriteRaw },
	{ "fileInterrupt",		"(J)V",							(void *) fileInterrupt },
	{ "fileClose",			"(J)V",							(void *) fileClose },
	{ "ioctlJNI",			"(JII)I",						(void *) ioctlJNI },
	{ "nativeGetCpuUsage",		"(I)F",							(void *) nativeGetCpuUsage },
	{ "getMbVersion",		"(I)Ljava/lang/String;",				(void *) getMbVersion },
	{ "readMiscConfig",		"([I)I",						(void *) readMiscConfig },
	{ "writeMiscConfig",		"([I)I",						(void *) writeMiscConfig },
	{ "clearMiscConfig",		"()I",							(void *) clearMiscConfig },
	{ "readMfgOffsetInt",		"(I)I",							(void *) readMfgOffsetInt },
	{ "writeMfgOffsetInt",		"(II)I",						(void *) writeMfgOffsetInt },
	{ "isAutoStartBitSet",		"()Z",                                                  (void *) isAutoStartBitSet },
	{ "getFileStat",		"(Lcom/htc/android/ssdtest/infoclaz/FileStat;)I",	(void *) getFileStat },
	{ "enableSensor",		"(Ljava/lang/String;Z)I",				(void *) enableSensor },
	{ "getHdmiCableState",		"()I",							(void *) getHdmiCableState },
	{ "getImsiTstmpNative",		"(Ljava/lang/String;)J",				(void *) getImsiTstmpNative },
	{ "setImsiTstmpNative",		"(Ljava/lang/String;)Z",				(void *) setImsiTstmpNative },
	{ "clearImsiTstmpNative",	"()Z",							(void *) clearImsiTstmpNative },
	{ "getUserName",		"(I)Ljava/lang/String;",				(void *) getUserName },
	{ "getGroupName",		"(I)Ljava/lang/String;",				(void *) getGroupName },
	{ "confNew",			"(Ljava/lang/String;)J",				(void *) confNew },
	{ "confLoad",			"(Ljava/lang/String;)J",				(void *) confLoad },
	{ "confSave",			"(J)Z",							(void *) confSave },
	{ "confClose",			"(J)V",							(void *) confClose },
	{ "confGetNative",		"(JLjava/lang/String;)Ljava/lang/String;",		(void *) confGetNative },
	{ "confSet",			"(JLjava/lang/String;Ljava/lang/String;)Z",		(void *) confSet },
	{ "confClear",			"(J)V",							(void *) confClear },
	{ "confSort",			"(J)V",							(void *) confSort },
	{ "confDebugDump",		"(J)V",							(void *) confDebugDump },
	{ "confCount",			"(J)I",							(void *) confCount },
	{ "confGetPairNative",		"(JI)Ljava/lang/String;",				(void *) confGetPairNative },
	{ "getUsbStorageDirectory",	"()Ljava/lang/String;",					(void *) getUsbStorageDirectory },
	{ "getUsbStorageState",		"()I",							(void *) getUsbStorageState },
	{ "getExternalStorageDirectory","()Ljava/lang/String;",					(void *) getExternalStorageDirectory },
	{ "getExternalStorageState",	"()I",							(void *) getExternalStorageState },
	{ "getPhoneStorageDirectory",	"()Ljava/lang/String;",					(void *) getPhoneStorageDirectory },
	{ "getPhoneStorageState",	"()I",							(void *) getPhoneStorageState },
	{ "getStorageDirectory",	"(Ljava/lang/String;)Ljava/lang/String;",		(void *) getStorageDirectory },
	{ "getStorageState",		"(Ljava/lang/String;)I",				(void *) getStorageState },
	{ "getLargerExternalStorage",	"()Ljava/lang/String;",					(void *) getLargerExternalStorage },
	{ "getBoardName",		"()Ljava/lang/String;",					(void *) getBoardName },
	{ "getEnv",			"(Ljava/lang/String;)Ljava/lang/String;",		(void *) getEnv },
	{ "setEnv",			"(Ljava/lang/String;Ljava/lang/String;)Z",		(void *) setEnv },
	{ "createDummyFile",		"(Ljava/lang/String;J)Z",				(void *) createDummyFile },
	{ "createDummyFilePreAlloc",	"(J)J",							(void *) createDummyFilePreAlloc },
	{ "createDummyFileFree",	"()V",							(void *) createDummyFileFree },
#if 0
	{ "S3D_Init",			"()V",							(void *) S3D_Init },
	{ "S3D_Destroy",		"()V",							(void *) S3D_Destroy },
	{ "S3D_SDinitSingleViewPoint",	"()I",							(void *) S3D_SDinitSingleViewPoint },
	{ "S3D_SDCloseSingleViewPoint",	"()I",							(void *) S3D_SDCloseSingleViewPoint },
	{ "S3D_SDWriteFixdPoint",	"()I",							(void *) S3D_SDWriteFixdPoint },
	{ "S3D_SetWindowPos",		"(FFFFI)I",						(void *) S3D_SetWindowPos },
	{ "S3D_SetEyepos",		"(FFFF)I",						(void *) S3D_SetEyepos },
#endif

	{ "uMask0",			"()V",							(void *) uMask0 },

	/* keep this the last */
	{ "getNativeTime",		"(Lcom/htc/android/ssdtest/infoclaz/TimeInfo;)I",	(void *) getNativeTime }
};


/* JNI entry function */

jint JNI_OnLoad (JavaVM *vm, void *UNUSED_VAR (reserved))	/* returns the JNI version on success, -1 on failure */
{
	JNIEnv *env = NULL;
	jint result = -1;

	//LOGI ("Entering JNI_OnLoad [" TOOL_VERSIONS "]\n");

	if ((*vm)->GetEnv (vm, (void **) & env, JNI_VERSION_1_4) != JNI_OK)
	{
		LOGW ("ERROR: GetEnv failed\n");
		goto failed;
	}

	if (NULL == env)
		goto failed;

	if (0 > jniRegisterNativeMethods (env, SSD_TEST_JLIB_CLASS, method_table, NELEM (method_table)))
		goto failed;

	/* success -- return valid version number */
	result = JNI_VERSION_1_4 ;

failed:
	LOGI ("Leaving JNI_OnLoad (result=0x%x)\n", result);
	return result ;
}

#ifdef __cplusplus
}
#endif
