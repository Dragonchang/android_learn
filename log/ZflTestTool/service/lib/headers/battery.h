#ifndef _SSD_BATTERY_H_
#define	_SSD_BATTERY_H_

#ifdef __cplusplus
extern "C" {
#endif

#define	BATT_PROG_NONE				(1)
#define	BATT_PROG_SMEM_RAW			(2)
#define	BATT_PROG_SMEM_TEXT			(3)
#define	BATT_PROG_HTC_ATTR			(4)
#define	BATT_PROG_SYS_ATTR			(5)
#define	BATT_PROG_RPC_30100001_00000000_5	(6)
#define	BATT_PROG_RPC_30100001_00000000_8	(7)
#define	BATT_PROG_LAST				(8)

/*
 * battery info
 */
#define	BATT_NAME_LEN	32
#define	BATT_FIELD_MAX	128

#define	BATT_FT_NONE	0
#define	BATT_FT_INT	1
#define	BATT_FT_UINT	2
#define	BATT_FT_LONG	3
#define	BATT_FT_ULONG	4
#define	BATT_FT_FLOAT	5
#define	BATT_FT_STRING	6

typedef struct _batt_field {
	int type;
	char name [BATT_NAME_LEN];
	union {
		char s [BATT_NAME_LEN];
		float f;
		long l;
		int i;
		unsigned long ul;
		unsigned int ui;
	} data;
} BATT_FIELD;

typedef struct _batt_info {
	int count;
	/*
	 * we don't want to many malloc()s and free()s
	 */
	BATT_FIELD fields [BATT_FIELD_MAX];
} BATT_INFO;

extern BATT_INFO *get_battery_info (int prog);

#ifdef __cplusplus
}
#endif

#endif
