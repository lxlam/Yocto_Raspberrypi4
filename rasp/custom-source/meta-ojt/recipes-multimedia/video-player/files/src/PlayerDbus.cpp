#include "PlayerDbus.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

/* ---- media-manager (we are the CLIENT) ---- */
#define MM_NAME  "org.automotive.MediaManager"
#define MM_PATH  "/org/automotive/MediaManager"
#define MM_IFACE "org.automotive.MediaManager1"

/* ---- video-player (we are the SERVICE) ---- */
#define VP_NAME  "org.automotive.VideoPlayer"
#define VP_PATH  "/org/automotive/VideoPlayer"
#define VP_IFACE "org.automotive.VideoPlayer1"

static const char introspection_xml[] =
    "<node>"
    "  <interface name='" VP_IFACE "'>"
    "    <method name='Play'>     <arg name='ok' type='b' direction='out'/> </method>"
    "    <method name='Pause'>    <arg name='ok' type='b' direction='out'/> </method>"
    "    <method name='Stop'>     <arg name='ok' type='b' direction='out'/> </method>"
    "    <method name='Next'>     <arg name='ok' type='b' direction='out'/> </method>"
    "    <method name='Previous'> <arg name='ok' type='b' direction='out'/> </method>"
    "    <method name='PlayIndex'>"
    "      <arg name='index' type='i' direction='in'/>"
    "      <arg name='ok' type='b' direction='out'/>"
    "    </method>"
    "    <method name='Seek'>"                                   /* relative: +10000 / -10000 */
    "      <arg name='offset_ms' type='x' direction='in'/>"
    "      <arg name='ok' type='b' direction='out'/>"
    "    </method>"
    "    <method name='GetStatus'>"
    "      <arg name='state' type='s' direction='out'/>"
    "      <arg name='index' type='i' direction='out'/>"
    "      <arg name='title' type='s' direction='out'/>"
    "      <arg name='position_ms' type='x' direction='out'/>"
    "      <arg name='duration_ms' type='x' direction='out'/>"
    "    </method>"
    "    <signal name='StatusChanged'>"
    "      <arg name='state' type='s'/>"
    "      <arg name='index' type='i'/>"
    "      <arg name='title' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static PlayerCore*      g_player     = nullptr;
static GMainLoop*       g_loop       = nullptr;
static GDBusConnection* g_conn       = nullptr;
static GDBusNodeInfo*   g_info       = nullptr;
static guint            g_reg_id     = 0;   /* registered object      */
static guint            g_owner_id   = 0;   /* our bus name           */
static guint            g_watch_id   = 0;   /* watch media-manager    */
static guint            g_signal_id  = 0;   /* PlaylistChanged subscr */
static guint            g_timer_id   = 0;   /* status poll timer      */

