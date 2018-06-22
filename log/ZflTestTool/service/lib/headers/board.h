#ifndef	__SSD_BOARD_H__
#define	__SSD_BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <cutils/properties.h>

#define	BOARD_NAME_LEN	PROPERTY_VALUE_MAX

extern int read_proc_cmdline (const char *key, char *value, int len, const char *default_value);
extern char *get_board_name (char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif
