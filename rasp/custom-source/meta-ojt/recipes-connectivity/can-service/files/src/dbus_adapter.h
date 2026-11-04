/*
 * dbus_adapter.h - D-Bus client for can-service (GIO, async, runs in GLib main loop)
 *
 *   org.automotive.VideoPlayer   Play Pause Stop Next Previous PlayIndex(i) Seek(x) GetStatus()
 *                                signal StatusChanged(s,i,s)
 *   org.automotive.MediaManager  GetState() GetPlaylist()  (read only, for HEALTH)
 *                                signals StateChanged(s), PlaylistChanged(i)
 *
 * can-service is a client only: it owns no bus name and needs no policy file.
 */
#ifndef DBUS_ADAPTER_H
#define DBUS_ADAPTER_H

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool    valid;        /* false: player not available or call failed */
    char    state[16];    /* "STOPPED" "PLAYING" "PAUSED" "ERROR"       */
    int32_t index;        /* -1 = empty playlist                        */
    int64_t position_ms;
    int64_t duration_ms;
} PlayerStatus;

typedef struct {
    void (*player_changed)(void *ud);   /* StatusChanged signal, player appeared/vanished */
    void (*media_changed)(void *ud);    /* USB state / track count / media-manager up-down */
    void *user_data;
} DbusAdapterCallbacks;

/* method_done: result is an OjtResult (OK, REJECTED, PLAYER_UNAVAILABLE, DBUS_ERROR) */
typedef void (*DbusMethodDone)(uint8_t result, void *ctx);
typedef void (*DbusStatusDone)(const PlayerStatus *st, void *ctx);

bool dbus_adapter_start(GBusType bus_type, const DbusAdapterCallbacks *cb);
void dbus_adapter_stop(void);

bool        dbus_adapter_connected(void);
bool        dbus_adapter_player_available(void);
bool        dbus_adapter_media_available(void);
const char *dbus_adapter_usb_state(void);    /* "NO_MEDIA" "SCANNING" "READY" or "" */
int         dbus_adapter_track_count(void);  /* -1 = unknown */

/* Call a VideoPlayer method returning (b). params may be NULL. Never blocks. */
void dbus_adapter_call_player(const char *method, GVariant *params, DbusMethodDone done, void *ctx);

/* Async GetStatus. done() is always called (st->valid = false on failure). */
void dbus_adapter_get_status(DbusStatusDone done, void *ctx);

#endif /* DBUS_ADAPTER_H */
