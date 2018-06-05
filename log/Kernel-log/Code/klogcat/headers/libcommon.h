

#ifndef	_SSD_TEST_COMMON_DEF_H_
#define	_SSD_TEST_COMMON_DEF_H_

/* these folders would be changed at compile time, so please do not modify! */
#define  APP_DIR  "/system/"
#define  BIN_DIR  APP_DIR "bin/"
#define  LIB_DIR  APP_DIR "lib/"
#define  DAT_DIR  "/data/"
#define  TMP_DIR  "/sqlite_stmt_journals/"

#define  LOG_FOLDER_NAME  "htclog/"
#define  LOG_DIR  DAT_DIR LOG_FOLDER_NAME

#define	LOG_DATA_EXT	"debugklog"
#define	LOG_FILE_TAG	""

#define	DEFAULT_DIR_MODE	(0777)
#define	DEFAULT_FILE_MODE	(0666)

/* local socket path */
#define	SOCKET_SERVER_PATH	TMP_DIR "sttsocket.s."
#define	SOCKET_CLIENT_PATH	TMP_DIR "sttsocket.c."

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

#ifndef UNUSED_VAR
#define UNUSED_VAR(x) UNUSED_ ## x __attribute__((__unused__))
#endif

#ifndef UNUSED_FUNC
#define UNUSED_FUNC(x) __attribute__((__unused__)) UNUSED_ ## x
#endif

#endif

