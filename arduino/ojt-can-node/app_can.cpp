/*
 * app_can.cpp - OJT CAN test node (Arduino UNO + MCP2515)
 *
 * Logic of the CAN test node. It replaced the former STM32F411 node and keeps
 * its logic, console commands and log lines.
 * Arduino specifics:
 *   - console = Serial (USB cable), no UART interrupt: loop() reads the bytes
 *   - INT pin and button are polled in loop(), no EXTI
 *   - text is kept in flash (PSTR / printf_P) because the UNO has only 2 KB RAM
 *
 * Three levels of "it worked", printed separately on the console:
 *   1. RESPONSE received        -> the Pi got and decoded our frame
 *   2. RESPONSE result OK       -> the D-Bus method of video-player returned true
 *   3. STATUS shows the change  -> playback really changed ("CONFIRMED")
 */
#include "app_can.h"
#include "mcp2515.h"
#include "ojt_can_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* printf with the format string in flash: LOG("x=%u\n", x) */
#define LOG(fmt, ...) printf_P(PSTR(fmt), ##__VA_ARGS__)

#define TX_ACK_TIMEOUT_MS    100u    /* TXB0 still pending -> nobody ACKs     */
#define CONFIRM_TIMEOUT_MS  2000u    /* STATUS must show the change           */
#define BUTTON_DEBOUNCE_MS    50u

static Mcp2515 s_mcp;
static bool    s_mcp_ok = false;

/* ---- command waiting for RESPONSE ---- */
typedef struct {
    bool     active;
    uint8_t  opcode, seq;
    int32_t  param;
    OjtCanFrame frame;
    uint32_t sent_ms;
    uint8_t  retries;
} Pending;
static Pending s_pending;
static uint8_t s_seq = 0;

/* ---- STATUS confirmation after RESPONSE OK ---- */
typedef struct {
    bool     active;
    uint8_t  opcode;
    int32_t  param;
    uint16_t old_index;
    uint16_t old_pos;
    uint32_t start_ms;
} Confirm;
static Confirm s_confirm;

/* ---- last frames from the Pi ---- */
static OjtStatus s_status;
static OjtHealth s_health;
static bool      s_have_status = false, s_have_health = false;
static uint32_t  s_status_ms = 0;
static bool      s_pi_online = false;
static uint32_t  s_tx_start_ms = 0;
static bool      s_tx_watch = false;
static uint8_t   s_tec_before = 0;         /* TX error counter before the last send */

/* ---- console line + button ---- */
static char      s_line[64];
static uint8_t   s_line_len = 0;
static uint8_t   s_esc_state = 0;          /* skip arrow keys: ESC [ A ...          */
static bool      s_trace = false;          /* "trace on": one machine line per frame */
static uint8_t   s_demo_step = 0;
static bool      s_btn_last = HIGH;        /* INPUT_PULLUP: HIGH = not pressed */
static uint32_t  s_btn_change_ms = 0;
static bool      s_btn_done = false;

/* printf -> Serial ('\n' becomes "\r\n" for Tera Term / PuTTY) */
static FILE s_serial_out;
static int serial_putc(char c, FILE *stream)
{
    (void)stream;
    if (c == '\n')
        Serial.write('\r');
    Serial.write(c);
    return 0;
}

static void led(uint8_t pin, bool on) { digitalWrite(pin, on ? HIGH : LOW); }
static bool int_pin_low(void)         { return digitalRead(APP_INT_PIN) == LOW; }

/*
 * Trace lines for the PC tool (tools/can-monitor), one line each:
 *   #F,<micros>,<R|T>,<id hex>,<dlc>,<data hex>      every frame received / sent
 *   #E,<micros>,<event>[,<detail>]                    NOACK NOTSENT ARBLOST LOST TIMEOUT OFFLINE
 * <micros> = Arduino micros() (32 bit, wraps after ~71 min: the PC tool handles it).
 */
static void trace_frame(char dir, uint32_t us, const OjtCanFrame *f)
{
    if (!s_trace) return;
    LOG("#F,%lu,%c,%03lX,%u,", (unsigned long)us, dir, (unsigned long)f->id, f->dlc);
    for (int i = 0; i < f->dlc; i++)
        LOG("%02X", f->data[i]);
    LOG("\n");
}

static void trace_event(const char *event, uint8_t a, uint8_t b)
{
    if (!s_trace) return;
    LOG("#E,%lu,%s,%u,%u\n", (unsigned long)micros(), event, a, b);
}

