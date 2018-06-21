#define	LOG_TAG		"STT:cputest"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/statfs.h>
#include <cutils/misc.h>

#include "common.h"
#include "server.h"

#include "headers/process.h"

#define	VERSION	"2.7"
/*
 * 2.7	: refine cpufreq tool for 8952 and after.
 * 2.6	: support new MTK hotplug values.
 * 2.5	: 1. fix cpufreq table because both clusters have different cpu freq table.
 *    	: 2. save old governor and set it back after users turn off CPU control.
 *    	: 3. save old mpdecision and set it back after users turn off CPU control.
 * 2.4	: cpu0 may not have online node.
 * 2.3	: change DVFS test interval unit to ms.
 * 2.2	: fix a not return bug.
 * 2.1  : support 4th test mode (stress cpu with random cpu on/off and random frequencies).
 * 2.0	: reset CPU min/max frequencies after disabling CPU controllers.
 * 1.9	: take pnpmgr as a generic CPU controller.
 * 1.8	: 1. support new NV hotplug values. 2. make able to control CPU by default when controllers are not found.
 * 1.7	: considering tool is unable to change CPU state by default when there is no CPU controller found.
 * 1.6	: take thermald as a QCT second CPU controller.
 * 1.5	: support more commands for CPU Frequency tool.
 * 1.4	: add test mode "syncrandom".
 * 1.3	: support individual CPU settings.
 * 1.2	: support different test modes (ascend/descend/random).
 * 1.1	: use native frequency squence from kernel.
 * 1.0	: switch CPU frequencies periodically.
 */

/* custom commands */
#define CMD_GETCPUCOUNT		":getcpucount:"
#define CMD_GETINTERVAL		":getinterval:"
#define CMD_SETINTERVAL		":setinterval:"
#define CMD_GETTESTMODE		":gettestmode:"
#define CMD_SETTESTMODE		":settestmode:"
#define CMD_GETCPUMODES		":getcpumodes:"
#define CMD_SETCPUMODES		":setcpumodes:"
#define CMD_RUN			":run:"
#define CMD_STOP		":stop:"
#define CMD_ISRUNNING		":isrunning:"
#define CMD_GETCTRLSTATUS	":getctrlstatus:"
#define CMD_SETCTRLSTATUS	":setctrlstatus:"
#define CMD_GETAVAILFREQS	":getavailfreqs:"
#define CMD_SETCPUMODE		":setcpumode:"
#define CMD_SETCPUFREQ		":setcpufreq:"

#define	TESTMODE_ASCEND		(0)
#define	TESTMODE_DESCEND	(1)
#define	TESTMODE_RANDOM		(2)	/* each cpu has its own random index */
#define	TESTMODE_SYNCRANDOM	(3)	/* use the same random index to all cpus */
#define	TESTMODE_CPURANDOM	(4)	/* each cpu can be on/off and has its own random index */
#define	TESTMODE_COUNT		(5)

#define	CPUMODE_OFFLINE		('x')
#define	CPUMODE_DYNAMIC		('d')
#define	CPUMODE_USER		('u')

#define	GOV_USERSPACE		"userspace"
#define	GOV_ONDEMAND		"ondemand"

#define	SERVICE_PNPMGR			"pnpmgr"
#define	SERVICE_THERMALD		"thermald"
#define	SERVICE_MPDECISION		"mpdecision"

#define	MODULE_CORE_CTL		"core_ctl"

#define	RUNNING			"running"
#define	STOPPED			"stopped"
#define	UNKNOWN			"unknown"

#ifndef	TMP_DIR
#define	TMP_DIR	"/tmp"
#endif

#define MAX_GOVERNOR_BUFFER	64

static int done = 0;
static int interval = 3600 * 1000; // ms
static int testmode = TESTMODE_ASCEND;
static char *cpumodes = NULL;
static char *default_pnpmgr = NULL;
static char *default_thermald = NULL;
static char *default_mpdecision = NULL;
static char **default_governors = NULL;
static char default_nv_hotplug [4] = {0};
static char default_mtk_hotplug [4] = {0};

static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *node_online			= "/sys/devices/system/cpu/cpu%d/online";
static const char *node_available_frequencies	= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies";
static const char *node_available_governors	= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_available_governors";
static const char *node_governor		= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor";
static const char *node_setspeed		= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed";
static const char *node_curfreq			= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq";
static const char *node_minfreq			= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq";
static const char *node_maxfreq			= "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq";
static const char *node_minfreq_info		= "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq";
static const char *node_maxfreq_info		= "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq";

static const char *NV_HOTPLUG = "/sys/module/cpu_tegra3/parameters/auto_hotplug";
static const char *MTK_HOTPLUG = "/proc/hps/enabled";
static const char *PROC_MODULES = "/proc/modules";

extern int init_module (void *, unsigned long, const char *);
extern int delete_module (const char *, unsigned int);

static void choppy (char *s)
{
	// remove newline character from characters
	while (*s && *s != '\n' && *s != '\r')
		s++;

	*s = 0;
}

static char *node_path (const char *node, int idx, char *buffer, int buflen)
{
	snprintf (buffer, buflen, node, idx);
	buffer [buflen - 1] = 0;
	return buffer;
}

static int have_cpu_node (const char *node, int idx)
{
	char path [PATH_MAX];
	return (access (node_path (node, idx, path, sizeof (path)), F_OK) == 0);
}

static int have_cpu_online_node (int idx)
{
	return have_cpu_node (node_online, idx);
}

