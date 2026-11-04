/*
 * dbus_adapter.c - async D-Bus client for VideoPlayer + MediaManager
 */
#define LOG_TAG "DBUS"
#include "log.h"
#include "dbus_adapter.h"
#include "ojt_can_protocol.h"

#include <string.h>

#define VP_NAME  "org.automotive.VideoPlayer"
#define VP_PATH  "/org/automotive/VideoPlayer"
#define VP_IFACE "org.automotive.VideoPlayer1"

#define MM_NAME  "org.automotive.MediaManager"
#define MM_PATH  "/org/automotive/MediaManager"
#define MM_IFACE "org.automotive.MediaManager1"

static GDBusConnection *g_conn = NULL;
static GCancellable    *g_cancel = NULL;      /* cancels in-flight calls on stop */
static DbusAdapterCallbacks g_cb;

static guint g_vp_watch = 0, g_mm_watch = 0;
static guint g_sub_status = 0, g_sub_mm_state = 0, g_sub_mm_playlist = 0;

static bool g_player_up = false;
static bool g_media_up  = false;
static char g_usb_state[16] = "";
static int  g_track_count = -1;

/* ------------------------------------------------------------------ */
bool dbus_adapter_connected(void)        { return g_conn != NULL && !g_dbus_connection_is_closed(g_conn); }
bool dbus_adapter_player_available(void) { return g_player_up; }
bool dbus_adapter_media_available(void)  { return g_media_up; }
const char *dbus_adapter_usb_state(void) { return g_usb_state; }
int  dbus_adapter_track_count(void)      { return g_track_count; }

static void notify_player(void) { if (g_cb.player_changed) g_cb.player_changed(g_cb.user_data); }
static void notify_media(void)  { if (g_cb.media_changed)  g_cb.media_changed(g_cb.user_data); }

static bool is_cancelled(GError *err)
{
    return err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

/* ------------------------------------------------------------------ */
/* VideoPlayer methods returning (b)                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    DbusMethodDone done;
    void *ctx;
    char method[24];
} MethodCall;

static void on_method_reply(GObject *src, GAsyncResult *res, gpointer user_data)
{
    MethodCall *mc = user_data;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    uint8_t result;

    if (is_cancelled(err)) {               /* shutting down: do not touch service state */
        g_error_free(err);
        g_free(mc);
        return;
    }

    if (!reply) {
        if (g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
            g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
            result = OJT_RES_PLAYER_UNAVAILABLE;
        } else {
            result = OJT_RES_DBUS_ERROR;   /* includes G_IO_ERROR_TIMED_OUT */
        }
        LOG_W("%s() failed: %s", mc->method, err->message);
        g_error_free(err);
    } else {
        gboolean ok = FALSE;
        g_variant_get(reply, "(b)", &ok);
        g_variant_unref(reply);
        result = ok ? OJT_RES_OK : OJT_RES_REJECTED;
    }

    mc->done(result, mc->ctx);
    g_free(mc);
}

void dbus_adapter_call_player(const char *method, GVariant *params, DbusMethodDone done, void *ctx)
{
    if (!dbus_adapter_connected()) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        done(OJT_RES_DBUS_ERROR, ctx);
        return;
    }
    if (!g_player_up) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        done(OJT_RES_PLAYER_UNAVAILABLE, ctx);
        return;
    }

    MethodCall *mc = g_new0(MethodCall, 1);
    mc->done = done;
    mc->ctx  = ctx;
    g_strlcpy(mc->method, method, sizeof(mc->method));

    g_dbus_connection_call(g_conn, VP_NAME, VP_PATH, VP_IFACE, method, params,
                           G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           OJT_DBUS_TIMEOUT_MS, g_cancel, on_method_reply, mc);
}

/* ------------------------------------------------------------------ */
/* VideoPlayer GetStatus -> (s i s x x)                                */
/* ------------------------------------------------------------------ */
typedef struct {
    DbusStatusDone done;
    void *ctx;
} StatusCall;

