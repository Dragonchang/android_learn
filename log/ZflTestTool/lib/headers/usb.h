#ifndef _SSD_USB_H_
#define _SSD_USB_H_

#if __cplusplus
extern "C" {
#endif

extern int usb_serial_get (void);
extern int usb_serial_put (void);

extern int is_usb_online (void);

extern void enable_usb_serial (int enable);

#if __cplusplus
}
#endif

#endif