static int write_cpu_node (const char *node, int idx, const char *buffer)
{
	char path [PATH_MAX];
	int fd;

	if ((fd = open (node_path (node, idx, path, sizeof (path)), O_WRONLY)) < 0)
	{
		DM ("open %s: %s\n", path, strerror (errno));
		return -1;
	}

	if (write (fd, buffer, strlen (buffer)) < 0)
	{
		DM ("write %s: %s\n", path, strerror (errno));
		close (fd);
		return -1;
	}

	close (fd);

	DM ("write_cpu_node [%s][%s]\n", path, buffer);
	return 0;
}

static int write_cpu_online_node (int idx, const char *buffer)
{
	return write_cpu_node (node_online, idx, buffer);
}

static int read_cpu_node (const char *node, int idx, char *buffer, int buflen)
{
	char path [PATH_MAX];
	int fd;

	if ((fd = open (node_path (node, idx, path, sizeof (path)), O_RDONLY)) < 0)
	{
		DM ("open %s: %s\n", path, strerror (errno));
		return -1;
	}

	if (read (fd, buffer, buflen) < 0)
	{
		DM ("read %s: %s\n", path, strerror (errno));
		close (fd);
		return -1;
	}
	buffer [buflen - 1] = 0;
	choppy (buffer);

	close (fd);
	return 0;
}

static int read_cpu_online_node (int idx, char *buffer, int buflen)
{
	return read_cpu_node (node_online, idx, buffer, buflen);
}

static int get_cpu_count (void)
{
	static int cpu_count = 0;

	int idx;

	if (cpu_count < 1)
	{
		for (idx = 0; ; idx ++)
		{
			if (! have_cpu_online_node (idx))
				break;
		}

		cpu_count = idx;
	}

	return cpu_count;
}

static int set_cpu_online (int idx, int online)
{
	int ret = 0;
	char buffer [2];

	if (! have_cpu_online_node (idx))
		return -1;

	if (read_cpu_online_node (idx, buffer, sizeof (buffer)) < 0)
		buffer [0] = 0;

	if (online && (buffer [0] != '1'))
	{
		ret = write_cpu_online_node (idx, "1");
	}
	if ((! online) && (buffer [0] == '1'))
	{
		ret = write_cpu_online_node (idx, "0");
	}
	return ret;
}

static GLIST *get_cpu_available_governors (int idx)
{
	GLIST *cpu_govers = NULL;
	char buffer [1024];
	char *p1, *p2;
	int c;

	bzero (buffer, sizeof (buffer));

	if (read_cpu_node (node_available_governors, idx, buffer, sizeof (buffer)) == 0)
	{
		DM ("cpu%d governor buffer [%s]\n", idx, buffer);

		for (p1 = buffer; *p1 && (isspace (*p1)); p1 ++);

		for (; *p1;)
		{
			for (p2 = p1; *p2 && (! isspace (*p2)); p2 ++);

			c = *p2;
			*p2 = 0;

			DM ("cpu%d governor: %s\n", idx, p1);

			/* use glist_append() instead of glist_add() to keep the freq squence */
			glist_append (& cpu_govers, strdup (p1));

			for (p1 = c ? p2 + 1 : p2; *p1 && (isspace (*p1)); p1 ++);
		}
	}

	return cpu_govers;
}

static int get_cpu_governor (int idx, char *buffer, int buflen)
{
	if (read_cpu_node (node_governor, idx, buffer, buflen) != 0)
		return -1;

	return 0;
}

static GLIST *get_cpu_available_frequencies (int idx)
{
	GLIST *cpu_freqs = NULL;
	char buffer [1024];
	char *p1, *p2;
	int c;

	bzero (buffer, sizeof (buffer));

	if (read_cpu_node (node_available_frequencies, idx, buffer, sizeof (buffer)) == 0)
	{
		DM ("cpu%d frequency buffer [%s]\n", idx, buffer);

		for (p1 = buffer; *p1 && (isspace (*p1)); p1 ++);

		for (; *p1;)
		{
			for (p2 = p1; *p2 && (! isspace (*p2)); p2 ++);

			c = *p2;
			*p2 = 0;

			DM ("cpu%d frequency: %s\n", idx, p1);

			/* use glist_append() instead of glist_add() to keep the freq squence */
			glist_append (& cpu_freqs, strdup (p1));

			for (p1 = c ? p2 + 1 : p2; *p1 && (isspace (*p1)); p1 ++);
		}
	}

	return cpu_freqs;
}

static int set_cpu_governor (int idx, const char *gover)
{
	if (set_cpu_online (idx, 1) < 0)
		return -1;

	if (! have_cpu_node (node_governor, idx))
		return -1;

	return write_cpu_node (node_governor, idx, gover);
}

static int set_cpu_frequency (int idx, const char *freq)
{
	if (set_cpu_online (idx, 1) < 0)
		return -1;

	if (! have_cpu_node (node_setspeed, idx))
		return -1;

	if (have_cpu_node (node_governor, idx))
	{
		write_cpu_node (node_governor, idx, GOV_USERSPACE);
	}

	write_cpu_node (node_setspeed, idx, freq);
	return 0;
}

static int compare_cpu_frequency (int idx, const char *freq)
{
	if (! have_cpu_node (node_curfreq, idx))
		return -1;

	char buffer [12] = {0};

	// read cpu current frequency
	if (read_cpu_node (node_curfreq, idx, buffer, sizeof (buffer)) < 0)
	{
		DM ("CPURandom[E]cpu%d read frequency failed!\n", idx);
	}
	else
	{
		choppy(buffer);

		DM ("CPURandom-cpu%d: previous frequency [%s], current frequency [%s].\n", idx, freq, buffer);

		if(strcmp(buffer, freq) != 0)
		{
			DM ("CPURandom[E]cpu%d: Failed frequency change!\n", idx);
		}
	}
	return 0;
}

