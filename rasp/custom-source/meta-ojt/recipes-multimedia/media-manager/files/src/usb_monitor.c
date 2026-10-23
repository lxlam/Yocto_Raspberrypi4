/*
 * usb_monitor.c - USB hotplug (udev) + mount / unmount
 *
 * Flow:
 *   kernel uevent -> udevd -> netlink socket (fd) -> GLib main loop
 *        -> on_udev_fd_ready() -> handle_device()
 *             "add"    : is USB + has filesystem? -> mount -> callback(true, mount_point)
 *             "remove" : is our device?           -> umount -> callback(false, NULL)
 */
#define LOG_TAG "USB"
#include "common.h"
#include "usb_monitor.h"

#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <libudev.h>
#include <glib-unix.h>

#define AUTO_MOUNT_DIR "/media/usb0"

static struct udev         *g_udev     = NULL;
static struct udev_monitor *g_mon      = NULL;
static guint                g_watch_id = 0;    /* id of the fd watch in GLib main loop */

static usb_event_cb_t g_cb      = NULL;
static void          *g_cb_data = NULL;

/* The USB partition currently in use */
static char g_dev_node[64]     = "";     /* e.g. "/dev/sda1"   */
static char g_mount_point[256] = "";     /* e.g. "/media/usb0" */
static bool g_we_mounted       = false;  /* true = we called mount(), so we must umount() */

/* Read /proc/mounts: is dev_node already mounted by someone else? */
static bool find_mount_point(const char *dev_node, char *out, size_t out_len)
{
    FILE *fp = fopen("/proc/mounts", "r");
    char dev[256], mnt[256];
    bool found = false;

    if (!fp)
        return false;

    /* each line: <device> <mount_point> <fs_type> <options> 0 0 */
    while (fscanf(fp, "%255s %255s %*[^\n]", dev, mnt) == 2) {
        if (strcmp(dev, dev_node) == 0) {
            snprintf(out, out_len, "%s", mnt);
            found = true;
            break;
        }
    }
    fclose(fp);
    return found;
}

static bool mount_usb(const char *dev_node, const char *fs_type)
{
    unsigned long flags = MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;  /* read-only, safe */
    int rc;

    /* 1. Already mounted (by systemd / udev rule / user)? Just use it. */
    if (find_mount_point(dev_node, g_mount_point, sizeof(g_mount_point))) {
        LOG_I("%s already mounted at %s", dev_node, g_mount_point);
        g_we_mounted = false;
        return true;
    }

    /* 2. Mount it ourselves */
    mkdir("/media", 0755);          /* errors (e.g. EEXIST) are ignored */
    mkdir(AUTO_MOUNT_DIR, 0755);

    if (strcmp(fs_type, "ntfs") == 0)
        fs_type = "ntfs3";          /* newer kernels name the NTFS driver "ntfs3" */

    if (strcmp(fs_type, "vfat") == 0) {
        /* "utf8": file names with Vietnamese characters are read as UTF-8 */
        rc = mount(dev_node, AUTO_MOUNT_DIR, fs_type, flags, "utf8");
        if (rc != 0)
            rc = mount(dev_node, AUTO_MOUNT_DIR, fs_type, flags, NULL);
    } else {
        rc = mount(dev_node, AUTO_MOUNT_DIR, fs_type, flags, NULL);
    }

    if (rc != 0) {
        LOG_E("mount %s (%s) -> %s failed: %s", dev_node, fs_type, AUTO_MOUNT_DIR, strerror(errno));
        return false;
    }

    snprintf(g_mount_point, sizeof(g_mount_point), "%s", AUTO_MOUNT_DIR);
    g_we_mounted = true;
    LOG_I("Mounted %s (%s) at %s", dev_node, fs_type, g_mount_point);
    return true;
}

static void unmount_usb(void)
{
    if (g_we_mounted) {
        /* MNT_DETACH: the device is already gone, so do a "lazy" unmount */
        if (umount2(g_mount_point, MNT_DETACH) != 0)
            LOG_W("umount %s failed: %s", g_mount_point, strerror(errno));
        else
            LOG_I("Unmounted %s", g_mount_point);
    }
    g_dev_node[0]    = '\0';
    g_mount_point[0] = '\0';
    g_we_mounted     = false;
}

