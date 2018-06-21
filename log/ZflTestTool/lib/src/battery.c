#define	LOG_TAG	"STT:battery"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>

#include <cutils/log.h>

#include "libcommon.h"
#include "headers/fio.h"
#include "headers/battery.h"

typedef unsigned int uint32;
typedef int sint32;

/*
 * main structure for storing battery info
 */
static BATT_INFO htc_batt_info;

static void batt_add_i (BATT_FIELD **ppf, const char *name, int data)
{
	(*ppf)->type = BATT_FT_INT;
	strncpy ((*ppf)->name, name, BATT_NAME_LEN - 1);
	(*ppf)->name [BATT_NAME_LEN - 1] = 0;
	(*ppf)->data.i = data;
	(*ppf) ++;
}

static void batt_add_f (BATT_FIELD **ppf, const char *name, float data)
{
	(*ppf)->type = BATT_FT_FLOAT;
	strncpy ((*ppf)->name, name, BATT_NAME_LEN - 1);
	(*ppf)->name [BATT_NAME_LEN - 1] = 0;
	(*ppf)->data.f = data;
	(*ppf) ++;
}

static void batt_add_s (BATT_FIELD **ppf, const char *name, const char *data)
{
	(*ppf)->type = BATT_FT_STRING;
	strncpy ((*ppf)->name, name, BATT_NAME_LEN - 1);
	(*ppf)->name [BATT_NAME_LEN - 1] = 0;
	strncpy ((*ppf)->data.s, data, BATT_NAME_LEN - 1);
	(*ppf)->data.s [BATT_NAME_LEN - 1] = 0;
	(*ppf) ++;
}

static void batt_debug_dump (BATT_INFO *pbi)
{
	int i;

	if ((! pbi) || (pbi->count == 0))
		return;

	fLOGD ("battery info count = %d", pbi->count);

	for (i = 0; i < pbi->count; i ++)
	{
		switch (pbi->fields [i].type)
		{
		case BATT_FT_INT:
			fLOGD ("  %02d: %s = %d", i, pbi->fields [i].name, pbi->fields [i].data.i);
			break;
		case BATT_FT_FLOAT:
			fLOGD ("  %02d: %s = %f", i, pbi->fields [i].name, pbi->fields [i].data.f);
			break;
		case BATT_FT_STRING:
			fLOGD ("  %02d: %s = %s", i, pbi->fields [i].name, pbi->fields [i].data.s);
			break;
		default:
			fLOGD ("  %02d: %s = %p", i, pbi->fields [i].name, & pbi->fields [i].data);
		}
	}
}

static const char *get_charger_name (int idx)
{
	static const char *chargers [] = {
		"NONE",	// 0
		"USB",	// 1
		"AC"	// 2
	};
	if (idx < 0 || idx > 2) idx = 0;
	return chargers [idx];
}

static const char *get_charge_mode (int idx)
{
	static const char *modes [] = {
		"Disabled",	// 0
		"Slow",		// 1
		"Fast"		// 2
	};
	if (idx < 0 || idx > 2) idx = 0;
	return modes [idx];
}

/*
 * rpc common
 */
#define	HTC_PROCEDURE_BATTERY_NULL		0
#define	HTC_PROCEDURE_GET_BATT_LEVEL		1
#define	HTC_PROCEDURE_GET_BATT_INFO		2
#define	HTC_PROCEDURE_GET_CABLE_STATUS		3
#define	HTC_PROCEDURE_SET_BATT_DELTA		4
#define	HTC_PROCEDURE_GET_BATT_FULL_INFO	5
#define	HTC_PROCEDURE_GET_BATT_COULOMB_COUNTER	8

struct rpc_request_hdr
{
	unsigned int xid;
	unsigned int type;	/* 0 */
	unsigned int rpc_vers;	/* 2 */
	unsigned int prog;
	unsigned int vers;
	unsigned int procedure;
	unsigned int cred_flavor;
	unsigned int cred_length;
	unsigned int verf_flavor;
	unsigned int verf_length;
};

static struct rpc_request_hdr req;