static int reset_cpu_frequency_range (int idx)
{
	char buffer [12];

	if (set_cpu_online (idx, 1) < 0)
		return -1;

	if ((! have_cpu_node (node_minfreq, idx)) || (! have_cpu_node (node_maxfreq, idx)) ||
		(! have_cpu_node (node_minfreq_info, idx)) || (! have_cpu_node (node_maxfreq_info, idx)))
		return -1;

	memset (buffer, 0, sizeof (buffer));

	if (read_cpu_node (node_minfreq_info, idx, buffer, sizeof (buffer)) < 0)
	{
		DM ("cpu%d read min frequency failed!\n", idx);
	}
	else
	{
		buffer [sizeof (buffer) - 1] = 0;

		write_cpu_node (node_minfreq, idx, strtrim (buffer));
	}

	memset (buffer, 0, sizeof (buffer));

	if (read_cpu_node (node_maxfreq_info, idx, buffer, sizeof (buffer)) < 0)
	{
		DM ("cpu%d read max frequency failed!\n", idx);
	}
	else
	{
		buffer [sizeof (buffer) - 1] = 0;

		write_cpu_node (node_maxfreq, idx, strtrim (buffer));
	}
	return 0;
}

static int get_service_state (const char *key, char *value, const char *default_value)
{
	char propertyName [PROPERTY_KEY_MAX] = {0};
	int ret = 0;

	ret = snprintf (propertyName, sizeof (propertyName), "init.svc.%s", key);
	if (ret > (PROPERTY_KEY_MAX - 1))
	{
		DM ("Service name [%s] is too long", key);

		if (default_value)
		{
			ret = strlen (default_value);
			if (ret >= PROPERTY_VALUE_MAX)
			{
				ret = PROPERTY_VALUE_MAX - 1;
			}
			memcpy (value, default_value, ret);
			value [ret] = '\0';
		}

		return ret;
	}

	return property_get (propertyName, value, default_value);
}

int wait_for_service (const char *name, const char *value, int timeCount)
{
	char propertyValue [PROPERTY_VALUE_MAX];

	while ((-- timeCount) >= 0)
	{
		get_service_state (name, propertyValue, NULL);
		if (strcmp (propertyValue, value) == 0)
		{
			return 1;
		}

		usleep (50000);  // 50 msec
	}

	return -1;
}

static int is_module_loaded (const char *moduleName)
{
	FILE *proc;
	char line [PROPERTY_VALUE_MAX];

	if ((proc = fopen (PROC_MODULES, "rb")) == NULL)
	{
		DM ("fopen %s: %s\n", PROC_MODULES, strerror (errno));
		return -1;
	}

	while ((fgets (line, sizeof (line), proc)) != NULL)
	{
		if (strncmp (line, moduleName, strlen (moduleName)) == 0)
		{
			fclose (proc);
			return 1;
		}
	}

	fclose (proc);
	return 0;
}

static int insmod (const char *fileName)
{
	void *module;
	unsigned int size;
	int ret;

	module = load_file (fileName, &size);
	if (!module)
	{
		DM ("load_file %s: %s\n", fileName, strerror (errno));
		return -1;
	}

	ret = init_module (module, size, "");
	if (ret != 0)
	{
		DM ("init_module %s failed: %s\n", fileName, strerror (errno));
	}

	free (module);

	return ret;
}

static int get_cpu_controller_status (void)
{
	char value [PROPERTY_VALUE_MAX];
	int fd, i, ret;

	value [0] = 0;

	/*
	 * a cross platform CPU controller
	 * program: /system/bin/pnpmgr
	 */
	get_service_state (SERVICE_PNPMGR, value, UNKNOWN);
	if (strcmp (value, RUNNING) == 0)
	{
		DM ("%s service controller found!\n", SERVICE_PNPMGR);
		return 1;
	}

	/*
	 * mpdecision & thermald on QCT
	 * program: /system/bin/thermald
	 */
	get_service_state (SERVICE_THERMALD, value, UNKNOWN);
	if (strcmp (value, RUNNING) == 0)
	{
		DM ("%s service controller found!\n", SERVICE_THERMALD);
		return 1;
	}

	/*
	 * mpdecision & thermald on QCT
	 * program: /system/bin/mpdecision
	 */
	get_service_state (SERVICE_MPDECISION, value, UNKNOWN);
	if (strcmp (value, RUNNING) == 0)
	{
		DM ("%s service controller found!\n", SERVICE_MPDECISION);
		return 1;
	}

	/*
	 * auto hotplug on NV
	 */
	if (access (NV_HOTPLUG, F_OK) == 0)
	{
		if ((fd = open (NV_HOTPLUG, O_RDONLY)) < 0)
		{
			DM ("open %s: %s\n", NV_HOTPLUG, strerror (errno));
		}
		else
		{
			if (read (fd, value, sizeof (value)) < 0)
			{
				DM ("read %s: %s\n", NV_HOTPLUG, strerror (errno));
			}
			close (fd);
			if (value [0] != '0')
			{
				DM ("%s hotplug controller found!\n", NV_HOTPLUG);
				return 1;
			}
		}
	}

	/*
	 * auto hotplug on MTK
	 */
	if (access (MTK_HOTPLUG, F_OK) == 0)
	{
		if ((fd = open (MTK_HOTPLUG, O_RDONLY)) < 0)
		{
			DM ("open %s: %s\n", MTK_HOTPLUG, strerror (errno));
		}
		else
		{
			if (read (fd, value, sizeof (value)) < 0)
			{
				DM ("read %s: %s\n", MTK_HOTPLUG, strerror (errno));
			}
			close (fd);
			if (value [0] != '0')
			{
				DM ("%s hotplug controller found!\n", MTK_HOTPLUG);
				return 1;
			}
		}
	}

	if ((is_module_loaded (MODULE_CORE_CTL)) == 1)
	{
		DM ("%s module controller found!\n", MODULE_CORE_CTL);
		return 1;
	}

	/*
	 * no controller found
	 */
	return 0;
}

