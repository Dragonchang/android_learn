#define	LOG_TAG	"STT:gpstest"

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <gps.h>
#include <cutils/log.h>

#include "headers/gps_test.h"
#include "headers/socket.h"
#include "lib.h"

/**
 * Name for the GPS XTRA interface. Added by Eric
 */
#define GPS_XTRA_INTERFACE      "gps-xtra"

#define	DO_FILTER_RECORD	1

static const GpsInterface *sGpsInterface = NULL;
static const GpsXtraInterface *sGpsXtraInterface = NULL;
static GpsAidingData aiding = 0;

static pthread_mutex_t mux = PTHREAD_MUTEX_INITIALIZER;
static SOCKET_ID id = NULL;
static int type_do_count = 0;
static time_t begtime = 0;

#if DO_FILTER_RECORD
static time_t prevtime = 0;
#endif

static void _gps_status_callback (GpsStatus *ps)
{
	char status_buf [8] = "";

	strcpy (status_buf, "0:");

	switch (ps->status)
	{
	case GPS_STATUS_SESSION_BEGIN:	strcat (status_buf, "BEGIN");	break;
	case GPS_STATUS_SESSION_END:	strcat (status_buf, "END");	break;
	case GPS_STATUS_ENGINE_ON:	strcat (status_buf, "ON");	break;
	case GPS_STATUS_ENGINE_OFF:	strcat (status_buf, "OFF");	break;
	default: /* GPS_STATUS_NONE */	strcat (status_buf, "NONE");
	}

	LOGI ("CALLBACK: [%s] %d\n", status_buf, mux.value);

	if ((ps->status != GPS_STATUS_ENGINE_ON) && (ps->status != GPS_STATUS_ENGINE_OFF))
	{
		LOGI ("Ignore callback.\n");
		return;
	}

	pthread_mutex_lock (& mux);

	if (id)
	{
		LOGI ("Write status [%s]\n", status_buf);
		socket_write (id, status_buf, strlen (status_buf));
	}

	pthread_mutex_unlock (& mux);

	LOGD ("-- /// call back end /// --\n");
}

static void _gps_location_callback (GpsLocation *lo)
{
	char location_buf [128];
	int loglen;
	time_t curtime;
	double dif;
	struct tm time1, time2;

#if DO_FILTER_RECORD
	if (prevtime == begtime)
		return;

	prevtime = begtime;
#endif

	time (& curtime);
	dif = (double) curtime - (double) begtime;
	localtime_r (& begtime, & time1);
	localtime_r (& curtime, & time2);

	//sprintf (location_buf, "1:%d %f %f %f %f %f %f %u", lo->flags, lo->latitude, lo->longitude, lo->altitude,
	//							lo->speed, lo->bearing, lo->accuracy, lo->timestamp);

	loglen = sprintf (location_buf, "1:(%d)     %02d:%02d:%02d     %02d:%02d:%02d     %.2lf sec",
			++ type_do_count, time1.tm_hour, time1.tm_min, time1.tm_sec, time2.tm_hour, time2.tm_min, time2.tm_sec, dif);

	LOGI ("CALLBACK LOC: [%s]\n", location_buf);

	pthread_mutex_lock (& mux);

	if (id)
	{
		LOGI ("Write data [%s]\n", location_buf);
		socket_write (id, location_buf, strlen (location_buf));
	}

	pthread_mutex_unlock (& mux);

	LOGD ("-- /// call back end /// --\n");
}

GpsCallbacks sGpsCallbacks = {
	_gps_location_callback,
	_gps_status_callback,
	NULL,
};

static GpsAidingData gps_convert_flag (int flag)
{
	GpsAidingData ret = 0;
	if (flag == GPS_FLAG_WARM) ret = GPS_DELETE_EPHEMERIS;
	if (flag == GPS_FLAG_COLD) ret = GPS_DELETE_ALL;
	return ret;
}

int gpstest_enable (int flag)
{
	LOGD ("-- /// gpstest_enable /// --\n");
	pthread_mutex_lock (& mux);

	if (! id)
	{
		id = socket_init (LOCAL_IP, SOCKET_PORT_GPS);
	}

	LOGD ("-- GPS before init --\n");
	if (! sGpsInterface)
	{
		sGpsInterface = gps_get_interface ();
	}

	if ((! sGpsInterface) || (sGpsInterface->init (& sGpsCallbacks) != 0))
	{
		pthread_mutex_unlock (& mux);
		return -1;
	}
	LOGD ("-- GPS after init --\n");

	aiding = gps_convert_flag (flag);

	if (aiding)
	{
		sGpsInterface->delete_aiding_data (aiding);
	}

	sGpsInterface->set_position_mode (GPS_POSITION_MODE_STANDALONE, 1);

	time (& begtime);

#if DO_FILTER_RECORD
	prevtime = 0;
#endif

	LOGD ("-- GPS before start --\n");
	sGpsInterface->start ();
	LOGD ("-- GPS after start --\n");

	pthread_mutex_unlock (& mux);
	return 0;
}

