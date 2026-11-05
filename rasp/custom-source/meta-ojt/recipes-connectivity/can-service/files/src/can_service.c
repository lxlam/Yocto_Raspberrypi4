/*
 * can_service.c - CAN <-> D-Bus bridge for the OJT video player
 *
 *   CAN 0x100 CMD  --decode--> VideoPlayer D-Bus method --reply--> CAN 0x102 RESPONSE
 *   GetStatus() every 500 ms / on StatusChanged          ---------> CAN 0x101 STATUS
 *   MediaManager state, service flags, errors every 1 s  ---------> CAN 0x103 HEALTH
 *
 * One thread (GLib main loop). CAN fd and D-Bus calls are non-blocking, so a
 * slow or missing video-player never blocks CAN, the GUI or playback.
 *
 * Run:  can-service [--iface can0] [--session] [--verbose]
 */
#define LOG_TAG "CAN"
#include "log.h"
#include "can_link.h"
#include "dbus_adapter.h"
#include "ojt_can_protocol.h"

#include <errno.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define REOPEN_PERIOD_MS 1000u

/* ---------------- state ---------------- */
static GMainLoop *g_loop;
static char       g_ifname[32] = "can0";
static bool       g_verbose = false;

static int   g_fd = -1;
static guint g_fd_watch = 0;
static guint g_reopen_timer = 0;
static int   g_last_open_errno = 0;          /* log open errors only when they change */

/* the one command that waits for D-Bus */
typedef struct {
    bool     active;
    uint8_t  raw[8];
    OjtCommand cmd;
    gint64   start_us;
} Pending;
static Pending g_pending;

/* last completed command, for duplicate detection */
typedef struct {
    bool     valid;
    uint8_t  raw[8];
    OjtResponse resp;
    gint64   time_us;
} LastDone;
static LastDone g_last;

static bool    g_status_in_flight = false;
static uint8_t g_status_counter = 0;
static uint8_t g_health_counter = 0;
static uint8_t g_last_error = OJT_RES_OK;
static uint8_t g_rejected_frames = 0;
static guint   g_tx_fail_logged = 0;

static gint64 now_us(void) { return g_get_monotonic_time(); }

/* ---------------- CAN TX ---------------- */
static void can_send(const OjtCanFrame *f)
{
    if (g_fd < 0)
        return;                                   /* interface down: drop, reopen timer runs */
    if (!can_link_write(g_fd, f)) {
        /* ENOBUFS: TX queue full, usually nobody ACKs (test node off / wiring) */
        if (g_tx_fail_logged++ % 50 == 0)
            LOG_W("TX 0x%03X failed: %s (x%u)", f->id, strerror(errno), g_tx_fail_logged);
    } else {
        g_tx_fail_logged = 0;
    }
}

static void send_response(const OjtCommand *cmd, uint8_t result, uint8_t flags)
{
    OjtResponse r = { cmd->opcode, cmd->seq, result, flags };
    OjtCanFrame f;
    ojt_encode_response(&f, &r);
    can_send(&f);
    if (result != OJT_RES_OK)
        g_last_error = result;
    LOG_I("TX RESP %s seq=%u %s%s", ojt_opcode_name(cmd->opcode), cmd->seq,
          ojt_result_name(result), (flags & OJT_RESP_FLAG_DUPLICATE) ? " (duplicate)" : "");
}

/* ---------------- STATUS / HEALTH ---------------- */
static uint8_t state_from_string(const char *s)
{
    if (strcmp(s, "STOPPED") == 0) return OJT_STATE_STOPPED;
    if (strcmp(s, "PLAYING") == 0) return OJT_STATE_PLAYING;
    if (strcmp(s, "PAUSED")  == 0) return OJT_STATE_PAUSED;
    if (strcmp(s, "ERROR")   == 0) return OJT_STATE_ERROR;
    return OJT_STATE_UNKNOWN;
}

static uint16_t ms_to_s16(int64_t ms)
{
    if (ms <= 0) return 0;
    int64_t s = ms / 1000;
    return s > 65535 ? 65535 : (uint16_t)s;
}