static void enable_cpu_controller (int enable)
{
	char value [PATH_MAX];
	int fd, i, ret;

	if (enable == 0)
	{
		get_service_state (SERVICE_PNPMGR, default_pnpmgr, UNKNOWN);
		get_service_state (SERVICE_THERMALD, default_thermald, UNKNOWN);
		get_service_state (SERVICE_MPDECISION, default_mpdecision, UNKNOWN);

		DM ("[init.svc.%s]: [%s]", SERVICE_PNPMGR, default_pnpmgr);
		DM ("[init.svc.%s]: [%s]", SERVICE_THERMALD, default_thermald);
		DM ("[init.svc.%s]: [%s]", SERVICE_MPDECISION, default_mpdecision);

		if (access (NV_HOTPLUG, F_OK) == 0)
		{
			if ((fd = open (NV_HOTPLUG, O_RDONLY)) < 0)
			{
				DM ("open %s: %s\n", NV_HOTPLUG, strerror (errno));
			}
			else
			{
				if (read (fd, default_nv_hotplug, sizeof (default_nv_hotplug) - 1) < 0)
				{
					DM ("read %s: %s\n", NV_HOTPLUG, strerror (errno));
				}
				close (fd);
				default_nv_hotplug [sizeof (default_nv_hotplug) - 1] = 0;
				choppy (default_nv_hotplug);
				DM ("read [%s]: [%s]", NV_HOTPLUG, default_nv_hotplug);
			}
		}

		if (access (MTK_HOTPLUG, F_OK) == 0)
		{
			if ((fd = open (MTK_HOTPLUG, O_RDONLY)) < 0)
			{
				DM ("open %s: %s\n", MTK_HOTPLUG, strerror (errno));
			}
			else
			{
				if (read (fd, default_mtk_hotplug, sizeof (default_mtk_hotplug) - 1) < 0)
				{
					DM ("read %s: %s\n", MTK_HOTPLUG, strerror (errno));
				}
				close (fd);
				default_mtk_hotplug [sizeof (default_mtk_hotplug) - 1] = 0;
				choppy (default_mtk_hotplug);
				DM ("read [%s]: [%s]", MTK_HOTPLUG, default_mtk_hotplug);
			}
		}

		if (strcmp (default_pnpmgr, RUNNING) == 0)
		{
			property_set ("ctl.stop", SERVICE_PNPMGR);
			wait_for_service (SERVICE_PNPMGR, STOPPED, 6);
			DM ("stop %s", SERVICE_PNPMGR);
		}

		if (strcmp (default_thermald, RUNNING) == 0)
		{
			property_set ("ctl.stop", SERVICE_THERMALD);
			wait_for_service (SERVICE_THERMALD, STOPPED, 6);
			DM ("stop %s", SERVICE_THERMALD);
		}

		if (strcmp (default_mpdecision, RUNNING) == 0)
		{
			property_set ("ctl.stop", SERVICE_MPDECISION);
			wait_for_service (SERVICE_MPDECISION, STOPPED, 6);
			DM ("stop %s", SERVICE_MPDECISION);
		}

		if (access (NV_HOTPLUG, F_OK) == 0)
		{
			if (default_nv_hotplug [0] != '0')
			{
				if ((fd = open (NV_HOTPLUG, O_WRONLY)) < 0)
				{
					DM ("open %s: %s\n", NV_HOTPLUG, strerror (errno));
				}
				else
				{
					if (write (fd, "0", 1) < 0)
					{
						DM ("write %s: %s\n", NV_HOTPLUG, strerror (errno));
					}
					close (fd);
					DM ("write [%s]: [0]", NV_HOTPLUG);
				}
			}
		}

		if (access (MTK_HOTPLUG, F_OK) == 0)
		{
			if (default_mtk_hotplug [0] != '0')
			{
				if ((fd = open (MTK_HOTPLUG, O_WRONLY)) < 0)
				{
					DM ("open %s: %s\n", MTK_HOTPLUG, strerror (errno));
				}
				else
				{
					if (write (fd, "0", 1) < 0)
					{
						DM ("write %s: %s\n", MTK_HOTPLUG, strerror (errno));
					}
					close (fd);
					DM ("write [%s]: [0]", MTK_HOTPLUG);
				}
			}
		}

		ret = delete_module (MODULE_CORE_CTL, O_NONBLOCK | O_EXCL);
		if (ret != 0)
		{
			DM ("delete_module %s failed: %s\n", MODULE_CORE_CTL, strerror (errno));
		}

		for (i = 0; i < get_cpu_count (); ++i)
		{
			/*
			 * force cpu online to enable all nodes
			 */
			set_cpu_online (i, 1);
		}

		for (i = 0; i < get_cpu_count (); ++i)
		{
			ret = get_cpu_governor (i, default_governors [i], sizeof (char) * MAX_GOVERNOR_BUFFER);

			if (ret < 0)
			{
				DM ("read cpu%d default governor failed.", i);
			}
			else
			{
				DM ("cpu%d default governor [%s].", i, default_governors [i]);
			}
		}

		/*
		* reset min/max frequencies
		*/
		for (i = 0; i < get_cpu_count (); ++i)
		{
			reset_cpu_frequency_range (i);
		}
	}
	else
	{
		for (i = 0; i < get_cpu_count (); ++i)
		{
			/*
			 * force cpu online to enable all nodes
			 */
			set_cpu_online (i, 1);
		}

		for (i = 0; i < get_cpu_count (); ++i)
		{
			if (default_governors [i] != NULL)
			{
				DM ("restore cpu%d governor [%s]", i, default_governors [i]);

				set_cpu_governor (i, default_governors [i]);
			}
		}

		snprintf (value, sizeof (value), "/system/lib/modules/%s.ko", MODULE_CORE_CTL);
		insmod (value);

		if (access (NV_HOTPLUG, F_OK) == 0)
		{
			if (default_nv_hotplug [0] != '0')
			{
				if ((fd = open (NV_HOTPLUG, O_WRONLY)) < 0)
				{
					DM ("open %s: %s\n", NV_HOTPLUG, strerror (errno));
				}
				else
				{
					if (write (fd, default_nv_hotplug, 1) < 0)
					{
						DM ("write %s: %s\n", NV_HOTPLUG, strerror (errno));
					}
					close (fd);
					DM ("write [%s]: [%s]", NV_HOTPLUG, default_nv_hotplug);
				}
			}
		}

		if (access (MTK_HOTPLUG, F_OK) == 0)
		{
			if (default_mtk_hotplug [0] != '0')
			{
				if ((fd = open (MTK_HOTPLUG, O_WRONLY)) < 0)
				{
					DM ("open %s: %s\n", MTK_HOTPLUG, strerror (errno));
				}
				else
				{
					if (write (fd, default_mtk_hotplug, 1) < 0)
					{
						DM ("write %s: %s\n", MTK_HOTPLUG, strerror (errno));
					}
					close (fd);
					DM ("write [%s]: [%s]", MTK_HOTPLUG, default_mtk_hotplug);
				}
			}
		}

		if (strcmp (default_pnpmgr, RUNNING) == 0)
		{
			property_set ("ctl.start", SERVICE_PNPMGR);
			DM ("start %s", SERVICE_PNPMGR);
		}

		if (strcmp (default_thermald, RUNNING) == 0)
		{
			property_set ("ctl.start", SERVICE_THERMALD);
			DM ("start %s", SERVICE_THERMALD);
		}

		if (strcmp (default_mpdecision, RUNNING) == 0)
		{
			property_set ("ctl.start", SERVICE_MPDECISION);
			DM ("start %s", SERVICE_MPDECISION);
		}
	}
}