static void print_frame(const char *dir, const OjtCanFrame *f)
{
    printf("%s", dir);
    LOG("%03lX#", (unsigned long)f->id);
    for (int i = 0; i < f->dlc; i++)
        LOG("%02X", f->data[i]);
    LOG("\n");
}

/* ------------------------------------------------------------------ */
/* TX                                                                  */
/* ------------------------------------------------------------------ */
static bool can_tx(const OjtCanFrame *f)
{
    if (!s_mcp_ok) {
        LOG("ERR  MCP2515 not initialised (check SPI wiring, 'selftest')\n");
        return false;
    }
    Mcp2515Frame m;
    memset(&m, 0, sizeof(m));
    m.id  = (uint16_t)f->id;
    m.dlc = f->dlc;
    memcpy(m.data, f->data, f->dlc);
    s_tec_before = mcp2515_read_register(&s_mcp, 0x1C);     /* TEC */
    Mcp2515Status st = mcp2515_send(&s_mcp, &m);
    if (st == MCP2515_ERR_TX_BUSY) {
        LOG("ERR  TX buffer busy: previous frame not ACKed (bus/termination/Pi?)\n");
        return false;
    }
    if (st != MCP2515_OK) {
        LOG("ERR  SPI write failed: TX buffer read-back wrong (check SI D11, SCK D13, CS D10)\n");
        led(APP_LED_ERROR, true);
        return false;
    }
    s_tx_start_ms = millis();
    s_tx_watch = true;
    trace_frame('T', micros(), f);
    digitalWrite(APP_LED_TX, !digitalRead(APP_LED_TX));
    print_frame("TX  ", f);
    return true;
}

static void send_command(uint8_t opcode, int32_t param)
{
    if (s_pending.active) {
        LOG("WAIT previous %s seq=%u has no RESPONSE yet\n", ojt_opcode_name(s_pending.opcode), s_pending.seq);
        return;
    }
    s_seq++;
    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.opcode = opcode;
    s_pending.seq    = s_seq;
    s_pending.param  = param;
    ojt_encode_command(&s_pending.frame, opcode, s_seq, param);
    LOG("CMD  %s seq=%u param=%ld\n", ojt_opcode_name(opcode), s_seq, (long)param);
    if (can_tx(&s_pending.frame)) {
        s_pending.active  = true;
        s_pending.sent_ms = millis();
    }
}

/* ------------------------------------------------------------------ */
/* RX handling                                                         */
/* ------------------------------------------------------------------ */
static void start_confirm(void)
{
    s_confirm.active    = true;
    s_confirm.opcode    = s_pending.opcode;
    s_confirm.param     = s_pending.param;
    s_confirm.old_index = s_status.index;
    s_confirm.old_pos   = s_status.position_s;
    s_confirm.start_ms  = millis();
}

static bool confirm_matches(const OjtStatus *s)
{
    switch (s_confirm.opcode) {
    case OJT_OP_PLAY:          return s->state == OJT_STATE_PLAYING;
    case OJT_OP_PAUSE:         return s->state == OJT_STATE_PAUSED;
    case OJT_OP_STOP:          return s->state == OJT_STATE_STOPPED;
    case OJT_OP_PLAY_INDEX:    return s->state == OJT_STATE_PLAYING && s->index == (uint16_t)s_confirm.param;
    case OJT_OP_NEXT:
    case OJT_OP_PREVIOUS:      return s->index != s_confirm.old_index;
    case OJT_OP_SEEK_FORWARD: {
        /* position grows by itself while playing: require at least half of the step */
        uint32_t half = (uint32_t)ojt_seek_step_ms(s_confirm.param) / 2000UL;
        if (half == 0) half = 1;
        return (uint32_t)s->position_s >= s_confirm.old_pos + half || s->index != s_confirm.old_index;
    }
    case OJT_OP_SEEK_BACKWARD: {
        uint32_t half = (uint32_t)ojt_seek_step_ms(s_confirm.param) / 2000UL;
        if (half == 0) half = 1;
        return (uint32_t)s->position_s + half <= s_confirm.old_pos || s->position_s == 0;
    }
    default:                   return true;     /* GET_STATUS: nothing to confirm */
    }
}

