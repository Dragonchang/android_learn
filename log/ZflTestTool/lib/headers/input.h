#ifndef	__SSD_INPUT_H__
#define	__SSD_INPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

extern int open_input_device (const char *keyword);
extern int find_input_device (const char *keyword, char *buffer, int len);

extern int *open_input_devices (const char **white_list, const char **black_list);
extern void close_input_devices (int *list);

#ifdef __cplusplus
}
#endif

#endif