static void send_status(const PlayerStatus *st)
{
    OjtStatus s;
    memset(&s, 0, sizeof(s));
    if (st && st->valid) {
        s.state      = state_from_string(st->state);
        s.index      = (st->index < 0) ? OJT_INDEX_NONE
                                       : (uint16_t)(st->index > OJT_INDEX_MAX ? OJT_INDEX_MAX : st->index);
        s.position_s = ms_to_s16(st->position_ms);
        s.duration_s = ms_to_s16(st->duration_ms);
    } else {
        s.state = OJT_STATE_UNKNOWN;
        s.index = OJT_INDEX_NONE;
    }
    s.counter = g_status_counter++;
    OjtCanFrame f;
    ojt_encode_status(&f, &s);
    can_send(&f);
    if (g_verbose)
        LOG_I("TX STATUS %s idx=%u pos=%us dur=%us", ojt_state_name(s.state), s.index, s.position_s, s.duration_s);
}

static uint8_t usb_from_string(const char *s)
{
    if (strcmp(s, "NO_MEDIA") == 0) return OJT_USB_NO_MEDIA;
    if (strcmp(s, "SCANNING") == 0) return OJT_USB_SCANNING;
    if (strcmp(s, "READY")    == 0) return OJT_USB_READY;
    return OJT_USB_UNKNOWN;
}

static void send_health(void)
{
    OjtHealth h;
    memset(&h, 0, sizeof(h));
    h.usb_state = dbus_adapter_media_available() ? usb_from_string(dbus_adapter_usb_state()) : OJT_USB_UNKNOWN;
    int n = dbus_adapter_track_count();
    h.track_count = (n < 0) ? 0 : (n > 0xFFFF ? 0xFFFF : (uint16_t)n);
    if (dbus_adapter_player_available()) h.flags |= OJT_HEALTH_PLAYER_UP;
    if (dbus_adapter_media_available())  h.flags |= OJT_HEALTH_MEDIA_UP;
    if (dbus_adapter_connected())        h.flags |= OJT_HEALTH_DBUS_UP;
    h.last_error      = g_last_error;
    h.rejected_frames = g_rejected_frames;
    h.counter         = g_health_counter++;
    OjtCanFrame f;
    ojt_encode_health(&f, &h);
    can_send(&f);
    if (g_verbose)
        LOG_I("TX HEALTH usb=%s tracks=%u flags=0x%02X", ojt_usb_name(h.usb_state), h.track_count, h.flags);
}

static void on_status_done(const PlayerStatus *st, void *ctx)
{
    (void)ctx;
    g_status_in_flight = false;
    send_status(st);
}

static void request_status(void)
{
    if (g_status_in_flight)
        return;                                    /* never stack GetStatus calls */
    g_status_in_flight = true;
    dbus_adapter_get_status(on_status_done, NULL);
}

static gboolean on_status_timer(gpointer ud) { (void)ud; request_status(); return G_SOURCE_CONTINUE; }
static gboolean on_health_timer(gpointer ud) { (void)ud; send_health();    return G_SOURCE_CONTINUE; }

static void on_player_changed(void *ud) { (void)ud; request_status(); }
static void on_media_changed(void *ud)  { (void)ud; send_health(); }

/* ---------------- command handling ---------------- */
static void finish_pending(uint8_t result)
{
    if (!g_pending.active)
        return;
    gint64 ms = (now_us() - g_pending.start_us) / 1000;
    send_response(&g_pending.cmd, result, 0);
    LOG_I("    %s seq=%u done in %" G_GINT64_FORMAT " ms", ojt_opcode_name(g_pending.cmd.opcode),
          g_pending.cmd.seq, ms);

    g_last.valid = true;
    memcpy(g_last.raw, g_pending.raw, 8);
    g_last.resp = (OjtResponse){ g_pending.cmd.opcode, g_pending.cmd.seq, result, 0 };
    g_last.time_us = now_us();
    g_pending.active = false;

    request_status();                              /* STATUS shows the new state soon */
}

static void on_method_done(uint8_t result, void *ctx)
{
    (void)ctx;
    finish_pending(result);
}

static void on_get_status_done(const PlayerStatus *st, void *ctx)
{
    (void)ctx;
    uint8_t result = st->valid ? OJT_RES_OK
                               : (dbus_adapter_player_available() ? OJT_RES_DBUS_ERROR : OJT_RES_PLAYER_UNAVAILABLE);
    finish_pending(result);                        /* RESPONSE first ... */
    send_status(st);                               /* ... then STATUS + HEALTH */
    send_health();
}

static void reject_frame(const OjtCommand *cmd, uint8_t result)
{
    if (g_rejected_frames < 255)
        g_rejected_frames++;
    send_response(cmd, result, 0);
}