static void on_response(const OjtResponse *r)
{
    if (!s_pending.active || r->seq != s_pending.seq || r->opcode != s_pending.opcode) {
        LOG("RESP %s seq=%u %s (not the pending command, ignored)\n",
            ojt_opcode_name(r->opcode), r->seq, ojt_result_name(r->result));
        return;
    }
    uint32_t ms = millis() - s_pending.sent_ms;
    LOG("RESP %s seq=%u -> %s%s  [%lu ms]  (1) frame received by Pi\n", ojt_opcode_name(r->opcode), r->seq,
        ojt_result_name(r->result), (r->flags & OJT_RESP_FLAG_DUPLICATE) ? " (duplicate)" : "", (unsigned long)ms);

    if (r->result == OJT_RES_OK) {
        LOG("     (2) D-Bus call OK, waiting for STATUS to confirm ...\n");
        led(APP_LED_ERROR, false);
        if (r->opcode == OJT_OP_GET_STATUS)
            LOG("     STATUS/HEALTH follow\n");
        else
            start_confirm();
    } else {
        LOG("     (2) NOT executed: %s\n", ojt_result_name(r->result));
        led(APP_LED_ERROR, true);
    }
    s_pending.active = false;
}

static void on_status(const OjtStatus *s)
{
    bool changed = !s_have_status || s->state != s_status.state || s->index != s_status.index;
    s_status = *s;
    s_have_status = true;
    s_status_ms = millis();
    if (!s_pi_online) {
        LOG("INFO Pi online (STATUS received)\n");
        s_pi_online = true;
    }
    led(APP_LED_ONLINE, true);
    led(APP_LED_PLAY, s->state == OJT_STATE_PLAYING);

    if (changed) {
        if (s->index == OJT_INDEX_NONE)
            LOG("STAT %s index=none\n", ojt_state_name(s->state));
        else
            LOG("STAT %s index=%u %u/%u s\n", ojt_state_name(s->state), s->index, s->position_s, s->duration_s);
    }
    if (s_confirm.active && confirm_matches(s)) {
        LOG("     (3) CONFIRMED by STATUS: %s index=%u pos=%u s  [%lu ms]\n", ojt_state_name(s->state), s->index,
            s->position_s, (unsigned long)(millis() - s_confirm.start_ms));
        s_confirm.active = false;
    }
}

static void on_health(const OjtHealth *h)
{
    bool changed = !s_have_health || h->usb_state != s_health.usb_state || h->track_count != s_health.track_count ||
                   h->flags != s_health.flags;
    s_health = *h;
    s_have_health = true;
    if (changed)
        LOG("HLTH usb=%s tracks=%u player=%s media=%s last_err=%s rejected=%u\n", ojt_usb_name(h->usb_state),
            h->track_count, (h->flags & OJT_HEALTH_PLAYER_UP) ? "up" : "DOWN",
            (h->flags & OJT_HEALTH_MEDIA_UP) ? "up" : "DOWN", ojt_result_name(h->last_error), h->rejected_frames);
}

static void poll_can_rx(void)
{
    Mcp2515Frame m;
    for (int i = 0; i < 8; i++) {                    /* RXB0 + RXB1, a few frames per loop */
        if (mcp2515_receive(&s_mcp, &m) != MCP2515_OK)
            break;
        uint32_t rx_us = micros();
        OjtCanFrame f;
        memset(&f, 0, sizeof(f));
        f.id  = m.id;
        f.dlc = m.dlc;
        memcpy(f.data, m.data, m.dlc);

        trace_frame('R', rx_us, &f);

        OjtResponse r;
        OjtStatus s;
        OjtHealth h;
        if (f.id == OJT_CAN_ID_RESPONSE)
            print_frame("RX  ", &f);                 /* STATUS/HEALTH: too frequent to print raw */
        if (ojt_decode_response(&f, &r))      on_response(&r);
        else if (ojt_decode_status(&f, &s))   on_status(&s);
        else if (ojt_decode_health(&f, &h))   on_health(&h);
        else if (f.id == OJT_CAN_ID_RESPONSE) LOG("ERR  RESPONSE with bad DLC/checksum\n");
    }
}

/* ------------------------------------------------------------------ */
/* timeouts                                                            */
/* ------------------------------------------------------------------ */

/*
 * TXB0 is still pending after 100 ms. Two very different reasons:
 *  a) the frame WAS sent but nobody ACKed it -> TXERR set and/or TEC grew
 *  b) the frame was NEVER sent: the MCP2515 waits for the bus to be idle,
 *     i.e. its RX pin is stuck "dominant" (transceiver / CAN_H-CAN_L problem)
 */