typedef struct {
	unsigned int verf_flavor;
	unsigned int verf_length;
	unsigned int accept_stat;
	#define	RPC_ACCEPTSTAT_SUCCESS		0
	#define	RPC_ACCEPTSTAT_PROG_UNAVAIL	1
	#define	RPC_ACCEPTSTAT_PROG_MISMATCH	2
	#define	RPC_ACCEPTSTAT_PROC_UNAVAIL	3
	#define	RPC_ACCEPTSTAT_GARBAGE_ARGS	4
	#define	RPC_ACCEPTSTAT_SYSTEM_ERR	5
	#define	RPC_ACCEPTSTAT_PROG_LOCKED	6
} rpc_accepted_reply_hdr;

typedef struct {
} rpc_denied_reply_hdr;

struct rpc_reply_hdr
{
	unsigned int xid;
	unsigned int type;
	unsigned int reply_stat;
	#define	RPCMSG_REPLYSTAT_ACCEPTED	0
	#define	RPCMSG_REPLYSTAT_DENIED		1
	union
	{
		rpc_accepted_reply_hdr	acc_hdr;
		rpc_denied_reply_hdr	dny_hdr;
	}
	data;
};

static void setup_header (struct rpc_request_hdr *hdr, unsigned int prog, unsigned int vers, unsigned int proc)
{
	static unsigned int next_xid = 0;

	memset (hdr, 0, sizeof (struct rpc_request_hdr));

	if (50000 < next_xid) // to prevent the overflow of unsigned int next_xid
		next_xid = 0;

	hdr->xid = htonl (++ next_xid);
	hdr->rpc_vers = htonl (2);
	hdr->prog = htonl (prog);
	hdr->vers = htonl (vers);
	hdr->procedure = htonl (proc);
}

/*
 * rpc 30100001:00000000:5
 */
struct battery_info_reply_30100001_00000000_5
{
	uint32 batt_id;		// Battery ID from ADC
	uint32 batt_vol;	// Battery voltage from ADC
	uint32 batt_temp;	// Battery temperature from ADC
	uint32 batt_current;	// Battery current from ADC
	uint32 VREF_2;		// PM_sADC_MUXSEL12 from formula & ADC
	uint32 VREF;		// PM_sADC_MUXSEL13 from formula & ADC
	uint32 ADC4096_VREF;	// ADC calibration formula
	uint32 Rtemp;		// Temperature (Kohm) from formula
	uint32 Temp;		// Temperature (C) from formula
	uint32 pd_M;		// formula
	uint32 MBAT_pd;		// batt_vol + pd_M
	uint32 pd_temp;		// formula
	uint32 percent_last;	// last % from formula
	uint32 percent_update;	// update %
	uint32 dis_percent;	// Discharge % (First time)
	uint32 vbus;		// VBUS => 0: Not, 1: In
	uint32 usbid;		// USB-ID => 0: AC, 1: USB
	uint32 charging_source;	// 0: Cable out, 1: USB, 2: AC
	uint32 MBAT_IN;		// MBAT_IN => 0: No battery or battery fault, 1: Battery docking
	uint32 full_bat;	// Full capacity of battery (mAh)
	uint32 eval_current;	// mA
	uint32 charging_enabled;// 0: Disable, 1: Slow charge, 2: Fast charge
	uint32 timeout;		// Check timer
	uint32 fullcharge;	// Full charge counter
	uint32 level;		// % for Arm11 show
	uint32 delta;		// Delta
};

struct htc_get_batt_info_rep_30100001_00000000_5
{
	struct rpc_reply_hdr hdr;
	struct battery_info_reply_30100001_00000000_5 info;
};