static void handle_command_frame(const OjtCanFrame *f)
{
    OjtCommand cmd;
    OjtResult r = ojt_decode_command(f, &cmd);

    LOG_I("RX CMD %s seq=%u param=%d dlc=%u", ojt_opcode_name(cmd.opcode), cmd.seq, cmd.param, f->dlc);

    if (r != OJT_RES_OK) {
        LOG_W("    invalid frame: %s", ojt_result_name(r));
        reject_frame(&cmd, r);
        return;
    }

    /* same 8 bytes again: test node retry after a lost RESPONSE */
    if (g_pending.active && memcmp(g_pending.raw, f->data, 8) == 0) {
        LOG_I("    duplicate of the pending command, answer comes when D-Bus replies");
        return;
    }
    if (g_last.valid && memcmp(g_last.raw, f->data, 8) == 0 &&
        now_us() - g_last.time_us < (gint64)OJT_DUPLICATE_WINDOW_MS * 1000) {
        send_response(&cmd, g_last.resp.result, OJT_RESP_FLAG_DUPLICATE);
        return;
    }
    if (g_pending.active) {
        send_response(&cmd, OJT_RES_BUSY, 0);
        return;
    }

    /* PLAY_INDEX beyond the known playlist: reject without calling D-Bus */
    if (cmd.opcode == OJT_OP_PLAY_INDEX) {
        int count = dbus_adapter_track_count();
        if (count > 0 && cmd.param >= count) {
            LOG_W("    index %d >= track count %d", cmd.param, count);
            reject_frame(&cmd, OJT_RES_OUT_OF_RANGE);
            return;
        }
    }

    g_pending.active = true;
    g_pending.cmd = cmd;
    memcpy(g_pending.raw, f->data, 8);
    g_pending.start_us = now_us();

    switch (cmd.opcode) {
    case OJT_OP_PLAY:     dbus_adapter_call_player("Play", NULL, on_method_done, NULL); break;
    case OJT_OP_PAUSE:    dbus_adapter_call_player("Pause", NULL, on_method_done, NULL); break;
    case OJT_OP_STOP:     dbus_adapter_call_player("Stop", NULL, on_method_done, NULL); break;
    case OJT_OP_NEXT:     dbus_adapter_call_player("Next", NULL, on_method_done, NULL); break;
    case OJT_OP_PREVIOUS: dbus_adapter_call_player("Previous", NULL, on_method_done, NULL); break;
    case OJT_OP_PLAY_INDEX:
        dbus_adapter_call_player("PlayIndex", g_variant_new("(i)", cmd.param), on_method_done, NULL);
        break;
    case OJT_OP_SEEK_FORWARD:
        dbus_adapter_call_player("Seek", g_variant_new("(x)", (gint64)ojt_seek_step_ms(cmd.param)),
                                 on_method_done, NULL);
        break;
    case OJT_OP_SEEK_BACKWARD:
        dbus_adapter_call_player("Seek", g_variant_new("(x)", -(gint64)ojt_seek_step_ms(cmd.param)),
                                 on_method_done, NULL);
        break;
    case OJT_OP_GET_STATUS:
        dbus_adapter_get_status(on_get_status_done, NULL);
        break;
    default:
        g_pending.active = false;                  /* not reachable: decode checked opcode */
        reject_frame(&cmd, OJT_RES_BAD_OPCODE);
        break;
    }
}

/* ---------------- CAN RX + interface recovery ---------------- */
static void log_error_frame(uint32_t cls)
{
    static gint64 last_us = 0;
    if (now_us() - last_us < 1000000)               /* max 1 log line per second */
        return;
    last_us = now_us();
    LOG_W("CAN error frame:%s%s%s%s%s%s%s",
          (cls & CAN_LINK_ERR_BUSOFF)     ? " BUS-OFF" : "",
          (cls & CAN_LINK_ERR_ACK)        ? " NO-ACK" : "",
          (cls & CAN_LINK_ERR_CRTL)       ? " CONTROLLER" : "",
          (cls & CAN_LINK_ERR_PROT)       ? " PROTOCOL" : "",
          (cls & CAN_LINK_ERR_BUSERROR)   ? " BUS-ERROR" : "",
          (cls & CAN_LINK_ERR_TX_TIMEOUT) ? " TX-TIMEOUT" : "",
          (cls & CAN_LINK_ERR_RESTARTED)  ? " RESTARTED" : "");
}

