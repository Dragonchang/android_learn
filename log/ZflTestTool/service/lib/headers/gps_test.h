#ifndef	__SSD_GPS_TEST_H__
#define	__SSD_GPS_TEST_H__

//#include "gps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* jni commands */
#define	GPS_CMD_ENABLE		1
#define	GPS_CMD_DISABLE		2
#define	GPS_CMD_START_TYPE	3
#define	GPS_CMD_START		4
#define	GPS_CMD_STOP		5
#define	GPS_CMD_CLEAR		6

/* jni event type */
#define	GPS_EVT_STATUS		0
#define	GPS_EVT_DATA		1

/* jni start flag */
#define	GPS_FLAG_HOT		1
#define	GPS_FLAG_WARM		2
#define	GPS_FLAG_COLD		3

int gpstest_enable (int flag);

int gpstest_disable ();

int gpstest_set_fixtype (int flag);

int gpstest_start ();

int gpstest_stop ();

int gpstest_clear_aiding ();

int gpstest_extra_download_disable ();

int gpstest_extra_download (int enable, int interval);

#ifdef __cplusplus
}
#endif

#endif