static void report_tx_failure(void)
{
    Mcp2515Regs r;
    mcp2515_read_regs(&s_mcp, &r);
    if (r.txb0ctrl & MCP2515_TXB_MLOA) {
        trace_event("ARBLOST", r.tec, r.txb0ctrl);
        LOG("ERR  arbitration lost on every try (another node sends a lower ID all the time?) TXB0CTRL=%02X\n", r.txb0ctrl);
    } else if ((r.txb0ctrl & MCP2515_TXB_TXERR) || r.tec > s_tec_before) {
        trace_event("NOACK", r.tec, r.txb0ctrl);
        LOG("ERR  no ACK on CAN bus: frame aborted (Pi down? CAN_H/L swapped? termination? bitrate?)\n");
        LOG("     frame was sent, nobody acknowledged it  (TEC %u -> %u, TXB0CTRL=%02X)\n",
            s_tec_before, r.tec, r.txb0ctrl);
    } else {
        trace_event("NOTSENT", r.tec, r.txb0ctrl);
        LOG("ERR  frame NOT sent: MCP2515 never saw the CAN bus idle  (TEC=%u, TXB0CTRL=%02X)\n", r.tec, r.txb0ctrl);
        LOG("     RX of the MCP2515 stuck 'dominant': transceiver power (5 V), TJA1050, CAN_H/CAN_L shorted to GND/5V?\n");
    }
}
static void check_timeouts(void)
{
    uint32_t now = millis();

    if (s_tx_watch && now - s_tx_start_ms > TX_ACK_TIMEOUT_MS) {
        s_tx_watch = false;
        if (mcp2515_tx_pending(&s_mcp)) {
            report_tx_failure();
            mcp2515_abort_tx(&s_mcp);
            led(APP_LED_ERROR, true);
        } else if (!mcp2515_tx_done(&s_mcp)) {
            /* not waiting and not sent: the "request to send" never reached the chip */
            trace_event("LOST", 0, 0);
            LOG("ERR  frame lost: MCP2515 did not send it (SPI write lost? check SI D11, SCK D13)\n");
            led(APP_LED_ERROR, true);
        }
    }

    if (s_pending.active && now - s_pending.sent_ms > OJT_RESPONSE_TIMEOUT_MS) {
        if (s_pending.retries == 0) {
            s_pending.retries = 1;
            s_pending.sent_ms = now;
            LOG("RETR %s seq=%u (no RESPONSE in %u ms, same seq -> Pi answers DUPLICATE if it already ran)\n",
                ojt_opcode_name(s_pending.opcode), s_pending.seq, (unsigned)OJT_RESPONSE_TIMEOUT_MS);
            can_tx(&s_pending.frame);
        } else {
            trace_event("TIMEOUT", s_pending.opcode, s_pending.seq);
            LOG("ERR  TIMEOUT %s seq=%u: no RESPONSE (can-service running? 'journalctl -u can-service')\n",
                ojt_opcode_name(s_pending.opcode), s_pending.seq);
            s_pending.active = false;
            led(APP_LED_ERROR, true);
        }
    }

    if (s_confirm.active && now - s_confirm.start_ms > CONFIRM_TIMEOUT_MS) {
        LOG("     (3) NOT CONFIRMED: STATUS did not show the change in %u ms (state=%s index=%u)\n",
            (unsigned)CONFIRM_TIMEOUT_MS, ojt_state_name(s_status.state), s_status.index);
        s_confirm.active = false;
    }

    if (s_pi_online && now - s_status_ms > OJT_PEER_TIMEOUT_MS) {
        trace_event("OFFLINE", 0, 0);
        LOG("ERR  Pi offline: no STATUS for %u ms\n", (unsigned)OJT_PEER_TIMEOUT_MS);
        s_pi_online = false;
        led(APP_LED_ONLINE, false);
        led(APP_LED_PLAY, false);
    }
}

/* ------------------------------------------------------------------ */
/* self test: MCP2515 loopback, no bus needed                          */
/* ------------------------------------------------------------------ */