static void on_status_reply(GObject *src, GAsyncResult *res, gpointer user_data)
{
    StatusCall *sc = user_data;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    PlayerStatus st;
    memset(&st, 0, sizeof(st));
    st.index = -1;

    if (is_cancelled(err)) {
        g_error_free(err);
        g_free(sc);
        return;
    }

    if (reply) {
        const gchar *state = NULL, *title = NULL;
        gint32 index = -1;
        gint64 pos = 0, dur = 0;
        g_variant_get(reply, "(&si&sxx)", &state, &index, &title, &pos, &dur);
        g_strlcpy(st.state, state, sizeof(st.state));
        st.index = index;
        st.position_ms = pos;
        st.duration_ms = dur;
        st.valid = true;
        g_variant_unref(reply);
    } else {
        LOG_W("GetStatus() failed: %s", err->message);
        g_error_free(err);
    }

    sc->done(&st, sc->ctx);
    g_free(sc);
}

void dbus_adapter_get_status(DbusStatusDone done, void *ctx)
{
    if (!dbus_adapter_connected() || !g_player_up) {
        PlayerStatus st;
        memset(&st, 0, sizeof(st));
        st.index = -1;
        done(&st, ctx);
        return;
    }
    StatusCall *sc = g_new0(StatusCall, 1);
    sc->done = done;
    sc->ctx  = ctx;
    g_dbus_connection_call(g_conn, VP_NAME, VP_PATH, VP_IFACE, "GetStatus", NULL,
                           G_VARIANT_TYPE("(sisxx)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           OJT_DBUS_TIMEOUT_MS, g_cancel, on_status_reply, sc);
}

/* ------------------------------------------------------------------ */
/* MediaManager: GetState / GetPlaylist (count only)                   */
/* ------------------------------------------------------------------ */
static void on_mm_state_reply(GObject *src, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (is_cancelled(err)) { g_error_free(err); return; }
    if (reply) {
        const gchar *s = NULL;
        g_variant_get(reply, "(&s)", &s);
        g_strlcpy(g_usb_state, s, sizeof(g_usb_state));
        g_variant_unref(reply);
        notify_media();
    } else {
        LOG_W("MediaManager GetState() failed: %s", err->message);
        g_error_free(err);
    }
}

static void on_mm_playlist_reply(GObject *src, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (is_cancelled(err)) { g_error_free(err); return; }
    if (reply) {
        GVariant *arr = g_variant_get_child_value(reply, 0);
        g_track_count = (int)g_variant_n_children(arr);
        g_variant_unref(arr);
        g_variant_unref(reply);
        notify_media();
    } else {
        LOG_W("MediaManager GetPlaylist() failed: %s", err->message);
        g_error_free(err);
    }
}

static void mm_refresh(void)
{
    g_dbus_connection_call(g_conn, MM_NAME, MM_PATH, MM_IFACE, "GetState", NULL,
                           G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           OJT_DBUS_TIMEOUT_MS, g_cancel, on_mm_state_reply, NULL);
    g_dbus_connection_call(g_conn, MM_NAME, MM_PATH, MM_IFACE, "GetPlaylist", NULL,
                           G_VARIANT_TYPE("(a(iss))"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                           OJT_DBUS_TIMEOUT_MS, g_cancel, on_mm_playlist_reply, NULL);
}

/* ------------------------------------------------------------------ */
/* Signals + name watching                                             */
/* ------------------------------------------------------------------ */
static void on_vp_status_changed(GDBusConnection *c, const gchar *s, const gchar *p, const gchar *i,
                                 const gchar *sig, GVariant *params, gpointer ud)
{
    (void)c; (void)s; (void)p; (void)i; (void)sig; (void)params; (void)ud;
    notify_player();
}

static void on_mm_state_changed(GDBusConnection *c, const gchar *s, const gchar *p, const gchar *i,
                                const gchar *sig, GVariant *params, gpointer ud)
{
    (void)c; (void)s; (void)p; (void)i; (void)sig; (void)ud;
    const gchar *state = NULL;
    g_variant_get(params, "(&s)", &state);
    g_strlcpy(g_usb_state, state, sizeof(g_usb_state));
    notify_media();
}

static void on_mm_playlist_changed(GDBusConnection *c, const gchar *s, const gchar *p, const gchar *i,
                                   const gchar *sig, GVariant *params, gpointer ud)
{
    (void)c; (void)s; (void)p; (void)i; (void)sig; (void)ud;
    gint32 count = 0;
    g_variant_get(params, "(i)", &count);
    g_track_count = count;
    notify_media();
}

static void on_vp_appeared(GDBusConnection *c, const gchar *name, const gchar *owner, gpointer ud)
{
    (void)c; (void)owner; (void)ud;
    LOG_I("%s is available", name);
    g_player_up = true;
    notify_player();
}

static void on_vp_vanished(GDBusConnection *c, const gchar *name, gpointer ud)
{
    (void)c; (void)ud;
    LOG_W("%s is NOT available", name);
    g_player_up = false;
    notify_player();
}

static void on_mm_appeared(GDBusConnection *c, const gchar *name, const gchar *owner, gpointer ud)
{
    (void)c; (void)owner; (void)ud;
    LOG_I("%s is available", name);
    g_media_up = true;
    mm_refresh();
    notify_media();
}

static void on_mm_vanished(GDBusConnection *c, const gchar *name, gpointer ud)
{
    (void)c; (void)ud;
    LOG_W("%s is NOT available", name);
    g_media_up = false;
    g_usb_state[0] = '\0';
    g_track_count = -1;
    notify_media();
}

/* ------------------------------------------------------------------ */
bool dbus_adapter_start(GBusType bus_type, const DbusAdapterCallbacks *cb)
{
    GError *err = NULL;
    g_cb = *cb;

    g_conn = g_bus_get_sync(bus_type, NULL, &err);
    if (!g_conn) {
        LOG_E("Cannot connect to D-Bus: %s", err->message);
        g_error_free(err);
        return false;
    }
    /* dbus-daemon restart: we exit, systemd restarts us (Restart=on-failure) */
    g_dbus_connection_set_exit_on_close(g_conn, TRUE);
    g_cancel = g_cancellable_new();

    g_sub_status = g_dbus_connection_signal_subscribe(g_conn, VP_NAME, VP_IFACE, "StatusChanged", VP_PATH,
                                                      NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                      on_vp_status_changed, NULL, NULL);
    g_sub_mm_state = g_dbus_connection_signal_subscribe(g_conn, MM_NAME, MM_IFACE, "StateChanged", MM_PATH,
                                                        NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                        on_mm_state_changed, NULL, NULL);
    g_sub_mm_playlist = g_dbus_connection_signal_subscribe(g_conn, MM_NAME, MM_IFACE, "PlaylistChanged", MM_PATH,
                                                           NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                           on_mm_playlist_changed, NULL, NULL);

    g_vp_watch = g_bus_watch_name_on_connection(g_conn, VP_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                on_vp_appeared, on_vp_vanished, NULL, NULL);
    g_mm_watch = g_bus_watch_name_on_connection(g_conn, MM_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                on_mm_appeared, on_mm_vanished, NULL, NULL);
    LOG_I("Connected to D-Bus, watching %s and %s", VP_NAME, MM_NAME);
    return true;
}

void dbus_adapter_stop(void)
{
    if (g_cancel)          { g_cancellable_cancel(g_cancel); }
    if (g_vp_watch)        { g_bus_unwatch_name(g_vp_watch); g_vp_watch = 0; }
    if (g_mm_watch)        { g_bus_unwatch_name(g_mm_watch); g_mm_watch = 0; }
    if (g_sub_status)      { g_dbus_connection_signal_unsubscribe(g_conn, g_sub_status); g_sub_status = 0; }
    if (g_sub_mm_state)    { g_dbus_connection_signal_unsubscribe(g_conn, g_sub_mm_state); g_sub_mm_state = 0; }
    if (g_sub_mm_playlist) { g_dbus_connection_signal_unsubscribe(g_conn, g_sub_mm_playlist); g_sub_mm_playlist = 0; }
    if (g_conn) {
        g_dbus_connection_flush_sync(g_conn, NULL, NULL);
        g_object_unref(g_conn);
        g_conn = NULL;
    }
    if (g_cancel) { g_object_unref(g_cancel); g_cancel = NULL; }
    g_player_up = g_media_up = false;
}
