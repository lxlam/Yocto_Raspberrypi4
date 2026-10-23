#include "GStreamerBackend.hpp"
#include "PlayerCore.hpp"
#include "PlayerDbus.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <glib-unix.h>

/*
 * video-player daemon
 *   - PlayerCore + GStreamerBackend : play video (GStreamer bus has its own thread)
 *   - PlayerDbus                     : playlist from media-manager, commands via D-Bus
 *   - GLib main loop                 : runs all D-Bus callbacks
 *
 * Run:  video-player             (system bus)
 *       video-player --session   (session bus, PC test)
 */

static gboolean on_quit_signal(gpointer loop)
{
    std::cout << "video-player: SIGINT/SIGTERM received, stopping...\n";
    g_main_loop_quit(static_cast<GMainLoop*>(loop));
    return G_SOURCE_REMOVE;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);   /* logs appear at once in journalctl */

    GBusType bus_type = G_BUS_TYPE_SYSTEM;
    if (argc > 1 && std::strcmp(argv[1], "--session") == 0)
        bus_type = G_BUS_TYPE_SESSION;

    PlayerCore player(std::make_unique<GStreamerBackend>());
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);

    g_unix_signal_add(SIGINT,  on_quit_signal, loop);
    g_unix_signal_add(SIGTERM, on_quit_signal, loop);

    if (!player_dbus_start(&player, bus_type, loop)) {
        g_main_loop_unref(loop);
        return 1;
    }

    std::cout << "video-player started ("
              << (bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session") << " bus)\n";
    g_main_loop_run(loop);                 /* <- waits here until quit */

    player_dbus_stop();
    player.stop();
    g_main_loop_unref(loop);
    std::cout << "video-player stopped\n";
    return 0;
}