static BATT_INFO *get_battery_info_rpc_30100001_00000000_full_info ()
{
	fLOGV ("get battery info via rpc 30100001_00000000_5");

	struct htc_get_batt_info_rep_30100001_00000000_5 *rep;

	const char dev [] = "/dev/oncrpc/30100001:00000000";
	BATT_FIELD *ptr;
	char buf [512];
	int fd, count;

	fd = open_nointr (dev, O_RDWR, 0);

	if (fd < 0)
	{
		fLOGE ("cannot open_nointr %s: %s", dev, strerror (errno));
		return NULL;
	}

	setup_header (& req, 0x30100001, 0x00000000, HTC_PROCEDURE_GET_BATT_FULL_INFO /* 5 */);

	if ((count = write_nointr (fd, & req, sizeof (req))) < 0)
	{
		fLOGE ("write_nointr rpc failed: %s", strerror (errno));
		close_nointr (fd);
		return NULL;
	}
	//fLOGD ("rpc write_nointr %d bytes", count);

	memset (buf, 0, sizeof (buf));

	if ((count = read_nointr (fd, buf, sizeof (buf))) < 0)
	{
		fLOGE ("read_nointr rpc failed: %s", strerror (errno));
		close_nointr (fd);
		return NULL;
	}
	//fLOGD ("rpc read_nointr %d bytes", count);

	close_nointr (fd);

	if (count == sizeof (struct rpc_reply_hdr))
	{
		/* only reply header, no data */
		htc_batt_info.count = 0;
		return & htc_batt_info;
	}

	rep = (struct htc_get_batt_info_rep_30100001_00000000_5 *) buf;

	ptr = & htc_batt_info.fields [0];
	batt_add_i (& ptr, "P",				ntohl (rep->info.percent_update));
	batt_add_i (& ptr, "RP",			ntohl (rep->info.percent_last));
	batt_add_f (& ptr, "V_MBAT (V)",		((float) ntohl (rep->info.batt_vol)) / 1000.00f); // divide 1000 to mV.
	batt_add_f (& ptr, "Main Battery ID",		(float) ntohl (rep->info.batt_id));
	batt_add_i (& ptr, "pd_M",			ntohl (rep->info.pd_M));
	batt_add_f (& ptr, "Temp From ADC (C)",		((float) ((int) ntohl (rep->info.Temp))) / 10.00f); // divide 10 to degree C.
	batt_add_i (& ptr, "Charge Current (mA)",	ntohl (rep->info.batt_current));
	batt_add_i (& ptr, "Full (mAh)",		ntohl (rep->info.full_bat) / 1000); // divide 1000 to mA.
	batt_add_i (& ptr, "1st Dis_percentage (%)",	ntohl (rep->info.dis_percent));
	batt_add_i (& ptr, "Estimated Current (mA)",	ntohl (rep->info.eval_current));
	batt_add_s (& ptr, "Charger Type",		get_charger_name (ntohl (rep->info.charging_source)));
	batt_add_i (& ptr, "Charging",			ntohl (rep->info.charging_enabled));
	htc_batt_info.count = ((unsigned long) ptr - (unsigned long) & htc_batt_info.fields [0]) / sizeof (BATT_FIELD);

	/*
	fLOGI ("==Battery Info== PU:%d, PL:%d, BV:%d, BI:%d, pd_M:%d, tmp:%d, BC:%d, FB:%d, DP:%d, EC:%d, CS:%d, CE:%d\n",
		ntohl (rep->info.percent_update),
		ntohl (rep->info.percent_last),
		ntohl (rep->info.batt_vol),
		ntohl (rep->info.batt_id),
		ntohl (rep->info.pd_M),
		ntohl (rep->info.Temp),
		ntohl (rep->info.batt_current),
		ntohl (rep->info.full_bat),
		ntohl (rep->info.dis_percent),
		ntohl (rep->info.eval_current),
		ntohl (rep->info.charging_source),
		ntohl (rep->info.charging_enabled));
	*/

	return & htc_batt_info;
}

/*
 * rpc 30100001:00000000:8
 */
struct battery_info_reply_30100001_00000000_8
{
	uint32 first_mbi;
	uint32 percent_update;
	uint32 percent_last;
	sint32 VCBI;
	uint32 CCBI;
	uint32 Vmis;
	uint32 Vi;
	sint32 Temp;
	uint32 batt_id;
	uint32 eval_current;
	uint32 batt_current;
	uint32 charging_source;
	uint32 T;
	uint32 FT;
	uint32 percent_ini;
	uint32 Vth;
	uint32 deltaV;
	uint32 percent_temp;
	uint32 ACRL;
	uint32 ACRt;
	uint32 AUC;
	uint32 FL;
	uint32 AEL;
	uint32 Qc;
	uint32 ADC_VREF;
};

struct htc_get_batt_info_rep_30100001_00000000_8
{
	struct rpc_reply_hdr hdr;
	struct battery_info_reply_30100001_00000000_8 info;
};