/* what an init error code means for the wiring */
static void print_init_hint(Mcp2515Status st)
{
    uint8_t cs = mcp2515_read_register(&s_mcp, 0x0E);
    LOG("     code %d, CANSTAT=%02X: ", (int)st, cs);
    if (st == MCP2515_ERR_SPI)
        LOG("chip answers (read ok) but writes do not arrive -> SI D11 (MOSI), SCK D13\n");
    else if (st == MCP2515_ERR_NO_CHIP && cs == 0xFF)
        LOG("no answer -> CS D10, SO D12, VCC/GND of the module\n");
    else if (st == MCP2515_ERR_NO_CHIP && cs == 0x00)
        LOG("MISO always low -> SO D12, VCC of the module (must be 5V)\n");
    else if (st == MCP2515_ERR_NO_CHIP)
        LOG("wrong value -> loose wire, SI/SO swapped (D11/D12)\n");
    else if (st == MCP2515_ERR_MODE)
        LOG("mode change refused -> chip or crystal does not run\n");
    else
        LOG("crystal/bitrate pair not supported (APP_MCP2515_OSC_HZ)\n");
}

/* step 5: send one real frame in NORMAL mode and look at what the chip did */
static void transceiver_test(void)
{
    Mcp2515Frame t = { 0x7AB, 0, { 0 } };            /* ID not used by the OJT protocol */
    uint8_t tec0 = mcp2515_read_register(&s_mcp, 0x1C);
    if (mcp2515_send(&s_mcp, &t) != MCP2515_OK) {
        LOG("     (5) FAIL: SPI write failed while loading the TX buffer -> SI D11, SCK D13\n");
        return;
    }
    delay(20);
    Mcp2515Regs r;
    mcp2515_read_regs(&s_mcp, &r);
    bool done = mcp2515_tx_done(&s_mcp);
    if (r.txb0ctrl & MCP2515_TXB_TXREQ)
        mcp2515_abort_tx(&s_mcp);

    if (done)
        LOG("     (5) transceiver OK: frame sent and ACKed by another node (bus connected)\n");
    else if ((r.txb0ctrl & MCP2515_TXB_TXERR) || r.tec > tec0)
        LOG("     (5) transceiver OK: frame reached the bus, no ACK (normal if the bus is not connected)\n");
    else if (r.txb0ctrl & MCP2515_TXB_MLOA)
        LOG("     (5) transceiver OK: arbitration lost (another node is sending)\n");
    else if (r.txb0ctrl & MCP2515_TXB_TXREQ)
        LOG("     (5) transceiver FAIL: frame NOT sent, RX stuck 'dominant' -> TJA1050 / its 5 V / CAN_H-CAN_L\n");
    else
        LOG("     (5) FAIL: chip did not start sending -> SPI write lost (SI D11, SCK D13)\n");
}
static void self_test(void)
{
    LOG("SELFTEST MCP2515 loopback ...\n");

    /* 1. reset + configuration (SPI test) */
    Mcp2515Status st = mcp2515_reset_config(&s_mcp);
    if (st != MCP2515_OK) {
        LOG("SELFTEST FAIL: init\n");
        print_init_hint(st);
        s_mcp_ok = false;
        return;
    }
    LOG("     (1) SPI ok, CONFIG mode\n");

    /* 2. CONFIG -> LOOPBACK: internal, does NOT depend on the CAN bus */
    if (mcp2515_set_mode(&s_mcp, MCP2515_MODE_LOOPBACK) != MCP2515_OK) {
        LOG("SELFTEST FAIL: cannot enter loopback mode from CONFIG (CANSTAT=%02X CANCTRL=%02X)\n",
            mcp2515_read_register(&s_mcp, 0x0E), mcp2515_read_register(&s_mcp, 0x0F));
        LOG("     MCP2515 chip or its crystal does not run\n");
        s_mcp_ok = (mcp2515_init(&s_mcp) == MCP2515_OK);
        return;
    }

    /* 3. send one frame to ourselves */
    Mcp2515Frame tx = { 0x7AB, 8, { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 } }, rx;
    mcp2515_send(&s_mcp, &tx);
    uint32_t t0 = millis();
    st = MCP2515_NO_MSG;
    bool int_seen = false;
    while (millis() - t0 < 50 && st != MCP2515_OK) {
        if (int_pin_low())
            int_seen = true;                         /* INT goes low when the frame arrives */
        st = mcp2515_receive(&s_mcp, &rx);
    }
    bool ok = (st == MCP2515_OK && rx.id == tx.id && rx.dlc == 8 && memcmp(rx.data, tx.data, 8) == 0);
    LOG("SELFTEST %s: frame %s, INT pin %s\n", ok ? "PASS" : "FAIL", ok ? "looped back" : "not received",
        int_seen ? "seen" : "not seen (check D2 wiring)");

    /* 4. back to NORMAL (loopback -> config -> normal: does not depend on the bus) */
    mcp2515_set_mode(&s_mcp, MCP2515_MODE_CONFIG);
    s_mcp_ok = (mcp2515_set_mode(&s_mcp, MCP2515_MODE_NORMAL) == MCP2515_OK);
    if (!s_mcp_ok) {
        LOG("     (4) cannot enter NORMAL mode (CANSTAT=%02X)\n", mcp2515_read_register(&s_mcp, 0x0E));
        return;
    }
    LOG("     (4) back to NORMAL mode\n");

    /* 5. transceiver: one real frame on the bus (works with only J1, bus not connected) */
    transceiver_test();
}