typedef struct {
	int online;
	int prev_online;

	GLIST *frequencies;
	int frequencies_len;
	int frequency_idx;
	int prev_frequency_idx;

	GLIST *governors;
	int governors_len;
	int governor_idx;
} CPU;

static void *thread_main (void *UNUSED_VAR (null))
{
	CPU *cpus = NULL;
	int i, t;

	prctl (PR_SET_NAME, (unsigned long) "cputest:test", 0, 0, 0);

	enable_cpu_controller (0);

	cpus = malloc (sizeof (CPU) * get_cpu_count ());

	if (! cpus)
	{
		DM ("malloc cpu structures failed!\n");
		goto end;
	}

	for (i = 0; i < get_cpu_count (); i ++)
	{
		/*
		 * force cpu online to enable all nodes
		 */
		set_cpu_online (i, 1);

		cpus [i].online = (cpumodes [i] != CPUMODE_OFFLINE);
		cpus [i].prev_online = (cpumodes [i] != CPUMODE_OFFLINE);

		cpus [i].frequencies = get_cpu_available_frequencies (i);
		cpus [i].frequencies_len = glist_length (& cpus [i].frequencies);

		if (cpus [i].frequencies_len <= 0)
		{
			if (i == 0)
			{
				DM ("cpu0: no valid frequencies found!\n");
				goto end;
			}
			else
			{
				DM ("cpu%d: use the frequencies of cpu0.\n", i);

				cpus [i].frequencies = cpus [0].frequencies;
				cpus [i].frequencies_len = cpus [0].frequencies_len;
			}
		}

		cpus [i].governors = get_cpu_available_governors (i);
		cpus [i].governors_len = glist_length (& cpus [i].governors);

		if (cpus [i].governors_len <= 0)
		{
			if (i == 0)
			{
				DM ("cpu0: no valid governors found!\n");
				goto end;
			}
			else
			{
				DM ("cpu%d: use the governors of cpu0.\n", i);

				cpus [i].governors = cpus [0].governors;
				cpus [i].governors_len = cpus [0].governors_len;
			}
		}

		switch (testmode)
		{
		case TESTMODE_DESCEND:
			cpus [i].frequency_idx = cpus [i].frequencies_len - 1;
			break;
		case TESTMODE_RANDOM:
		case TESTMODE_CPURANDOM:
			cpus [i].frequency_idx = rand () % cpus [i].frequencies_len;
			cpus [i].prev_frequency_idx = cpus [i].frequency_idx;
			break;
		case TESTMODE_SYNCRANDOM:
			/*
			 * while this mode may not be valid, starting with index 0 to prevent exceptions.
			 */
		case TESTMODE_ASCEND:
		default:
			cpus [i].frequency_idx = 0;
			break;
		}

		if (cpus [i].online)
		{
			if (cpumodes [i] == CPUMODE_USER)
			{
				set_cpu_frequency (i, glist_get (& cpus [i].frequencies, cpus [i].frequency_idx));
			}
			else
			{
				set_cpu_governor (i, GOV_ONDEMAND);
			}
		}
		else
		{
			set_cpu_online (i, cpus [i].online);
		}
	}

	/* check if TESTMODE_SYNCRANDOM is valid */
	if (testmode == TESTMODE_SYNCRANDOM)
	{
		for (i = 1; i < get_cpu_count (); i ++)
		{
			if (cpus [i].frequencies_len != cpus [0].frequencies_len)
			{
				DM ("not all cpus have the same size of frequency table, change the test mode from [syncrandom] to [random]!\n");
				testmode = TESTMODE_RANDOM;
				break;
			}
		}
	}

	for (t = 0; ! done;)
	{
		if (t >= interval)
		{
			t = 0;

			for (i = 0; i < get_cpu_count (); i ++)
			{
				if (testmode != TESTMODE_CPURANDOM)
				{
					cpus [i].online = (cpumodes [i] != CPUMODE_OFFLINE);
				}

				switch (testmode)
				{
				case TESTMODE_DESCEND:
					cpus [i].frequency_idx --;

					if (cpus [i].frequency_idx < 0)
					{
						cpus [i].frequency_idx = cpus [i].frequencies_len - 1;
					}
					break;
				case TESTMODE_RANDOM:
					cpus [i].frequency_idx = rand () % cpus [i].frequencies_len;
					break;
				case TESTMODE_SYNCRANDOM:
					if (i == 0)
					{
						cpus [i].frequency_idx = rand () % cpus [i].frequencies_len;
					}
					else
					{
						cpus [i].frequency_idx = cpus [0].frequency_idx;
					}
					break;
				case TESTMODE_CPURANDOM:
					if (cpumodes [i] != CPUMODE_OFFLINE)
					{
						if(cpus [i].prev_online != cpus [i].online)
						{
							DM ("CPURandom[E]cpu%d: Failed CPU status change. CPU status: Prev[%d], Now[%d].\n", i, cpus [i].prev_online, cpus [i].online);
						}

						// Config next CPU status
						int nextOnline = rand () % 2;

						if(i == 0) // For cpu0 always ON
						{
							nextOnline = 1;
						}

						if (cpus [i].online != nextOnline)
						{
							cpus [i].online = nextOnline;

							if (cpus [i].online)	// Off -> On
							{
								cpus [i].frequency_idx = rand () % cpus [i].frequencies_len;
							}
							else 					// On -> Off
							{
								// To compare CPU frequency state
								compare_cpu_frequency (i, glist_get (& cpus [i].frequencies, cpus [i].frequency_idx));
							}
						}
						else
						{
							if (cpus [i].online) 	// On -> On
							{
								cpus [i].prev_frequency_idx = cpus [i].frequency_idx;
								cpus [i].frequency_idx = rand () % cpus [i].frequencies_len;

								// To compare CPU frequency state
								compare_cpu_frequency (i, glist_get (& cpus [i].frequencies, cpus [i].prev_frequency_idx));
							}
							else 					// Off -> Off
							{
								// Do nothing
							}
						}

						cpus [i].prev_online = cpus [i].online;
					}
					break;
				case TESTMODE_ASCEND:
				default:
					cpus [i].frequency_idx ++;

					if (cpus [i].frequency_idx >= cpus [i].frequencies_len)
					{
						cpus [i].frequency_idx = 0;
					}
					break;
				}

				if (cpus [i].online)
				{
					if (cpumodes [i] == CPUMODE_USER)
					{
						set_cpu_frequency (i, glist_get (& cpus [i].frequencies, cpus [i].frequency_idx));
					}
					else
					{
						set_cpu_governor (i, GOV_ONDEMAND);
					}
				}
				else
				{
					set_cpu_online (i, cpus [i].online);
				}
			}
			continue;
		}

		if (interval <= 1000)
		{
			usleep (interval * 1000);
			t = interval;
		}
		else
		{
			int s = interval - t;

			if (s > 1000)
				s = 1000;

			usleep (s * 1000);

			t += s;
		}
	}

end:;
	if (cpus)
	{
		void *cpu0_freqs = cpus [0].frequencies;
		void *cpu0_govers = cpus [0].governors;

		for (i = 0; i < get_cpu_count (); i ++)
		{
			set_cpu_governor (i, GOV_ONDEMAND);

			if ((cpus [i].frequencies) && ((i == 0) || (cpus [i].frequencies != cpu0_freqs)))
			{
				glist_clear (& cpus [i].frequencies, free);
			}

			if ((cpus [i].governors) && ((i == 0) || (cpus [i].governors != cpu0_govers)))
			{
				glist_clear (& cpus [i].governors, free);
			}
		}
		free (cpus);
	}
	enable_cpu_controller (1);
	return NULL;
}