static BATT_INFO *get_battery_info_rpc_30100001_00000000_coulomb_counter ()
{
	fLOGV ("get battery info via rpc 30100001_00000000_8");

	struct htc_get_batt_info_rep_30100001_00000000_8 *rep;

	const char dev [] = "/dev/oncrpc/30100001:00000000";
	BATT_FIELD *ptr;
	char buf [512];
	int fd, count;

	fd = open_nointr (dev, O_RDWR, 0);

	if (fd < 0)
	{
		fLOGE ("cannot open_nointr %s: %s", dev, strerror (errno));
		return NULL;
	}

	setup_header (& req, 0x30100001, 0x00000000, HTC_PROCEDURE_GET_BATT_COULOMB_COUNTER /* 8 */);

	if ((count = write_nointr (fd, & req, sizeof (req))) < 0)
	{
		fLOGE ("write_nointr rpc failed: %s", strerror (errno));
		close_nointr (fd);
		return NULL;
	}
	//fLOGD ("rpc write_nointr %d bytes", count);

	memset (buf, 0, sizeof (buf));

	if ((count = read_nointr (fd, buf, sizeof (buf))) < 0)
	{
		fLOGE ("read_nointr rpc failed: %s", strerror (errno));
		close_nointr (fd);
		return NULL;
	}
	//fLOGD ("rpc read_nointr %d bytes", count);

	close_nointr (fd);

	if (count == sizeof (struct rpc_reply_hdr))
	{
		/* only reply header, no data */
		htc_batt_info.count = 0;
		return & htc_batt_info;
	}

	rep = (struct htc_get_batt_info_rep_30100001_00000000_8 *) buf;
	//fLOGD ("  xid = [%u], reply_stat = [%u], verf flavor = [%u], verf length = [%u], accept_stat = [%u]", rep->hdr.xid, rep->hdr.reply_stat, rep->hdr.data.acc_hdr.verf_flavor, rep->hdr.data.acc_hdr.verf_length, rep->hdr.data.acc_hdr.accept_stat);

	ptr = & htc_batt_info.fields [0];
	batt_add_i (& ptr, "first mbi",		ntohl (rep->info.first_mbi));
	batt_add_i (& ptr, "percent update",	ntohl (rep->info.percent_update));
	batt_add_i (& ptr, "percent last",	ntohl (rep->info.percent_last));
	batt_add_i (& ptr, "VCBI",		ntohl (rep->info.VCBI));
	batt_add_i (& ptr, "CCBI",		ntohl (rep->info.CCBI));
	batt_add_i (& ptr, "Vmis",		ntohl (rep->info.Vmis));
	batt_add_i (& ptr, "Vi",		ntohl (rep->info.Vi));
	batt_add_f (& ptr, "Temp (C)",		((float) ((int) ntohl (rep->info.Temp))) / 10.00f); // divide 10 to degree C.
	batt_add_f (& ptr, "batt id",		(float) ntohl (rep->info.batt_id));
	batt_add_i (& ptr, "eval current (mA)",	ntohl (rep->info.eval_current));
	batt_add_i (& ptr, "batt current (mA)",	ntohl (rep->info.batt_current));
	batt_add_s (& ptr, "charging source",	get_charger_name (ntohl (rep->info.charging_source)));
	batt_add_i (& ptr, "T",			ntohl (rep->info.T));
	batt_add_i (& ptr, "FT",		ntohl (rep->info.FT));
	batt_add_i (& ptr, "percent_ini",	ntohl (rep->info.percent_ini));
	batt_add_i (& ptr, "Vth",		ntohl (rep->info.Vth));
	batt_add_i (& ptr, "deltaV",		ntohl (rep->info.deltaV));
	batt_add_i (& ptr, "percent_temp",	ntohl (rep->info.percent_temp));
	batt_add_i (& ptr, "ACRL",		ntohl (rep->info.ACRL));
	batt_add_i (& ptr, "ACRt",		ntohl (rep->info.ACRt));
	batt_add_i (& ptr, "AUC",		ntohl (rep->info.AUC));
	batt_add_i (& ptr, "FL",		ntohl (rep->info.FL));
	batt_add_i (& ptr, "AEL",		ntohl (rep->info.AEL));
	batt_add_i (& ptr, "Qc",		ntohl (rep->info.Qc));
	batt_add_i (& ptr, "ADC_VREF",		ntohl (rep->info.ADC_VREF));
	htc_batt_info.count = ((unsigned long) ptr - (unsigned long) & htc_batt_info.fields [0]) / sizeof (BATT_FIELD);

	/*
	fLOGI ("==Battery Info== %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
		ntohl (rep->info.first_mbi),
		ntohl (rep->info.percent_update),
		ntohl (rep->info.percent_last),
		ntohl (rep->info.VCBI),
		ntohl (rep->info.CCBI),
		ntohl (rep->info.Vmis),
		ntohl (rep->info.Vi),
		ntohl (rep->info.Temp),
		ntohl (rep->info.batt_id),
		ntohl (rep->info.eval_current),
		ntohl (rep->info.batt_current),
		ntohl (rep->info.charging_source),
		ntohl (rep->info.T),
		ntohl (rep->info.FT),
		ntohl (rep->info.percent_ini),
		ntohl (rep->info.Vth),
		ntohl (rep->info.deltaV),
		ntohl (rep->info.percent_temp),
		ntohl (rep->info.ACRL),
		ntohl (rep->info.ACRt),
		ntohl (rep->info.AUC),
		ntohl (rep->info.FL),
		ntohl (rep->info.AEL),
		ntohl (rep->info.Qc),
		ntohl (rep->info.ADC_VREF));
	*/

	return & htc_batt_info;
}