/* ------------------------------------------------------------------ */
/* console                                                             */
/* ------------------------------------------------------------------ */
static void print_help(void)
{
    LOG("commands: play pause stop next prev | index <n> | ff [ms] | rew [ms] | status\n"
        "          info | regs | selftest | spitest | raw <hex, 0..16 digits> | trace on|off | help\n"
        "button D4: next demo step (PLAY, PAUSE, NEXT, PREV, FF, REW, INDEX 0, STOP, STATUS)\n");
}

static void send_raw(const char *hex)
{
    OjtCanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = OJT_CAN_ID_CMD;
    size_t n = strlen(hex);
    if (n % 2 || n > 16) {
        LOG("raw: give 0..8 bytes as hex, e.g. raw 0101000000000000\n");
        return;
    }
    for (size_t i = 0; i < n / 2; i++) {
        char b[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        f.data[i] = (uint8_t)strtoul(b, NULL, 16);
    }
    f.dlc = (uint8_t)(n / 2);
    can_tx(&f);                       /* not tracked as pending: watch the RESP line */
}

static void print_info(void)
{
    LOG("node: MCP2515 %s, %lu MHz crystal, %lu bit/s, Pi %s\n", s_mcp_ok ? "ok" : "NOT OK",
        (unsigned long)(APP_MCP2515_OSC_HZ / 1000000UL), (unsigned long)APP_CAN_BITRATE, s_pi_online ? "online" : "offline");
    if (s_have_status)
        LOG("STATUS %s index=%u pos=%u/%u s cnt=%u\n", ojt_state_name(s_status.state), s_status.index,
            s_status.position_s, s_status.duration_s, s_status.counter);
    if (s_have_health)
        LOG("HEALTH usb=%s tracks=%u flags=0x%02X last_err=%s rejected=%u\n", ojt_usb_name(s_health.usb_state),
            s_health.track_count, s_health.flags, ojt_result_name(s_health.last_error), s_health.rejected_frames);
}

/*
 * spitest: write + read back the 8 data bytes of TX buffer 1 (registers 0x46..0x4D,
 * never sent because TXB1 is not used) 1000 times with different patterns,
 * and read CANSTAT 2000 times. Finds loose wires that a single test misses.
 */
static void spi_test(void)
{
    const uint16_t loops = 1000;
    static const uint8_t pat[] = { 0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0, 0x01, 0x80 };
    uint32_t wr_err = 0, rd_err = 0;
    uint8_t  bad_bits = 0, first_reg = 0, first_w = 0, first_r = 0;
    bool     all_ff = true, all_00 = true;

    LOG("SPITEST %u x 8 write/read + %u CANSTAT reads (1 MHz) ...\n", loops, 2 * loops);
    for (uint16_t n = 0; n < loops; n++) {
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t reg = 0x46 + i;
            uint8_t w = (uint8_t)(pat[(n + i) % 8] ^ (uint8_t)n);
            mcp2515_write_register(&s_mcp, reg, w);
            uint8_t r = mcp2515_read_register(&s_mcp, reg);
            if (r != 0xFF) all_ff = false;
            if (r != 0x00) all_00 = false;
            if (r != w) {
                if (wr_err == 0) { first_reg = reg; first_w = w; first_r = r; }
                wr_err++;
                bad_bits |= (uint8_t)(r ^ w);
            }
        }
    }
    uint8_t cs0 = mcp2515_read_register(&s_mcp, 0x0E);
    for (uint16_t n = 0; n < 2 * loops; n++)
        if (mcp2515_read_register(&s_mcp, 0x0E) != cs0)
            rd_err++;

    if (wr_err == 0 && rd_err == 0) {
        LOG("SPITEST PASS: 8000 write/read ok, CANSTAT stable (%02X) -> SPI wiring is good\n", cs0);
        return;
    }
    LOG("SPITEST FAIL: %lu write/read errors, %lu unstable CANSTAT reads\n",
        (unsigned long)wr_err, (unsigned long)rd_err);
    if (wr_err)
        LOG("     first error: reg %02X wrote %02X read %02X, wrong bits mask %02X\n",
            first_reg, first_w, first_r, bad_bits);
    if (all_ff)       LOG("     always FF: chip does not answer -> CS D10, SO D12, module VCC/GND\n");
    else if (all_00)  LOG("     always 00: MISO dead -> SO D12, module VCC\n");
    else if (wr_err)  LOG("     some errors: loose wire -> SI D11, SCK D13, SO D12 (wiggle each wire while running spitest)\n");
    else              LOG("     reads unstable: loose SO D12 / CS D10 / GND\n");
}

