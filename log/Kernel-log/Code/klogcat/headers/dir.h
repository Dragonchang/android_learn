

#ifndef	_SSD_DIR_H_
#define	_SSD_DIR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "headers/glist.h"

#define	STORAGE_KEY_AUTO	"auto"
#define	STORAGE_KEY_USB		"usb"
#define	STORAGE_KEY_EXTERNAL	"external"
#define	STORAGE_KEY_PHONE	"phone"
#define	STORAGE_KEY_INTERNAL	"internal"

#define	STORAGE_CODE_UNKNOWN	'x'
#define	STORAGE_CODE_USB	'u'
#define	STORAGE_CODE_EXTERNAL	'e'
#define	STORAGE_CODE_PHONE	'p'
#define	STORAGE_CODE_INTERNAL	'i'

#define	IS_STORAGE_CODE(c)	(\
	(c == STORAGE_CODE_USB) ||\
	(c == STORAGE_CODE_EXTERNAL) ||\
	(c == STORAGE_CODE_PHONE) ||\
	(c == STORAGE_CODE_INTERNAL) ||\
	(c == STORAGE_CODE_UNKNOWN))

typedef struct {
	char entry [PATH_MAX];
	char *device;
	char *mountpoint;
	char *type;
	char *options;
} STORAGE_MOUNT_ENTRY;

extern const char *dir_get_phone_storage (void);
extern const char *dir_get_external_storage (void);
extern const char *dir_get_usb_storage (void);
extern const char *dir_get_known_storage (const char *storage_name);
extern const char *dir_get_larger_storage (void);
extern char dir_get_storage_code (const char *path);
extern int dir_get_mount_entry (const char *mountpoint, STORAGE_MOUNT_ENTRY *pme);
extern int dir_fuse_state ();
extern int dir_storage_state (const char *storage_path);
extern int dir_exists (const char *path);
extern int dir_write_test (const char *path);
extern int dir_create_recursive (const char *path);
extern void dir_no_media (const char *path);
extern int dir_select_log_path (char *buffer, int len);
extern int dir_clear (const char *path, GLIST *patterns); // if no pattern specified, clear all

#ifdef __cplusplus
}
#endif

#endif