/*
 * smem raw
 */
struct htc_batt_smem_raw
{
	uint32 batt_id;
	uint32 batt_vol;
	uint32 batt_vol_last;
	uint32 batt_temp;
	uint32 batt_current;
	uint32 batt_current_last;
	uint32 batt_discharge_current;

	uint32 VREF_2;
	uint32 VREF;
	uint32 ADC4096_VREF;

	uint32 Rtemp;
	sint32 Temp;
	sint32 Temp_last;

	uint32 pd_M;
	uint32 MBAT_pd;
	sint32 I_MBAT;

	uint32 pd_temp;
	uint32 percent_last;
	uint32 percent_update;
	uint32 dis_percent;

	uint32 vbus;
	uint32 usbid;
	uint32 charging_source;

	uint32 MBAT_IN;
	uint32 full_bat;

	uint32 eval_current;
	uint32 eval_current_last;
	uint32 charging_enabled;

	uint32 timeout;
	uint32 fullcharge;
	uint32 level;
	uint32 delta;

	uint32 chg_time;
	sint32 level_change;
	uint32 sleep_timer_count;
	uint32 OT_led_on;
	uint32 overloading_charge;

	uint32 reserve1;
	uint32 reserve2;
	uint32 reserve3;
	uint32 reserve4;
	uint32 reserve5;
};

static BATT_INFO *get_battery_info_smem_raw ()
{
	fLOGV ("get battery info from smem");

	const char dev [] = "/sys/class/power_supply/battery/smem_raw";
	BATT_FIELD *ptr;
	struct htc_batt_smem_raw buf;
	size_t size;
	int count;

	int fd = open_nointr (dev, O_RDONLY, 0);

	if (fd == -1)
	{
		fLOGE ("cannot open_nointr %s: %s", dev, strerror (errno));
		return NULL;
	}

	size = sizeof (struct htc_batt_smem_raw);
	count = read_nointr (fd, & buf, size);
	close_nointr (fd);

	if (count <= 0)
	{
		if (count < 0)
		{
			fLOGE ("read_nointr smem failed: %s", strerror (errno));
		}
		else
		{
			fLOGE ("read_nointr nothing from smem!");
		}
		return NULL;
	}

	ptr = & htc_batt_info.fields [0];
	batt_add_i (& ptr, "P",				buf.percent_update);
	batt_add_i (& ptr, "RP",			buf.percent_last);
	batt_add_f (& ptr, "V_MBAT (V)",		((float) buf.batt_vol) / 1000.00f); // divide 1000 to mV.
	batt_add_f (& ptr, "Main Battery ID",		(float) buf.batt_id);
	batt_add_i (& ptr, "pd_M",			buf.pd_M);
	//batt_add_f (& ptr, "Temp From ADC (\u00B0C)",	((float) ((int) buf.Temp)) / 10.00f); // divide 10 to degree C.
	batt_add_f (& ptr, "Temp From ADC (C)",		((float) ((int) buf.Temp)) / 10.00f); // divide 10 to degree C.
	batt_add_i (& ptr, "Charge Current (mA)",	buf.batt_current);
	batt_add_i (& ptr, "Full (mAh)",		buf.full_bat / 1000); // divide 1000 to mA.
	batt_add_i (& ptr, "1st Dis_percentage (%)",	buf.dis_percent);
	batt_add_i (& ptr, "Estimated Current (mA)",	buf.eval_current);
	batt_add_s (& ptr, "Charger Type",		get_charger_name (buf.charging_source));
	batt_add_i (& ptr, "Charging",			buf.charging_enabled);
	htc_batt_info.count = ((unsigned long) ptr - (unsigned long) & htc_batt_info.fields [0]) / sizeof (BATT_FIELD);

	/*
	fLOGI ("==Battery Info== PU:%d, PL:%d, BV:%d, BI:%d, pd_M:%d, tmp:%d, BC:%d, FB:%d, DP:%d, EC:%d, CS:%d, CE:%d\n",
		buf.percent_update,
		buf.percent_last,
		buf.batt_vol,
		buf.batt_id,
		buf.pd_M,
		buf.batt_temp,
		buf.batt_current,
		buf.full_bat,
		buf.dis_percent,
		buf.eval_current,
		buf.charging_source,
		buf.charging_enabled);
	*/

	return & htc_batt_info;
}