static void handle_device(struct udev_device *dev, const char *action)
{
    const char *dev_node = udev_device_get_devnode(dev);

    if (!dev_node || !action)
        return;

    if (strcmp(action, "add") == 0) {
        /* These properties are filled by udev rules (blkid) */
        const char *bus    = udev_device_get_property_value(dev, "ID_BUS");      /* "usb"        */
        const char *usage  = udev_device_get_property_value(dev, "ID_FS_USAGE"); /* "filesystem" */
        const char *fstype = udev_device_get_property_value(dev, "ID_FS_TYPE");  /* "vfat", ... */

        /* Keep only USB devices that contain a filesystem.
         * -> /dev/sda1 is accepted, /dev/sda (partition table) is skipped. */
        if (!bus || strcmp(bus, "usb") != 0)
            return;
        if (!usage || strcmp(usage, "filesystem") != 0 || !fstype)
            return;

        if (g_dev_node[0] != '\0') {
            LOG_W("Ignore %s: already using %s", dev_node, g_dev_node);
            return;
        }

        LOG_I("USB inserted: %s (fs=%s)", dev_node, fstype);
        if (!mount_usb(dev_node, fstype))
            return;

        snprintf(g_dev_node, sizeof(g_dev_node), "%s", dev_node);
        if (g_cb)
            g_cb(true, g_mount_point, g_cb_data);
    }
    else if (strcmp(action, "remove") == 0) {
        if (g_dev_node[0] == '\0' || strcmp(dev_node, g_dev_node) != 0)
            return;     /* not our device */

        LOG_I("USB removed: %s", dev_node);
        unmount_usb();
        if (g_cb)
            g_cb(false, NULL, g_cb_data);
    }
}

/* Called by GLib main loop when the udev socket has data */
static gboolean on_udev_fd_ready(gint fd, GIOCondition cond, gpointer user_data)
{
    struct udev_device *dev;
    (void)fd; (void)cond; (void)user_data;

    dev = udev_monitor_receive_device(g_mon);
    if (dev) {
        handle_device(dev, udev_device_get_action(dev));
        udev_device_unref(dev);
    }
    return G_SOURCE_CONTINUE;   /* keep watching */
}

/* "Coldplug": handle a USB that was plugged in BEFORE the daemon started */
static void check_existing_devices(void)
{
    struct udev_enumerate  *en = udev_enumerate_new(g_udev);
    struct udev_list_entry *item;

    udev_enumerate_add_match_subsystem(en, "block");
    udev_enumerate_scan_devices(en);

    udev_list_entry_foreach(item, udev_enumerate_get_list_entry(en)) {
        const char *syspath = udev_list_entry_get_name(item);
        struct udev_device *dev = udev_device_new_from_syspath(g_udev, syspath);
        if (dev) {
            handle_device(dev, "add");
            udev_device_unref(dev);
        }
    }
    udev_enumerate_unref(en);
}

bool usb_monitor_start(usb_event_cb_t cb, void *user_data)
{
    g_cb      = cb;
    g_cb_data = user_data;

    g_udev = udev_new();
    if (!g_udev) {
        LOG_E("udev_new() failed");
        return false;
    }

    /* "udev" = receive events AFTER udevd processed its rules (ID_FS_TYPE is ready) */
    g_mon = udev_monitor_new_from_netlink(g_udev, "udev");
    if (!g_mon) {
        LOG_E("udev_monitor_new_from_netlink() failed");
        udev_unref(g_udev);
        g_udev = NULL;
        return false;
    }
    udev_monitor_filter_add_match_subsystem_devtype(g_mon, "block", NULL);
    udev_monitor_enable_receiving(g_mon);

    /* Let the GLib main loop call on_udev_fd_ready() when the fd is readable */
    g_watch_id = g_unix_fd_add(udev_monitor_get_fd(g_mon), G_IO_IN, on_udev_fd_ready, NULL);
    LOG_I("Listening for USB events (udev netlink)");

    check_existing_devices();
    return true;
}

void usb_monitor_stop(void)
{
    if (g_watch_id) {
        g_source_remove(g_watch_id);
        g_watch_id = 0;
    }
    if (g_dev_node[0] != '\0')
        unmount_usb();
    if (g_mon) {
        udev_monitor_unref(g_mon);
        g_mon = NULL;
    }
    if (g_udev) {
        udev_unref(g_udev);
        g_udev = NULL;
    }
}