/* last status sent in StatusChanged */
static std::string g_last_state;
static int         g_last_index = -2;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static const char* state_to_str(PlayerState s)
{
    switch (s) {
    case PlayerState::STOPPED: return "STOPPED";
    case PlayerState::PLAYING: return "PLAYING";
    case PlayerState::PAUSED:  return "PAUSED";
    case PlayerState::ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

/* index of current track, or -1 when the playlist is empty */
static int current_index()
{
    if (g_player->getCurrentMedia().path.empty())
        return -1;
    return static_cast<int>(g_player->getCurrentIndex());
}

/* Send StatusChanged only if state or track changed */
static void check_status()
{
    const std::string state = state_to_str(g_player->getState());
    const int index = current_index();

    if (state == g_last_state && index == g_last_index)
        return;
    g_last_state = state;
    g_last_index = index;

    const std::string title = g_player->getCurrentMedia().name;
    std::cout << "[PlayerDbus] --> StatusChanged " << state << " [" << index << "] " << title << "\n";
    g_dbus_connection_emit_signal(g_conn, nullptr, VP_PATH, VP_IFACE, "StatusChanged",
                                  g_variant_new("(sis)", state.c_str(), index, title.c_str()),
                                  nullptr);
}

/* PlayerCore changes state by itself on EOS (auto-next) -> poll every 500 ms */
static gboolean on_status_timer(gpointer)
{
    check_status();
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Part 1: client of media-manager                                     */
/* ------------------------------------------------------------------ */
static void fetch_playlist()
{
    GError* err = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        g_conn, MM_NAME, MM_PATH, MM_IFACE, "GetPlaylist",
        nullptr,                                  /* no input args */
        G_VARIANT_TYPE("(a(iss))"),               /* expected reply type */
        G_DBUS_CALL_FLAGS_NONE, 3000 /* ms */, nullptr, &err);

    if (!reply) {
        std::cerr << "[PlayerDbus] GetPlaylist failed: " << err->message << "\n";
        g_error_free(err);
        return;
    }

    std::vector<MediaInfo> playlist;
    GVariantIter* iter = nullptr;
    gint32 id = 0;
    const gchar* title = nullptr;
    const gchar* path = nullptr;

    g_variant_get(reply, "(a(iss))", &iter);
    while (g_variant_iter_loop(iter, "(i&s&s)", &id, &title, &path)) {
        playlist.push_back({path, title});        /* MediaInfo{path, name} */
    }
    g_variant_iter_free(iter);
    g_variant_unref(reply);

    std::cout << "[PlayerDbus] Got playlist: " << playlist.size() << " track(s)\n";
    if (playlist.empty())
        g_player->clearPlaylist();
    else
        g_player->setPlaylist(playlist);          /* stops + loads track 0, waits for Play */
    check_status();
}

static void on_playlist_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                const gchar*, GVariant* params, gpointer)
{
    gint32 count = 0;
    g_variant_get(params, "(i)", &count);
    std::cout << "[PlayerDbus] <-- PlaylistChanged(" << count << ")\n";

    if (count == 0) {
        g_player->clearPlaylist();                /* USB removed -> stop now */
        check_status();
    } else {
        fetch_playlist();
    }
}

static void on_mm_appeared(GDBusConnection*, const gchar* name, const gchar*, gpointer)
{
    std::cout << "[PlayerDbus] " << name << " is running -> fetch playlist\n";
    fetch_playlist();
}

static void on_mm_vanished(GDBusConnection*, const gchar* name, gpointer)
{
    std::cout << "[PlayerDbus] " << name << " is not running\n";
    g_player->clearPlaylist();
    check_status();
}

/* ------------------------------------------------------------------ */
/* Part 2: our own service org.automotive.VideoPlayer                  */
/* ------------------------------------------------------------------ */
static void handle_method_call(GDBusConnection*, const gchar* sender, const gchar*,
                               const gchar*, const gchar* method, GVariant* params,
                               GDBusMethodInvocation* invocation, gpointer)
{
    std::cout << "[PlayerDbus] <-- call " << method << "() from " << sender << "\n";

    if (strcmp(method, "GetStatus") == 0) {
        const MediaInfo media = g_player->getCurrentMedia();
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(sisxx)",
                          state_to_str(g_player->getState()),
                          current_index(),
                          media.name.c_str(),
                          (gint64)g_player->getPosition(),
                          (gint64)g_player->getDuration()));
        return;
    }

    bool ok = false;
    if (strcmp(method, "Play") == 0) {
        ok = g_player->play();
    } else if (strcmp(method, "Pause") == 0) {
        ok = g_player->pause();
    } else if (strcmp(method, "Stop") == 0) {
        ok = g_player->stop();
    } else if (strcmp(method, "Next") == 0) {
        ok = g_player->next();
    } else if (strcmp(method, "Previous") == 0) {
        ok = g_player->previous();
    } else if (strcmp(method, "PlayIndex") == 0) {
        gint32 index = 0;
        g_variant_get(params, "(i)", &index);
        ok = index >= 0 && g_player->playIndex(static_cast<std::size_t>(index));
    } else if (strcmp(method, "Seek") == 0) {
        gint64 offset = 0;
        g_variant_get(params, "(x)", &offset);
        ok = offset >= 0 ? g_player->seekForward(offset) : g_player->seekBackward(-offset);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method);
        return;
    }

    if (!ok)
        std::cerr << "[PlayerDbus] " << method << " failed: " << g_player->getLastError() << "\n";
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", ok));
    check_status();                               /* send StatusChanged right away */
}

