#ifndef	_SSD_TEST_COMMON_DEF_H_
#define	_SSD_TEST_COMMON_DEF_H_

/* these folders would be changed at compile time, so please do not modify! */
#define  APP_DIR  "/system/"
#define  BIN_DIR  APP_DIR "bin/"
#define  LIB_DIR  APP_DIR "lib/"
#define  DAT_DIR  "/data/"
#define  TMP_DIR  "/sqlite_stmt_journals/"

#define  LOG_FOLDER_NAME  "zfllog/"
#define  LOG_DIR  DAT_DIR LOG_FOLDER_NAME

#define  GHOST_FOLDER_NAME  "ghost/"
#define  GHOST_DIR  DAT_DIR GHOST_FOLDER_NAME

#define	LOG_DATA_EXT	"stt"
#define	LOG_FILE_TAG	""

#define	DEFAULT_DIR_MODE	(0777)
#define	DEFAULT_FILE_MODE	(0666)

/* local host ip */
#define	LOCAL_IP		"127.0.0.1"

/* fixed socket ports */
#define	SOCKET_PORT_FIRST	61500
#define	SOCKET_PORT_SERVICE	(SOCKET_PORT_FIRST)
#define	SOCKET_PORT_GPS		(SOCKET_PORT_FIRST + 1)
#define	SOCKET_PORT_COMPORT	(SOCKET_PORT_FIRST + 2)
#define	SOCKET_PORT_GPS_PDAPI_NMEA_PRESERVE	(SOCKET_PORT_FIRST + 3)//Preserve for NMEA log, don't remove it.
#define	SOCKET_PORT_LAST	65500

/* local socket path */
#define	SOCKET_SERVER_PATH	TMP_DIR "sttsocket.s."
#define	SOCKET_CLIENT_PATH	TMP_DIR "sttsocket.c."

/* reduce stt logs */
#include <cutils/log.h>
#include <cutils/properties.h>
#include <stdarg.h>
#include <string.h>

#define PROPERTY_SILENT_LOG_FLAG 	"persist.sys.stt.log.silent"

#ifndef LOGV
#define LOGV ALOGV
#endif
#ifndef LOGI
#define LOGI ALOGI
#endif
#ifndef LOGD
#define LOGD ALOGD
#endif
#ifndef LOGW
#define LOGW ALOGW
#endif
#ifndef LOGE
#define LOGE ALOGE
#endif

#define	fLOGV	LOGV
#define	fLOGI	LOGI
#define	fLOGD	LOGD
#define	fLOGW	LOGW
#define	fLOGE	LOGE

#define fLOGV_IF(...)	{ SILENT_DMSG(1,__VA_ARGS__); }
#define fLOGI_IF(...)	{ SILENT_DMSG(2,__VA_ARGS__); }
#define fLOGD_IF(...)	{ SILENT_DMSG(3,__VA_ARGS__); }
#define fLOGW_IF(...)	{ SILENT_DMSG(4,__VA_ARGS__); }
#define fLOGE_IF(...)	{ SILENT_DMSG(5,__VA_ARGS__); }

#ifndef UNUSED_VAR
#define UNUSED_VAR(x) UNUSED_ ## x __attribute__((__unused__))
#endif

#ifndef UNUSED_FUNC
#define UNUSED_FUNC(x) __attribute__((__unused__)) UNUSED_ ## x
#endif

static int bSilentLogEnable = -1;

static void update_silent_log_flag (void)
{
	char buf [PROPERTY_VALUE_MAX];
	memset (buf, 0, sizeof (buf));

	property_get (PROPERTY_SILENT_LOG_FLAG, buf, "0");
	bSilentLogEnable = (buf [0] == '1');

	ALOGD ("update silent log flag[%d] ...\n", bSilentLogEnable);
}

static void SILENT_DMSG(int level, const char *fmt, ...)
{
	if (bSilentLogEnable == -1)
		update_silent_log_flag();

    char buf [1024];
    va_list ap;
    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    buf[sizeof (buf) - 1] = 0;
    va_end(ap);

    switch (level) {
    case 1:
        if (bSilentLogEnable != 1)
			LOGV("%s", buf);
        break;
    case 2:
        if (bSilentLogEnable != 1)
			LOGI("%s", buf);
        break;
    case 3:
        if (bSilentLogEnable != 1)
			LOGD("%s", buf);
        break;
    case 4:
		ALOGW("%s", buf);
        break;
    case 5:
		ALOGE("%s", buf);
        break;
    }
}



#endif
