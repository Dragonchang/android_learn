#define	LOG_TAG	"STT:usb"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>

#include "headers/attr_table_switch.h"
#include "headers/usb.h"

const char *attr_path_usb		= "/sys/class/power_supply/usb/online";
const char *attr_path_fserial		= "/sys/devices/platform/fserial/interface/fserial_enable";
const char *attr_path_fserial_ics	= "/sys/class/android_usb/f_serial/on";

static ATTR_TABLE usb_control_table [] = {
	{ "/sys/module/msm_serial_debugger/parameters/uart_enabled",	"0", 		0, "" },
	{ "/sys/module/pm/parameters/sleep_mode",			"5",		0, "" },
	{ "/sys/module/pm/parameters/idle_sleep_mode",			"5",		0, "" },
	{ "/sys/module/diag/parameters/enabled",			"1",		0, "" },
	{ "/sys/class/android_usb/f_diag/on",				"1",		0, "" },
	{ "/sys/module/serial/parameters/serial_enabled",		"1",		0, "" },
	{ "sys.usb.config",						"serial",	0, "" },
	/* for ti omap */
	{ "/sys/devices/platform/omap-uart.0/sleep_timeout",		"0", 		0, "" },
	{ "/sys/devices/platform/omap-uart.1/sleep_timeout",		"0", 		0, "" },
	{ "/sys/devices/platform/omap-uart.2/sleep_timeout",		"0", 		0, "" },
	{ "/sys/devices/platform/omap-uart.3/sleep_timeout",		"0", 		0, "" },
	{ "/sys/devices/platform/serial8250.0/sleep_timeout",		"0", 		0, "" },
	{ "", "", 0, "" }
};

static char fserial_state = 0;

static char fserial_control (char enable)
{
	FILE *fp;
	char ret = '0';

	fp = fopen (attr_path_fserial, "w+");

	if (! fp)
		return ret;

	if (fread (& ret, 1, 1, fp) != 1)
	{
		ALOGE ("%s: %s\n", attr_path_fserial, strerror (errno));
		ret = '0';
	}
	else if (ret != enable)
	{
		if (fwrite (& enable, 1, 1, fp) != 1)
		{
			ALOGE ("%s: %s\n", attr_path_fserial, strerror (errno));
			ret = '0';
		}
		else
		{
			ALOGD ("fserial_control %s: %c\n", attr_path_fserial, enable);
		}
	}

	fclose (fp);
	return ret;
}

void enable_usb_serial (int enable)
{
	if (access (attr_path_fserial_ics, F_OK) == 0)
	{
		FILE *fp;
		char ret = '0';
		char value = enable ? '1' : '0';

		fp = fopen (attr_path_fserial_ics, "w+");

		if (! fp)
		{
			ALOGE ("%s: %s\n", attr_path_fserial_ics, strerror (errno));
			return;
		}

		if (fwrite (& value, 1, 1, fp) != 1)
		{
			ALOGE ("%s: %s\n", attr_path_fserial_ics, strerror (errno));
		}
		else
		{
			ALOGD ("enable_usb_serial: %s: [%c]\n", attr_path_fserial_ics, value);
		}

		fclose (fp);
	}
	else
	{
		/*
		 * try to enable USB serial by setting property. requested by S731 Xerox Lin for T1 (flounder).
		 *
		 * adb shell "setprop sys.usb.config adb,serial"
		 */
		ATTR_TABLE local_table [] = {
			{ "sys.usb.config", "serial", 0, "" },
			{ "", "", 0, "" }
		};
		int count = 256;

		if (enable)
		{
			table_get (& count, local_table);
		}
		else
		{
			table_put (& count, local_table);
		}
	}
}

static int ref_count = 0;

int usb_serial_get (void)
{
	if (access (attr_path_fserial, F_OK) == 0)
	{
		fserial_state = fserial_control ('1');
		return 0;
	}
	return table_get (& ref_count, usb_control_table);
}

int usb_serial_put (void)
{
	if (access (attr_path_fserial, F_OK) == 0)
	{
		fserial_control (fserial_state);
		return 0;
	}
	return table_put (& ref_count, usb_control_table);
}

int is_usb_online (void)
{
	char buffer [8];
	int fd;

	buffer [0] = '0';

	fd = open (attr_path_usb, O_RDONLY);

	if (fd < 0)
	{
		ALOGE ("open %s: %s\n", attr_path_usb, strerror (errno));
	}
	else
	{
		if (read (fd, buffer, sizeof (buffer)) < 0)
		{
			ALOGE ("read %s: %s\n", attr_path_usb, strerror (errno));
		}
		close (fd);
	}

	return (buffer [0] == '1') ? 1 : 0;
}