int cputest_main (int server_socket)
{
	pthread_t working = (pthread_t) -1;

	char buffer [PATH_MAX + 16];
	char tmp [PATH_MAX + 16];
	int commfd = -1;
	int ret = 0;
	int i = 0;

	srand (time (NULL));

	cpumodes = malloc (sizeof (char) * get_cpu_count () + 1);

	if (! cpumodes)
	{
		DM ("malloc cpu modes failed!\n");
		return 0;
	}

	memset (cpumodes, CPUMODE_USER, get_cpu_count ());
	cpumodes [get_cpu_count ()] = 0;

	default_pnpmgr = malloc (sizeof (char) * PROPERTY_VALUE_MAX);
	if (default_pnpmgr == NULL)
	{
		free (cpumodes);
		DM ("malloc failed: %s\n", strerror (errno));
		return 0;
	}

	default_thermald = malloc (sizeof (char) * PROPERTY_VALUE_MAX);
	if (default_thermald == NULL)
	{
		free (cpumodes);
		free (default_pnpmgr);
		DM ("malloc failed: %s\n", strerror (errno));
		return 0;
	}

	default_mpdecision = malloc (sizeof (char) * PROPERTY_VALUE_MAX);
	if (default_mpdecision == NULL)
	{
		free (cpumodes);
		free (default_pnpmgr);
		free (default_thermald);
		DM ("malloc failed: %s\n", strerror (errno));
		return 0;
	}

	default_governors = malloc (sizeof (char*) * get_cpu_count ());
	if (default_governors == NULL)
	{
		free (cpumodes);
		free (default_pnpmgr);
		free (default_thermald);
		free (default_mpdecision);
		DM ("malloc failed: %s\n", strerror (errno));
		return 0;
	}

	for (i = 0; i < get_cpu_count (); ++i)
	{
		default_governors [i] = malloc (sizeof (char) * MAX_GOVERNOR_BUFFER);

		memset (default_governors [i], 0, sizeof (char) * MAX_GOVERNOR_BUFFER);

		if (default_governors [i] == NULL)
		{
			free (cpumodes);
			free (default_pnpmgr);
			free (default_thermald);
			free (default_mpdecision);
			for (; i > 0; --i)
			{
				free (default_governors [i - 1]);
			}
			free (default_governors);
			DM ("malloc failed: %s\n", strerror (errno));
			return 0;
		}
	}

	while (! done)
	{
		DM ("waiting connection ...\n");

		commfd = wait_for_connection (server_socket);

		if (commfd < 0)
		{
			DM ("accept client connection failed!\n");
			continue;
		}

		DM ("connection established.\n");

		for (;;)
		{
			memset (buffer, 0, sizeof (buffer));

			ret = read (commfd, buffer, sizeof (buffer));

			if (ret <= 0)
			{
				DM ("read command error (%d)! close connection!\n", ret);
				break;
			}

			buffer [sizeof (buffer) - 1] = 0;

			ret = 0;

			DM ("read command [%s].\n", buffer);

			if (CMP_CMD (buffer, CMD_ENDSERVER))
			{
				pthread_mutex_lock (& data_lock);
				done = 1;
				pthread_mutex_unlock (& data_lock);
				break;
			}
			if (CMP_CMD (buffer, CMD_GETVER))
			{
				strcpy (buffer, VERSION);
				ret = 1;
			}
			else if (CMP_CMD (buffer, CMD_ISRUNNING))
			{
				ret = is_thread_alive (working);
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, CMD_GETCPUCOUNT))
			{
				ret = get_cpu_count ();
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, CMD_GETINTERVAL))
			{
				ret = interval;
				if (ret == 1) sprintf (buffer, "%d", ret);
			}
			else if (CMP_CMD (buffer, CMD_SETINTERVAL))
			{
				/* change log interval */
				MAKE_DATA (buffer, CMD_SETINTERVAL);

				ret = atoi (buffer);

				if (ret > 0)
				{
					pthread_mutex_lock (& data_lock);
					interval = ret;
					pthread_mutex_unlock (& data_lock);
					ret = 0;
				}
				else
				{
					DM ("bad interval value (%d)!\n", ret);
					ret = -1;
				}
			}
			else if (CMP_CMD (buffer, CMD_GETTESTMODE))
			{
				switch (testmode)
				{
				case TESTMODE_ASCEND:
					snprintf (buffer, sizeof (buffer), "%d:ascend", testmode);
					break;
				case TESTMODE_DESCEND:
					snprintf (buffer, sizeof (buffer), "%d:descend", testmode);
					break;
				case TESTMODE_RANDOM:
					snprintf (buffer, sizeof (buffer), "%d:random", testmode);
					break;
				case TESTMODE_SYNCRANDOM:
					snprintf (buffer, sizeof (buffer), "%d:syncrandom", testmode);
					break;
				case TESTMODE_CPURANDOM:
					snprintf (buffer, sizeof (buffer), "%d:cpurandom", testmode);
					break;
				default:
					snprintf (buffer, sizeof (buffer), "%d:unknown", testmode);
					break;
				}
				buffer [sizeof (buffer) - 1] = 0;
				ret = 1;
			}
			else if (CMP_CMD (buffer, CMD_SETTESTMODE))
			{
				/* change test mode */
				MAKE_DATA (buffer, CMD_SETTESTMODE);

				if (strcmp (buffer, "ascend") == 0)
				{
					ret = TESTMODE_ASCEND;
				}
				else if (strcmp (buffer, "descend") == 0)
				{
					ret = TESTMODE_DESCEND;
				}
				else if (strcmp (buffer, "random") == 0)
				{
					ret = TESTMODE_RANDOM;
				}
				else if (strcmp (buffer, "syncrandom") == 0)
				{
					ret = TESTMODE_SYNCRANDOM;
				}
				else if (strcmp (buffer, "cpurandom") == 0)
				{
					ret = TESTMODE_CPURANDOM;
				}
				else
				{
					char *p = strchr (buffer, ':');
					if (p) *p = 0;
					ret = atoi (buffer);
				}

				if ((ret >= 0) && (ret < TESTMODE_COUNT))
				{
					pthread_mutex_lock (& data_lock);
					testmode = ret;
					pthread_mutex_unlock (& data_lock);
					ret = 0;
				}
				else
				{
					DM ("bad testmode value (%d)!\n", ret);
					ret = -1;
				}
			}
			else if (CMP_CMD (buffer, CMD_GETCPUMODES))
			{
				snprintf (buffer, sizeof (buffer), "%s", cpumodes);
				buffer [sizeof (buffer) - 1] = 0;
				ret = 1;
			}
			else if (CMP_CMD (buffer, CMD_SETCPUMODES))
			{
				/* change cpu modes */
				MAKE_DATA (buffer, CMD_SETCPUMODES);

				snprintf (cpumodes, get_cpu_count () + 1, "%s", buffer);
				cpumodes [get_cpu_count ()] = 0;

				if (cpumodes [0] == CPUMODE_OFFLINE)
				{
					/* cpu0 should be always on */
					cpumodes [0] = CPUMODE_USER;
				}

				DM ("set cpu modes = [%s]\n", cpumodes);
			}
			else if (CMP_CMD (buffer, CMD_RUN))
			{
				/* start log */
				if (! is_thread_alive (working))
				{
					if (pthread_create (& working, NULL, thread_main, NULL) < 0)
						ret = -1;
				}
			}
			else if (CMP_CMD (buffer, CMD_STOP))
			{
				if (is_thread_alive (working))
				{
					/* quit thread */
					//pthread_cancel (working);
					pthread_mutex_lock (& data_lock);
					done = 1;
					pthread_mutex_unlock (& data_lock);
					pthread_join (working, NULL);
					working = (pthread_t) -1;
					done = 0;
				}
			}
			else if (CMP_CMD (buffer, CMD_GETCTRLSTATUS))
			{
				snprintf (buffer, sizeof (buffer), "%d", get_cpu_controller_status ());
				buffer [sizeof (buffer) - 1] = 0;
				ret = 1;
			}
			else if (CMP_CMD (buffer, CMD_SETCTRLSTATUS))
			{
				if (is_thread_alive (working))
				{
					DM ("do not allow to change cpu controller status while dvfs test running!");
					ret = -1;
				}
				else
				{
					MAKE_DATA (buffer, CMD_SETCTRLSTATUS);

					enable_cpu_controller ((buffer [0] == '1') ? 1 : 0);
				}
			}
			else if (CMP_CMD (buffer, CMD_GETAVAILFREQS))
			{
				if (is_thread_alive (working))
				{
					DM ("force reading cpu0 frequencies while dvfs test running!");

					ret = read_cpu_node (node_available_frequencies, 0, buffer, sizeof (buffer));
				}
				else
				{
					MAKE_DATA (buffer, CMD_GETAVAILFREQS);

					ret = atoi (buffer);

					if ((ret < 0) || (ret >= get_cpu_count ()))
						ret = 0;

					set_cpu_online (ret, 1);

					ret = read_cpu_node (node_available_frequencies, ret, buffer, sizeof (buffer));
				}

				if (ret < 0)
				{
					ret = read_cpu_node (node_available_frequencies, 0, buffer, sizeof (buffer));

					if (ret < 0)
					{
						buffer [0] = ' ';
						buffer [1] = 0;
					}
				}
				ret = 1;
			}
			else if (CMP_CMD (buffer, CMD_SETCPUMODE))
			{
				MAKE_DATA (buffer, CMD_SETCPUMODE);

				datatok (buffer, tmp);
				i = atoi (tmp);
				datatok (buffer, tmp);

				if ((i < 0) || (i >= get_cpu_count ()))
					i = 0;

				cpumodes [i] = tmp [0];
				ret = 0;

				switch (cpumodes [i])
				{
				case CPUMODE_OFFLINE:
					ret = set_cpu_online (i, 0);
					break;
				case CPUMODE_DYNAMIC:
					ret = set_cpu_governor (i, GOV_ONDEMAND);
					break;
				default:
					cpumodes [i] = CPUMODE_USER;
				case CPUMODE_USER:
					ret = set_cpu_governor (i, GOV_USERSPACE);
					break;
				}

				DM ("set cpu [%d] mode = [%c]\n", i, cpumodes [i]);
			}
			else if (CMP_CMD (buffer, CMD_SETCPUFREQ))
			{
				MAKE_DATA (buffer, CMD_SETCPUFREQ);

				datatok (buffer, tmp);
				ret = atoi (tmp);
				datatok (buffer, tmp);

				if ((ret < 0) || (ret >= get_cpu_count ()))
					ret = 0;

				set_cpu_frequency (ret, tmp);

				DM ("set cpu [%d] freq = [%s]\n", ret, tmp);
				ret = 0;
			}
			else
			{
				DM ("unknown command [%s]!\n", buffer);
				ret = -1;
			}

			/* command response */
			if (ret != 1)
				sprintf (buffer, "%d", ret);

			DM ("send response [%s].\n", buffer);

			if (write (commfd, buffer, strlen (buffer)) != (ssize_t) strlen (buffer))
			{
				DM ("send response [%s] to client failed!\n", buffer);
			}
		}

		close (commfd);
		commfd = -1;
	}

	if (is_thread_alive (working))
		pthread_join (working, NULL);

	free (cpumodes);
	free (default_pnpmgr);
	free (default_thermald);
	free (default_mpdecision);
	for (i = 0; i < get_cpu_count (); ++i)
	{
		free (default_governors [i]);
	}
	free (default_governors);

	/* reset done flag */
	done = 0;

	return 0;
}
