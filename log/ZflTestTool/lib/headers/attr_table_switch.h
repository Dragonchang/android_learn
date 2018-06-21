#ifndef _SSD_ATTR_TABLE_SWITCH_H_
#define _SSD_ATTR_TABLE_SWITCH_H_

#if __cplusplus
extern "C" {
#endif

#include <cutils/properties.h>

typedef struct {
	const char *node, *on;
	int need_board_param;
	char save [PROPERTY_VALUE_MAX];
} ATTR_TABLE;

extern int table_get (int *ref_count, ATTR_TABLE *table);
extern int table_put (int *ref_count, ATTR_TABLE *table);

#if __cplusplus
}
#endif

#endif