/*
 * smem text
 */
static BATT_INFO *get_battery_info_smem_text ()
{
	fLOGV ("get battery info from smem text");

	const char dev [] = "/sys/class/power_supply/battery/smem_text";
	BATT_FIELD *ptr;
	FILE *fp;
	char title [BATT_NAME_LEN];
	char value [BATT_NAME_LEN];
	char line [BATT_NAME_LEN << 1];
	int count;
	char *p;

	fp = fopen_nointr (dev, "r");

	if (! fp)
	{
		fLOGE ("cannot fopen_nointr %s: %s", dev, strerror (errno));
		return NULL;
	}

	ptr = & htc_batt_info.fields [0];
	count = 0;

	while ((fgets (line, sizeof (line), fp) != NULL) && (count < BATT_FIELD_MAX))
	{
		memset (title, 0, sizeof (title));
		memset (value, 0, sizeof (value));

		p = strchr (line, ':');

		if (! p)
		{
			fLOGE ("invalid line in smem_text: [%s]", line);
			break;
		}

		*p = 0;

		strncpy (title, line, BATT_NAME_LEN - 1);

		for (p ++; *p && isspace (*p); p ++);

		strncpy (value, p, BATT_NAME_LEN - 1);

		if ((p = strrchr (value, '\n')) != NULL) *p = 0;
		if ((p = strrchr (value, '\r')) != NULL) *p = 0;

		batt_add_s (& ptr, title, value);

		count ++;
	}

	fclose_nointr (fp);

	htc_batt_info.count = ((unsigned long) ptr - (unsigned long) & htc_batt_info.fields [0]) / sizeof (BATT_FIELD);

	//batt_debug_dump (& htc_batt_info);

	return & htc_batt_info;
}

/*
 * htc attr
 */
static const char *htc_attrs [] = {
	"/sys/class/power_supply/battery/batt_attr_text",
	"/sys/class/power_supply/battery/device/batt_attr_text",
	NULL
};

static BATT_INFO *get_battery_info_htc_attr ()
{
	fLOGV ("get battery info from htc attr");

	const char *dev = NULL;
	BATT_FIELD *ptr;
	FILE *fp;
	int i, count;
	char title [BATT_NAME_LEN];
	char value [BATT_NAME_LEN];

	for (i = 0; htc_attrs [i] != NULL; i ++)
	{
		if (access (htc_attrs [i], R_OK) == 0)
		{
			dev = htc_attrs [i];
			break;
		}
	}

	if (! dev)
	{
		fLOGE ("cannot find a valid attribute!");
		return NULL;
	}

	fp = fopen_nointr (dev, "r");

	if (! fp)
	{
		fLOGE ("cannot fopen_nointr %s: %s", dev, strerror (errno));
		return NULL;
	}

	ptr = & htc_batt_info.fields [0];
	count = 0;

	do
	{
		memset (title, 0, sizeof (title));
		memset (value, 0, sizeof (value));

		i = fscanf (fp, "%s%s;", title, value);

		if (i == 2)
		{
			if (value [strlen (value) - 1] == ';')
				value [strlen (value) - 1] = 0;

			batt_add_s (& ptr, title, value);

			count ++;
		}
	}
	while ((i == 2) && (count < BATT_FIELD_MAX));

	fclose_nointr (fp);

	htc_batt_info.count = ((unsigned long) ptr - (unsigned long) & htc_batt_info.fields [0]) / sizeof (BATT_FIELD);

	return & htc_batt_info;
}

