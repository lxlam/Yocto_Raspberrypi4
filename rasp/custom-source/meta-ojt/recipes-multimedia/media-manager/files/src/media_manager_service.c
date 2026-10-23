/*
 * media_manager_service.c - main() + state + D-Bus service
 *
 * Everything runs in ONE thread (the GLib main loop):
 *     udev fd readable  -> usb_monitor.c -> on_usb_event()
 *     D-Bus method call -> handle_method_call()
 *
 * D-Bus:  name   org.automotive.MediaManager
 *         object /org/automotive/MediaManager
 *         iface  org.automotive.MediaManager1
 *
 * Run:    ojt-media-manager             (system bus, needs the .conf policy)
 *         ojt-media-manager --session   (session bus, for testing on a PC)
 */
#define LOG_TAG "MAIN"
#include "common.h"
#include "usb_monitor.h"
#include "playlist_scanner.h"

#include <string.h>
#include <signal.h>
#include <gio/gio.h>
#include <glib-unix.h>

#define BUS_NAME "org.automotive.MediaManager"
#define OBJ_PATH "/org/automotive/MediaManager"
#define IFACE    "org.automotive.MediaManager1"

/* Description of our D-Bus interface (types: s=string i=int32 b=bool a(...)=array) */
static const char introspection_xml[] =
    "<node>"
    "  <interface name='" IFACE "'>"
    "    <method name='GetState'>"
    "      <arg name='state' type='s' direction='out'/>"
    "    </method>"
    "    <method name='GetPlaylist'>"
    "      <arg name='playlist' type='a(iss)' direction='out'/>"   /* (id, title, path) */
    "    </method>"
    "    <method name='GetTrack'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='found' type='b' direction='out'/>"
    "      <arg name='title' type='s' direction='out'/>"
    "      <arg name='path' type='s' direction='out'/>"
    "    </method>"
    "    <method name='Rescan'>"
    "      <arg name='success' type='b' direction='out'/>"
    "    </method>"
    "    <method name='SimulateUsb'>"
    "      <arg name='action' type='s' direction='in'/>"           /* "add" or "remove" */
    "      <arg name='path' type='s' direction='in'/>"
    "      <arg name='success' type='b' direction='out'/>"
    "    </method>"
    "    <signal name='StateChanged'>"
    "      <arg name='state' type='s'/>"
    "    </signal>"
    "    <signal name='PlaylistChanged'>"
    "      <arg name='count' type='i'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

typedef enum {
    STATE_NO_MEDIA,     /* no USB                         */
    STATE_SCANNING,     /* USB mounted, looking for files */
    STATE_READY         /* playlist is ready (may be 0)   */
} MediaState_t;

static GMainLoop       *g_loop          = NULL;
static GDBusConnection *g_conn          = NULL;
static GDBusNodeInfo   *g_introspection = NULL;

static MediaState_t g_state = STATE_NO_MEDIA;
static Playlist_t   g_playlist;
static char         g_mount_point[256] = "";

/* ------------------------------------------------------------------ */
/* State + signals                                                     */
/* ------------------------------------------------------------------ */
static const char *state_to_str(MediaState_t s)
{
    switch (s) {
    case STATE_NO_MEDIA: return "NO_MEDIA";
    case STATE_SCANNING: return "SCANNING";
    case STATE_READY:    return "READY";
    }
    return "UNKNOWN";
}

static void emit_signal(const char *name, GVariant *params)
{
    GError *err = NULL;

    if (!g_conn) {
        g_variant_unref(g_variant_ref_sink(params));   /* not sent -> free it */
        return;
    }
    /* destination NULL = broadcast to everyone who listens */
    if (!g_dbus_connection_emit_signal(g_conn, NULL, OBJ_PATH, IFACE, name, params, &err)) {
        LOG_E("Emit %s failed: %s", name, err->message);
        g_error_free(err);
        return;
    }
    LOG_I("--> signal %s", name);
}

static void set_state(MediaState_t new_state)
{
    if (new_state == g_state)
        return;
    LOG_I("State: %s -> %s", state_to_str(g_state), state_to_str(new_state));
    g_state = new_state;
    emit_signal("StateChanged", g_variant_new("(s)", state_to_str(g_state)));
}

static void do_scan(void)
{
    int count;

    set_state(STATE_SCANNING);
    count = playlist_scan(&g_playlist, g_mount_point);
    emit_signal("PlaylistChanged", g_variant_new("(i)", count));
    set_state(STATE_READY);
}

/* Called by usb_monitor.c (or by SimulateUsb) */
static void on_usb_event(bool inserted, const char *mount_point, void *user_data)
{
    (void)user_data;

    if (inserted) {
        snprintf(g_mount_point, sizeof(g_mount_point), "%s", mount_point);
        do_scan();
    } else {
        g_mount_point[0] = '\0';
        playlist_clear(&g_playlist);
        emit_signal("PlaylistChanged", g_variant_new("(i)", 0));
        set_state(STATE_NO_MEDIA);
    }
}