static void close_can(void);
static void schedule_reopen(void);

static gboolean on_can_readable(gint fd, GIOCondition cond, gpointer ud)
{
    (void)ud;
    if (cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        LOG_W("%s: socket error, reopening", g_ifname);
        g_fd_watch = 0;
        close_can();
        schedule_reopen();
        return G_SOURCE_REMOVE;
    }
    for (int i = 0; i < 32; i++) {                  /* drain, but give the loop back sometimes */
        OjtCanFrame f;
        uint32_t cls = 0;
        CanReadResult r = can_link_read(fd, &f, &cls);
        if (r == CAN_READ_NONE)
            break;
        if (r == CAN_READ_FRAME && f.id == OJT_CAN_ID_CMD)
            handle_command_frame(&f);
        else if (r == CAN_READ_ERROR_FRAME)
            log_error_frame(cls);
        else if (r == CAN_READ_FAIL) {
            LOG_W("%s: read failed (%s), reopening", g_ifname, strerror(errno));
            g_fd_watch = 0;
            close_can();
            schedule_reopen();
            return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_CONTINUE;
}

static void close_can(void)
{
    if (g_fd_watch) { g_source_remove(g_fd_watch); g_fd_watch = 0; }
    if (g_fd >= 0)  { can_link_close(g_fd); g_fd = -1; }
}

static bool try_open_can(void)
{
    int fd = can_link_open(g_ifname);
    if (fd < 0) {
        if (errno != g_last_open_errno) {
            LOG_W("%s not ready (%s), retry every %u ms", g_ifname, strerror(errno), REOPEN_PERIOD_MS);
            g_last_open_errno = errno;
        }
        return false;
    }
    g_fd = fd;
    g_last_open_errno = 0;
    g_fd_watch = g_unix_fd_add(fd, G_IO_IN | G_IO_ERR | G_IO_HUP, on_can_readable, NULL);
    LOG_I("%s open (%s), listening for CMD 0x%03X", g_ifname, can_link_name(), OJT_CAN_ID_CMD);
    return true;
}

static gboolean on_reopen_timer(gpointer ud)
{
    (void)ud;
    if (try_open_can()) {
        g_reopen_timer = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void schedule_reopen(void)
{
    if (!g_reopen_timer)
        g_reopen_timer = g_timeout_add(REOPEN_PERIOD_MS, on_reopen_timer, NULL);
}

/* ---------------- main ---------------- */
static gboolean on_quit_signal(gpointer ud)
{
    (void)ud;
    LOG_I("SIGINT/SIGTERM, stopping");
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
    GBusType bus = G_BUS_TYPE_SYSTEM;
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *env_if = getenv("CAN_IFACE");
    if (env_if && *env_if)
        g_strlcpy(g_ifname, env_if, sizeof(g_ifname));
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--session") == 0)                 bus = G_BUS_TYPE_SESSION;
        else if (strcmp(argv[i], "--verbose") == 0)            g_verbose = true;
        else if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) g_strlcpy(g_ifname, argv[++i], sizeof(g_ifname));
        else { fprintf(stderr, "usage: %s [--iface can0] [--session] [--verbose]\n", argv[0]); return 2; }
    }

    LOG_I("can-service: protocol v%u, iface %s, link %s, %s bus", OJT_CAN_PROTOCOL_VERSION,
          g_ifname, can_link_name(), bus == G_BUS_TYPE_SYSTEM ? "system" : "session");

    g_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, on_quit_signal, NULL);
    g_unix_signal_add(SIGTERM, on_quit_signal, NULL);

    DbusAdapterCallbacks cb = { on_player_changed, on_media_changed, NULL };
    if (!dbus_adapter_start(bus, &cb))
        return 1;

    if (!try_open_can())
        schedule_reopen();

    guint t_status = g_timeout_add(OJT_STATUS_PERIOD_MS, on_status_timer, NULL);
    guint t_health = g_timeout_add(OJT_HEALTH_PERIOD_MS, on_health_timer, NULL);

    g_main_loop_run(g_loop);

    g_source_remove(t_status);
    g_source_remove(t_health);
    if (g_reopen_timer) g_source_remove(g_reopen_timer);
    close_can();
    dbus_adapter_stop();
    g_main_loop_unref(g_loop);
    LOG_I("can-service stopped");
    return 0;
}
