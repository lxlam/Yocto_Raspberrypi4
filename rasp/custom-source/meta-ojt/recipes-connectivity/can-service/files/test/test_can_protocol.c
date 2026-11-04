/* test_can_protocol.c - TEST ONLY: unit tests for ojt_can_protocol.c (host) */
#include "ojt_can_protocol.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, name) do { if (cond) { g_pass++; } else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } } while (0)

static void hex(const OjtCanFrame *f, char *out)
{
    int n = sprintf(out, "%03X#", (unsigned)f->id);
    for (int i = 0; i < f->dlc; i++) n += sprintf(out + n, "%02X", f->data[i]);
}

int main(void)
{
    OjtCanFrame f;
    OjtCommand c;
    char buf[64];

    /* encode PLAY seq 1: 01 01 00000000 00 cs=00 */
    ojt_encode_command(&f, OJT_OP_PLAY, 1, 0);
    hex(&f, buf);
    CHECK(strcmp(buf, "100#0101000000000000") == 0, "encode PLAY");
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OK && c.opcode == OJT_OP_PLAY && c.seq == 1, "decode PLAY");

    /* PLAY_INDEX 2 seq 7, little-endian param */
    ojt_encode_command(&f, OJT_OP_PLAY_INDEX, 7, 2);
    hex(&f, buf);
    CHECK(strcmp(buf, "100#080702000000000D") == 0, "encode PLAY_INDEX 2 (LE + checksum)");
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OK && c.param == 2, "decode PLAY_INDEX");

    /* SEEK_FORWARD 10000 ms = 0x2710 */
    ojt_encode_command(&f, OJT_OP_SEEK_FORWARD, 3, 10000);
    hex(&f, buf);
    CHECK(strcmp(buf, "100#0503102700000031") == 0, "encode SEEK_FWD 10000");
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OK && c.param == 10000, "decode SEEK_FWD");
    CHECK(ojt_seek_step_ms(0) == OJT_SEEK_DEFAULT_MS && ojt_seek_step_ms(500) == 500, "seek default step");

    /* errors, in priority order */
    ojt_encode_command(&f, OJT_OP_PLAY, 9, 0);
    f.dlc = 5;
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_BAD_DLC && c.opcode == OJT_OP_PLAY && c.seq == 9, "bad DLC keeps op/seq");
    f.dlc = 0;
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_BAD_DLC && c.opcode == 0 && c.seq == 0, "DLC 0");

    ojt_encode_command(&f, OJT_OP_PLAY, 9, 0);
    f.data[7] ^= 0xFF;
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_BAD_CHECKSUM, "bad checksum");

    ojt_encode_command(&f, 0x55, 9, 0);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_BAD_OPCODE, "unknown opcode 0x55");
    ojt_encode_command(&f, 0x00, 9, 0);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_BAD_OPCODE, "opcode 0x00");

    ojt_encode_command(&f, OJT_OP_PLAY_INDEX, 1, -1);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OUT_OF_RANGE, "PLAY_INDEX -1");
    ojt_encode_command(&f, OJT_OP_PLAY_INDEX, 1, OJT_INDEX_MAX);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OK, "PLAY_INDEX max");
    ojt_encode_command(&f, OJT_OP_PLAY_INDEX, 1, OJT_INDEX_MAX + 1);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OUT_OF_RANGE, "PLAY_INDEX max+1");
    ojt_encode_command(&f, OJT_OP_SEEK_BACKWARD, 1, OJT_SEEK_MAX_MS);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OK, "SEEK max");
    ojt_encode_command(&f, OJT_OP_SEEK_BACKWARD, 1, OJT_SEEK_MAX_MS + 1);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OUT_OF_RANGE, "SEEK max+1");
    ojt_encode_command(&f, OJT_OP_SEEK_FORWARD, 1, -5);
    CHECK(ojt_decode_command(&f, &c) == OJT_RES_OUT_OF_RANGE, "SEEK negative");

    /* response round trip */
    OjtResponse r = { OJT_OP_NEXT, 42, OJT_RES_REJECTED, OJT_RESP_FLAG_DUPLICATE }, r2;
    ojt_encode_response(&f, &r);
    hex(&f, buf);
    CHECK(strcmp(buf, "102#032A010100000029") == 0, "encode RESPONSE");
    CHECK(ojt_decode_response(&f, &r2) && r2.seq == 42 && r2.result == OJT_RES_REJECTED && r2.flags == 1, "decode RESPONSE");
    f.data[2] = 0;
    CHECK(!ojt_decode_response(&f, &r2), "RESPONSE bad checksum rejected");

    /* status round trip: PLAYING idx 2, 75 s / 300 s, counter 9 */
    OjtStatus s = { OJT_STATE_PLAYING, 2, 75, 300, 9 }, s2;
    ojt_encode_status(&f, &s);
    hex(&f, buf);
    CHECK(strcmp(buf, "101#0102004B002C0109") == 0, "encode STATUS");
    CHECK(ojt_decode_status(&f, &s2) && s2.state == OJT_STATE_PLAYING && s2.index == 2 &&
          s2.position_s == 75 && s2.duration_s == 300 && s2.counter == 9, "decode STATUS");

    /* health round trip */
    OjtHealth h = { OJT_USB_READY, 3, OJT_HEALTH_PLAYER_UP | OJT_HEALTH_MEDIA_UP | OJT_HEALTH_DBUS_UP,
                    OJT_RES_REJECTED, 4, 5 }, h2;
    ojt_encode_health(&f, &h);
    hex(&f, buf);
    CHECK(strcmp(buf, "103#0203000701040005") == 0, "encode HEALTH");
    CHECK(ojt_decode_health(&f, &h2) && h2.usb_state == OJT_USB_READY && h2.track_count == 3 &&
          h2.flags == 7 && h2.rejected_frames == 4, "decode HEALTH");

    /* wrong ID for decoders */
    f.id = 0x200;
    CHECK(!ojt_decode_health(&f, &h2) && !ojt_decode_status(&f, &s2), "wrong ID rejected");

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
