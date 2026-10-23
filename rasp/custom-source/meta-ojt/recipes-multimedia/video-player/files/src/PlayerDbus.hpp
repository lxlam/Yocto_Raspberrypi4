#pragma once
/*
 * PlayerDbus - D-Bus glue for the video player (uses GIO C API, same as media-manager)
 *
 * 1. Client of media-manager  (org.automotive.MediaManager)
 *      signal PlaylistChanged(n) -> call GetPlaylist() -> PlayerCore::setPlaylist()
 *      n == 0 / media-manager gone -> PlayerCore::clearPlaylist()
 *
 * 2. Service org.automotive.VideoPlayer  (called by gdbus now, GUI / CAN later)
 *      Play Pause Stop Next Previous PlayIndex(i) Seek(x) GetStatus()
 *      signal StatusChanged(s state, i index, s title)
 */
#include "PlayerCore.hpp"
#include <gio/gio.h>

bool player_dbus_start(PlayerCore* player, GBusType bus_type, GMainLoop* loop);
void player_dbus_stop();
