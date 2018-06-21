#ifndef _Included_com_htc_android_ssdtest_HtcNative
#define _Included_com_htc_android_ssdtest_HtcNative

#include <jni.h>
#include <stdint.h>

#define SSD_TEST_JLIB_CLASS             "com/XXX/android/ssdtest/XxxxNative"
#define VERSION_STRING_SIZE		10
#define DIAG_MD_NORMAL 1

struct diag_logging_mode_param_t {
	 uint32_t  req_mode;
	 uint32_t  peripheral_mask;
	 uint8_t  mode_param;
};
//For new kernel diag struct, pd_mask = 0
struct diag_logging_mode_param_t_pd {
	 uint32_t  req_mode;
	 uint32_t  peripheral_mask;
	 uint32_t  pd_mask;
	 uint8_t  mode_param;
}__packed;

#ifdef __cplusplus
extern "C" {
#endif

#define	IMSI_ENTRY_COUNT	(10)

typedef struct {
    char imsi [16];
    time_t timestamp;
} IMSI_Data;

#ifdef __cplusplus
}
#endif
#endif
