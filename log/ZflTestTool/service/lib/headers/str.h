#ifndef _SSD_STR_H_
#define _SSD_STR_H_

#if __cplusplus
extern "C" {
#endif

extern char *strtrim (char *str);

/* string replacement tag */
#define	TAG_DATETIME		"[_DATETIMESTR_]"	/* with "YYYYMMDD_HHMMSS" */
#define	TAG_DATETIME_LEN	(15)

extern void str_replace_tags (char *str);

#if __cplusplus
}
#endif

#endif
