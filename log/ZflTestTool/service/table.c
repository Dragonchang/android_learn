#include "common.h"

SERVICE_DECLARE (gettime);
SERVICE_DECLARE (proctrl);
SERVICE_DECLARE (logmem);
SERVICE_DECLARE (logfs);
SERVICE_DECLARE (iotty);
#ifdef BUILD_AND
SERVICE_DECLARE (logbattery);
SERVICE_DECLARE (logctl);
SERVICE_DECLARE (logkey);
SERVICE_DECLARE (dumpstate);
SERVICE_DECLARE (displaytest);
SERVICE_DECLARE (btrouter);
SERVICE_DECLARE (wimaxuarttool);
SERVICE_DECLARE (thrputclnt);
SERVICE_DECLARE (ghost);
SERVICE_DECLARE (fsspeed);
SERVICE_DECLARE (logtouch);
SERVICE_DECLARE (logrpm);
SERVICE_DECLARE (logprocrank);
SERVICE_DECLARE (touchtest);
SERVICE_DECLARE (logoom);
SERVICE_DECLARE (cputest);
SERVICE_DECLARE (lognet);
SERVICE_DECLARE (logsniff);
SERVICE_DECLARE (htc_if);
#endif

SERVICE table [] = {
	SERVICE_ENTRY (gettime),
	SERVICE_ENTRY (proctrl),
	SERVICE_ENTRY (logmem),
	SERVICE_ENTRY (logfs),
	SERVICE_ENTRY (iotty),
#ifdef BUILD_AND
	SERVICE_ENTRY (logbattery),
	SERVICE_ENTRY (logctl),
	SERVICE_ENTRY (logkey),
	SERVICE_ENTRY (dumpstate),
	SERVICE_ENTRY (displaytest),
	SERVICE_ENTRY (btrouter),
	SERVICE_ENTRY (wimaxuarttool),
	SERVICE_ENTRY (thrputclnt),
	SERVICE_ENTRY (ghost),
	SERVICE_ENTRY (fsspeed),
	SERVICE_ENTRY (logtouch),
	SERVICE_ENTRY (logrpm),
	SERVICE_ENTRY (logprocrank),
	SERVICE_ENTRY (touchtest),
	SERVICE_ENTRY (logoom),
	SERVICE_ENTRY (cputest),
	SERVICE_ENTRY (lognet),
    SERVICE_ENTRY (logsniff),
    SERVICE_ENTRY (htc_if),
#endif
	SERVICE_ENTRY_END
};