static void print_regs(void)
{
    Mcp2515Regs r;
    mcp2515_read_regs(&s_mcp, &r);
    LOG("CANSTAT=%02X CANCTRL=%02X EFLG=%02X TEC=%u REC=%u CANINTF=%02X TXB0CTRL=%02X\n",
        r.canstat, r.canctrl, r.eflg, r.tec, r.rec, r.canintf, r.txb0ctrl);
    if (r.eflg & 0x20) LOG("     EFLG: BUS-OFF\n");
    if (r.eflg & 0x10) LOG("     EFLG: TX error-passive\n");
    if (r.eflg & 0x08) LOG("     EFLG: RX error-passive\n");
    if (r.eflg & 0xC0) { LOG("     EFLG: RX overflow (cleared)\n"); mcp2515_clear_errors(&s_mcp); }
}

static void handle_line(char *line)
{
    LOG("> %s\n", line);              /* echo: Serial Monitor does not show what you typed */
    char *cmd = strtok(line, " ");
    char *arg = strtok(NULL, " ");
    if (!cmd) return;
    long v = arg ? strtol(arg, NULL, 0) : 0;

    if      (!strcmp_P(cmd, PSTR("play")))     send_command(OJT_OP_PLAY, 0);
    else if (!strcmp_P(cmd, PSTR("pause")))    send_command(OJT_OP_PAUSE, 0);
    else if (!strcmp_P(cmd, PSTR("stop")))     send_command(OJT_OP_STOP, 0);
    else if (!strcmp_P(cmd, PSTR("next")))     send_command(OJT_OP_NEXT, 0);
    else if (!strcmp_P(cmd, PSTR("prev")))     send_command(OJT_OP_PREVIOUS, 0);
    else if (!strcmp_P(cmd, PSTR("index")))    send_command(OJT_OP_PLAY_INDEX, (int32_t)v);
    else if (!strcmp_P(cmd, PSTR("ff")))       send_command(OJT_OP_SEEK_FORWARD, (int32_t)v);
    else if (!strcmp_P(cmd, PSTR("rew")))      send_command(OJT_OP_SEEK_BACKWARD, (int32_t)v);
    else if (!strcmp_P(cmd, PSTR("status")))   send_command(OJT_OP_GET_STATUS, 0);
    else if (!strcmp_P(cmd, PSTR("info")))     print_info();
    else if (!strcmp_P(cmd, PSTR("regs")))     print_regs();
    else if (!strcmp_P(cmd, PSTR("spitest")))  spi_test();
    else if (!strcmp_P(cmd, PSTR("selftest"))) self_test();
    else if (!strcmp_P(cmd, PSTR("raw")))      send_raw(arg ? arg : "");
    else if (!strcmp_P(cmd, PSTR("trace"))) {
        s_trace = (arg && !strcmp_P(arg, PSTR("on")));
        LOG("#TRACE,%u\n", s_trace ? 1 : 0);       /* PC tool waits for this line */
    }
    else                                       print_help();
}