static const GDBusInterfaceVTable s_vtable = { handle_method_call, nullptr, nullptr, {nullptr} };

static void on_name_acquired(GDBusConnection*, const gchar* name, gpointer)
{
    std::cout << "[PlayerDbus] Bus name acquired: " << name << "\n";
}

static void on_name_lost(GDBusConnection*, const gchar* name, gpointer)
{
    std::cerr << "[PlayerDbus] Cannot own " << name
              << " (D-Bus policy .conf missing? another instance running?)\n";
    g_main_loop_quit(g_loop);
}

/* ------------------------------------------------------------------ */
/* Start / stop                                                        */
/* ------------------------------------------------------------------ */
bool player_dbus_start(PlayerCore* player, GBusType bus_type, GMainLoop* loop)
{
    GError* err = nullptr;
    g_player = player;
    g_loop = loop;

    /* 1. Connect to the bus */
    g_conn = g_bus_get_sync(bus_type, nullptr, &err);
    if (!g_conn) {
        std::cerr << "[PlayerDbus] Cannot connect to D-Bus: " << err->message << "\n";
        g_error_free(err);
        return false;
    }

    /* 2. Publish our object + methods */
    g_info = g_dbus_node_info_new_for_xml(introspection_xml, &err);
    if (!g_info) {
        std::cerr << "[PlayerDbus] Bad XML: " << err->message << "\n";
        g_error_free(err);
        return false;
    }
    g_reg_id = g_dbus_connection_register_object(g_conn, VP_PATH, g_info->interfaces[0],
                                                 &s_vtable, nullptr, nullptr, &err);
    if (g_reg_id == 0) {
        std::cerr << "[PlayerDbus] Register object failed: " << err->message << "\n";
        g_error_free(err);
        return false;
    }

    /* 3. Take our name so others can find us */
    g_owner_id = g_bus_own_name_on_connection(g_conn, VP_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                                              on_name_acquired, on_name_lost, nullptr, nullptr);

    /* 4. Listen to PlaylistChanged from media-manager */
    g_signal_id = g_dbus_connection_signal_subscribe(g_conn, MM_NAME, MM_IFACE, "PlaylistChanged",
                                                     MM_PATH, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                                     on_playlist_changed, nullptr, nullptr);

    /* 5. Know when media-manager starts / stops (fetch playlist on start) */
    g_watch_id = g_bus_watch_name_on_connection(g_conn, MM_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                on_mm_appeared, on_mm_vanished, nullptr, nullptr);

    /* 6. Poll player state (catches EOS auto-next, errors) */
    g_timer_id = g_timeout_add(500, on_status_timer, nullptr);

    std::cout << "[PlayerDbus] Service " << VP_PATH << " ready\n";
    return true;
}

void player_dbus_stop()
{
    if (g_timer_id)  { g_source_remove(g_timer_id); g_timer_id = 0; }
    if (g_watch_id)  { g_bus_unwatch_name(g_watch_id); g_watch_id = 0; }
    if (g_signal_id) { g_dbus_connection_signal_unsubscribe(g_conn, g_signal_id); g_signal_id = 0; }
    if (g_owner_id)  { g_bus_unown_name(g_owner_id); g_owner_id = 0; }
    if (g_reg_id)    { g_dbus_connection_unregister_object(g_conn, g_reg_id); g_reg_id = 0; }
    if (g_info)      { g_dbus_node_info_unref(g_info); g_info = nullptr; }
    if (g_conn)      { g_object_unref(g_conn); g_conn = nullptr; }
}
