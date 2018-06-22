#define LOG_TAG	"STT:hw_libs"

#include <dlfcn.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

#include <hardware/hardware.h>
#include <hardware/sensors.h>

#include <cutils/log.h>

#include "headers/hw_libs.h"

int hw_sensor_enable (const char *UNUSED_VAR (keyword), int UNUSED_VAR (enable))
{
#if 0
	struct sensors_module_t *module = NULL;
	struct sensors_control_device_t *sSensorDevice = NULL;
	int active = -1;

	if ((hw_get_module (SENSORS_HARDWARE_MODULE_ID, (const struct hw_module_t **) & module) != 0) || (module == NULL))
	{
		LOGE ("failed to get sensors module!");
		goto end;
	}

	/*
	 * We check sSensorDevice instead of the returned value because this function may not return correct value on some platform.
	 */
	sensors_control_open (& module->common, & sSensorDevice);

	if (sSensorDevice == NULL)
	{
		LOGE ("failed to open sensors control device!");
		goto end;
	}

	const struct sensor_t *list;
	int count = module->get_sensors_list (module, & list);
	int i;

	for (i = 0; i < count; i ++)
	{
		LOGD ("sensor %d: name:[%s], vendor:[%s], version:[%d], handle:[%d], type:[%d]",
			i, list [i].name, list [i].vendor, list [i].version, list [i].handle, list [i].type);

		if (strstr (list [i].name, keyword) != NULL)
		{
			active = sSensorDevice->activate (sSensorDevice, list [i].handle, enable);
			break;
		}
	}

	sensors_control_close (sSensorDevice);

end:;
#if 0	/* older sources do not have dso member */
	if (module && module->common.dso)
		dlclose (module->common.dso);
#endif

	return (active < 0) ? -1 : 0;
#else
	return -1;
#endif
}
