#ifndef	__SSD_HW_LIB_H__
#define	__SSD_HW_LIB_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UNUSED_VAR
#define UNUSED_VAR(x) UNUSED_ ## x __attribute__((__unused__))
#endif

extern int hw_sensor_enable (const char *keyword, int enable);

#ifdef __cplusplus
}
#endif

#endif
