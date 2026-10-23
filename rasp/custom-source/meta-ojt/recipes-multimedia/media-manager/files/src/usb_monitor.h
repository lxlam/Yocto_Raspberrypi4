/*
 * usb_monitor.h - Detect USB plug/unplug (udev) and mount/unmount it
 *
 * Runs inside the GLib main loop (no extra thread):
 *   the udev socket fd is added to the main loop with g_unix_fd_add().
 * Only ONE USB storage is handled at a time.
 */
#ifndef USB_MONITOR_H
#define USB_MONITOR_H

#include <stdbool.h>

/* inserted = true  -> mount_point is where the USB is mounted
 * inserted = false -> USB was removed, mount_point is NULL          */
typedef void (*usb_event_cb_t)(bool inserted, const char *mount_point, void *user_data);

/* Start listening. Also checks a USB that is already plugged in at boot. */
bool usb_monitor_start(usb_event_cb_t cb, void *user_data);

/* Stop listening and unmount the USB if we mounted it. */
void usb_monitor_stop(void);

#endif /* USB_MONITOR_H */