int gpstest_disable ()
{
	LOGD ("-- /// gpstest_disable /// --\n");
	pthread_mutex_lock (& mux);

	if (id)
	{
		LOGI ("Try to end server ...\n");
		socket_write (id, CMD_CLI_CLOSE, strlen (CMD_CLI_CLOSE));
		socket_close (id);
		id = NULL;
	}

	LOGD ("-- GPS before stop -- %d\n", mux.value);
	sGpsInterface->stop ();
	LOGD ("-- GPS after stop --\n");
	//LOGD ("-- GPS after stop, before cleanup --\n");
	//sGpsInterface->cleanup ();
	//LOGD ("-- GPS after cleanup --\n");

	pthread_mutex_unlock (& mux);
	return 0;
}

int gpstest_set_fixtype (int flag)
{
	LOGD ("-- /// gpstest_set_fixtype /// --\n");
	pthread_mutex_lock (& mux);

	LOGD ("-- GPS before stop --\n");
	sGpsInterface->stop ();
	LOGD ("-- GPS after stop --\n");

	if (aiding != gps_convert_flag (flag))
	{
		aiding = gps_convert_flag (flag);
		type_do_count = 0;
	}

	pthread_mutex_unlock (& mux);
	return 0;
}

int gpstest_start ()
{
	LOGD ("-- /// gpstest_start /// --\n");
	pthread_mutex_lock (& mux);

	if (aiding)
	{
		sGpsInterface->delete_aiding_data (aiding);
	}

	sGpsInterface->set_position_mode (GPS_POSITION_MODE_STANDALONE, 1);

	time (& begtime);

#if DO_FILTER_RECORD
	prevtime = 0;
#endif

	LOGD ("-- GPS before start --\n");
	sGpsInterface->start ();
	LOGD ("-- GPS after start --\n");

	pthread_mutex_unlock (& mux);
	return 0;
}

int gpstest_stop ()
{
	LOGD ("-- /// gpstest_stop /// --\n");
	pthread_mutex_lock (& mux);

	LOGD ("-- GPS before stop -- %d\n", mux.value);
	sGpsInterface->stop ();
	LOGD ("-- GPS after stop --\n");

	pthread_mutex_unlock (& mux);
	return 0;
}

int gpstest_clear_aiding ()
{
	LOGD ("-- /// gpstest_clear_aiding /// --\n");
	pthread_mutex_lock (& mux);

	LOGD ("-- GPS before clear aiding -- %d\n", mux.value);
	sGpsInterface->delete_aiding_data (GPS_DELETE_ALL);
	LOGD ("-- GPS after clear aiding --\n");

	pthread_mutex_unlock (& mux);
	return 0;
}

int gpstest_extra_download_disable ()
{
	static GpsXtraCallbacks callbacks = { NULL };

	LOGD ("-- /// gps_test_extra_download_disable /// --\n");

	if (! sGpsInterface) {
		sGpsInterface = gps_get_interface ();
		if (sGpsInterface == NULL)
			return -1;
	}

	if(! sGpsXtraInterface) {
		sGpsXtraInterface = sGpsInterface->get_extension(GPS_XTRA_INTERFACE);
		if (sGpsXtraInterface == NULL)
			return -1;
	}

	// Current init function only support xtra download disabled.
	sGpsXtraInterface->init (& callbacks);

	LOGD ("-- Disable GPS extra download --\n");

	return 0;
}

int gpstest_extra_download (int enable, int interval)
{
#ifdef HTC_FLAG_XTRA_AUTO_DOWNLOAD
	LOGD ("-- /// gpstest_extra_download /// --\n");

	if (! sGpsInterface)
	{
		sGpsInterface = gps_get_interface ();
	}

	sGpsInterface->xtra_set_auto_download(enable, interval);

	LOGD ("-- Set GPS extra download -- %d\n", enable);
	LOGD ("-- Set GPS extra interval -- %d\n", interval);
#endif
	return 0;
}