/* ------------------------------------------------------------------ */
/* D-Bus method calls                                                  */
/* ------------------------------------------------------------------ */
static void handle_method_call(GDBusConnection *conn, const gchar *sender,
                               const gchar *object_path, const gchar *interface_name,
                               const gchar *method_name, GVariant *params,
                               GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)conn; (void)object_path; (void)interface_name; (void)user_data;
    LOG_I("<-- call %s() from %s", method_name, sender);

    if (strcmp(method_name, "GetState") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(s)", state_to_str(g_state)));
    }
    else if (strcmp(method_name, "GetPlaylist") == 0) {
        GVariantBuilder builder;
        int i;

        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iss)"));
        for (i = 0; i < g_playlist.count; i++) {
            const VideoTrack_t *t = &g_playlist.tracks[i];
            g_variant_builder_add(&builder, "(iss)", t->id, t->title, t->path);
        }
        g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(a(iss))", &builder));
    }
    else if (strcmp(method_name, "GetTrack") == 0) {
        gint32 id = 0;
        const VideoTrack_t *t;

        g_variant_get(params, "(i)", &id);
        t = playlist_get(&g_playlist, id);
        if (t)
            g_dbus_method_invocation_return_value(invocation,
                    g_variant_new("(bss)", TRUE, t->title, t->path));
        else
            g_dbus_method_invocation_return_value(invocation,
                    g_variant_new("(bss)", FALSE, "", ""));
    }
    else if (strcmp(method_name, "Rescan") == 0) {
        gboolean ok = (g_mount_point[0] != '\0');
        if (ok)
            do_scan();
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
    }
    else if (strcmp(method_name, "SimulateUsb") == 0) {
        const gchar *action = NULL;
        const gchar *path   = NULL;
        gboolean ok = TRUE;

        g_variant_get(params, "(&s&s)", &action, &path);   /* &s = borrow, no free */
        if (strcmp(action, "add") == 0 && path[0] != '\0')
            on_usb_event(true, path, NULL);
        else if (strcmp(action, "remove") == 0)
            on_usb_event(false, NULL, NULL);
        else
            ok = FALSE;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
    }
    else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method_name);
    }
}

static const GDBusInterfaceVTable s_vtable = {
    .method_call = handle_method_call,
};

/* ------------------------------------------------------------------ */
/* D-Bus name ownership                                                */
/* ------------------------------------------------------------------ */
static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    GError *err = NULL;
    (void)name; (void)user_data;

    g_conn = conn;
    if (g_dbus_connection_register_object(conn, OBJ_PATH,
            g_introspection->interfaces[0], &s_vtable, NULL, NULL, &err) == 0) {
        LOG_E("Register object failed: %s", err->message);
        g_error_free(err);
        g_main_loop_quit(g_loop);
        return;
    }
    LOG_I("Object registered: %s", OBJ_PATH);
}

static void on_name_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    static bool usb_started = false;
    (void)conn; (void)user_data;

    LOG_I("Bus name acquired: %s", name);

    /* Start USB monitor only now, so its signals can be sent on D-Bus */
    if (!usb_started) {
        usb_started = true;
        if (!usb_monitor_start(on_usb_event, NULL))
            LOG_W("USB monitor not running. You can still test with SimulateUsb.");
    }
}

static void on_name_lost(GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    (void)conn; (void)user_data;
    LOG_E("Cannot own bus name %s (D-Bus policy .conf missing? another instance running?)", name);
    g_main_loop_quit(g_loop);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
static gboolean on_quit_signal(gpointer user_data)
{
    (void)user_data;
    LOG_I("SIGINT/SIGTERM received, stopping...");
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
    GBusType bus_type = G_BUS_TYPE_SYSTEM;
    guint owner_id;

    setvbuf(stdout, NULL, _IOLBF, 0);   /* flush every line -> logs show up in journalctl at once */

    if (argc > 1 && strcmp(argv[1], "--session") == 0)
        bus_type = G_BUS_TYPE_SESSION;

    g_introspection = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    if (!g_introspection) {
        LOG_E("Invalid introspection XML");
        return 1;
    }

    g_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT,  on_quit_signal, NULL);
    g_unix_signal_add(SIGTERM, on_quit_signal, NULL);

    owner_id = g_bus_own_name(bus_type, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                              on_bus_acquired, on_name_acquired, on_name_lost,
                              NULL, NULL);

    LOG_I("Media Manager started (%s bus)", bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session");
    g_main_loop_run(g_loop);            /* <- waits here until quit */

    usb_monitor_stop();
    g_bus_unown_name(owner_id);
    g_main_loop_unref(g_loop);
    g_dbus_node_info_unref(g_introspection);
    LOG_I("Media Manager stopped");
    return 0;
}