/* read typed characters; a full line ends with CR or LF (Serial Monitor: "Newline") */
static void poll_console(void)
{
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        /* arrow keys send ESC [ A ... : skip the whole sequence */
        if (s_esc_state == 1) { s_esc_state = (c == '[' || c == 'O') ? 2 : 0; continue; }
        if (s_esc_state == 2) { if (c >= 0x40 && c <= 0x7E) s_esc_state = 0; continue; }
        if (c == 0x1B) { s_esc_state = 1; continue; }
        if (c == 0x03) { s_line_len = 0; LOG("^C\n"); continue; }   /* Ctrl+C: clear the line */
        if (c == '\r' || c == '\n') {
            if (s_line_len > 0) {
                s_line[s_line_len] = '\0';
                s_line_len = 0;
                handle_line(s_line);
            }
        } else if (c == '\b' || c == 0x7F) {     /* Backspace (Tera Term / PuTTY) */
            if (s_line_len > 0) s_line_len--;
        } else if ((uint8_t)c >= 0x20 && s_line_len < sizeof(s_line) - 1) {
            s_line[s_line_len++] = c;                    /* printable characters only */
        }
    }
}

static void demo_step(void)
{
    static const struct { uint8_t op; int32_t param; } demo[] = {
        { OJT_OP_PLAY, 0 }, { OJT_OP_PAUSE, 0 }, { OJT_OP_PLAY, 0 }, { OJT_OP_NEXT, 0 }, { OJT_OP_PREVIOUS, 0 },
        { OJT_OP_SEEK_FORWARD, 10000 }, { OJT_OP_SEEK_BACKWARD, 10000 }, { OJT_OP_PLAY_INDEX, 0 },
        { OJT_OP_STOP, 0 }, { OJT_OP_GET_STATUS, 0 },
    };
    LOG("BTN  demo step %u/%u\n", s_demo_step + 1, (unsigned)(sizeof(demo) / sizeof(demo[0])));
    send_command(demo[s_demo_step].op, demo[s_demo_step].param);
    s_demo_step = (uint8_t)((s_demo_step + 1) % (sizeof(demo) / sizeof(demo[0])));
}

/* button D4 -> GND: one demo step per press (debounced) */
static void poll_button(void)
{
    bool now = digitalRead(APP_BTN_PIN);
    if (now != s_btn_last) {
        s_btn_last = now;
        s_btn_change_ms = millis();
        s_btn_done = false;
    } else if (now == LOW && !s_btn_done && millis() - s_btn_change_ms > BUTTON_DEBOUNCE_MS) {
        s_btn_done = true;
        demo_step();
    }
}

/* ------------------------------------------------------------------ */
/* public                                                              */
/* ------------------------------------------------------------------ */
void app_init(void)
{
    Serial.begin(APP_SERIAL_BAUD);
    fdev_setup_stream(&s_serial_out, serial_putc, NULL, _FDEV_SETUP_WRITE);
    stdout = &s_serial_out;                  /* printf / LOG go to the USB serial port */

    pinMode(APP_INT_PIN, INPUT_PULLUP);
    pinMode(APP_BTN_PIN, INPUT_PULLUP);
    pinMode(APP_LED_ONLINE, OUTPUT);
    pinMode(APP_LED_PLAY, OUTPUT);
    pinMode(APP_LED_ERROR, OUTPUT);
    pinMode(APP_LED_TX, OUTPUT);

    s_mcp.cs_pin  = APP_CS_PIN;
    s_mcp.osc_hz  = APP_MCP2515_OSC_HZ;
    s_mcp.bitrate = APP_CAN_BITRATE;

    LOG("\n=== OJT CAN test node (Arduino UNO, protocol v%u) ===\n", (unsigned)OJT_CAN_PROTOCOL_VERSION);
    Mcp2515Status st = mcp2515_init(&s_mcp);
    s_mcp_ok = (st == MCP2515_OK);
    if (s_mcp_ok)
        LOG("MCP2515 ready: %lu bit/s, crystal %lu Hz, NORMAL mode\n",
            (unsigned long)APP_CAN_BITRATE, (unsigned long)APP_MCP2515_OSC_HZ);
    else
    {
        LOG("MCP2515 init FAILED (code %d): check SPI wiring D10-D13, 5 V power, crystal\n", (int)st);
        print_init_hint(st);
    }
    led(APP_LED_ERROR, !s_mcp_ok);
    print_help();
}

void app_loop(void)
{
    if (s_mcp_ok && int_pin_low())           /* INT low = MCP2515 has a frame */
        poll_can_rx();

    if (s_mcp_ok)
        check_timeouts();

    poll_button();
    poll_console();
}