/*
 * sys attr
 */
static const char *sys_attrs [] = {
	"/sys/class/power_supply/battery/",
	"/sys/class/power_supply/bms/",
	"/sys/class/power_supply/usb/",
	NULL
};

static const char *sys_filter [] = {
	"uevent",
	"device",
	"subsystem",
	"smem_raw",
	"smem_text",
	"batt_attr_text",
	"technology",
	"batt_debug_flag",
	"charger_control",
	"power",
	"charger_type",
	"htc_batt_data",
	"cycle_data",
	"consist_data",
	NULL
};

static int read_attr (char *path, char *buf, int len)
{
	int fd = open_nointr (path, O_RDONLY, 0);

	if (fd == -1)
	{
		//fLOGE ("cannot open_nointr %s: %s", path, strerror (errno));
		return 0;
	}

	memset (buf, 0, len);

	if (read_nointr (fd, buf, len - 1) < 0)
	{
		fLOGE ("read_nointr [%s]: %s", path, strerror (errno));
		close_nointr (fd);
		return 0;
	}

	close_nointr (fd);

	buf [len - 1] = 0;
	fd = strlen (buf) - 1;

	if (buf [fd] == '\n')
		buf [fd] = 0;

	return 1;
}

static BATT_INFO *get_battery_info_sys_attr ()
{
	fLOGV ("get battery info from system attr");

	BATT_FIELD *ptr;

	char filepath [128 /* > strlen (path) + BATT_NAME_LEN */];
	char buf [BATT_NAME_LEN];
	struct dirent *entry;
	int i, j, count;

	ptr = & htc_batt_info.fields [0];
	count = 0;

	for (i = 0; sys_attrs [i] != NULL; i ++)
	{
		DIR *dir = opendir (sys_attrs [i]);

		if (! dir)
		{
			fLOGE ("cannot opendir %s: %s", sys_attrs [i], strerror (errno));
			continue;
		}

		while (((entry = readdir (dir)) != NULL) && (count < BATT_FIELD_MAX))
		{
			if (entry->d_type == DT_DIR)
				continue;

			if (strlen (entry->d_name) >= BATT_NAME_LEN)
				continue;

			for (j = 0; sys_filter [j] != NULL; j ++)
			{
				if (strcmp (sys_filter [j], entry->d_name) == 0)
				{
					break;
				}
			}

			if (sys_filter [j] != NULL)
				continue;

			sprintf (filepath, "%s%s", sys_attrs [i], entry->d_name);

			if ((access (filepath, R_OK) == 0) && (read_attr (filepath, buf, BATT_NAME_LEN)))
			{
				batt_add_s (& ptr, entry->d_name, buf);

				count ++;
			}
		}

		closedir (dir);
	}

	htc_batt_info.count = ((unsigned long) ptr - (unsigned long) & htc_batt_info.fields [0]) / sizeof (BATT_FIELD);

	return & htc_batt_info;
}

/*
 * main function
 */
typedef struct {
	int prog;
	BATT_INFO *(* get_battery_info) ();
} PROG_MAP;

static PROG_MAP pmap [] = {
	{ BATT_PROG_SYS_ATTR,			get_battery_info_sys_attr },
	{ BATT_PROG_HTC_ATTR,			get_battery_info_htc_attr },
	{ BATT_PROG_RPC_30100001_00000000_5,	get_battery_info_rpc_30100001_00000000_full_info },
	{ BATT_PROG_RPC_30100001_00000000_8,	get_battery_info_rpc_30100001_00000000_coulomb_counter },
	{ BATT_PROG_SMEM_RAW,			get_battery_info_smem_raw },
	{ BATT_PROG_SMEM_TEXT,			get_battery_info_smem_text },
	{ BATT_PROG_NONE,			NULL }
};

BATT_INFO *get_battery_info (int prog)
{
	int i;

	for (i = 0; pmap [i].get_battery_info != NULL; i ++)
	{
		if (pmap [i].prog == prog)
			return pmap [i].get_battery_info ();
	}

	//return pmap [0].get_battery_info ();
	return NULL;
}
