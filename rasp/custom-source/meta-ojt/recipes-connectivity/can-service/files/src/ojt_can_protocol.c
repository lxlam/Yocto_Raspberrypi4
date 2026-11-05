/*
 * ojt_can_protocol.c - encode/decode for OJT player CAN protocol v1
 * SHARED FILE (Raspberry Pi can-service + Arduino UNO test node). Keep identical.
 */
#include "ojt_can_protocol.h"

#include <string.h>

/* little-endian helpers */
static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get_u16(const uint8_t *p)   { return (uint16_t)(p[0] | (p[1] << 8)); }
static void put_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)u; p[1] = (uint8_t)(u >> 8); p[2] = (uint8_t)(u >> 16); p[3] = (uint8_t)(u >> 24);
}
static int32_t get_i32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (int32_t)u;
}

static void frame_init(OjtCanFrame *f, uint32_t id)
{
    memset(f, 0, sizeof(*f));
    f->id  = id;
    f->dlc = OJT_CAN_DLC;
}

uint8_t ojt_checksum(const uint8_t data[8])
{
    uint8_t x = 0;
    for (int i = 0; i < 7; i++)
        x ^= data[i];
    return x;
}

void ojt_encode_command(OjtCanFrame *f, uint8_t opcode, uint8_t seq, int32_t param)
{
    frame_init(f, OJT_CAN_ID_CMD);
    f->data[0] = opcode;
    f->data[1] = seq;
    put_i32(&f->data[2], param);
    f->data[6] = 0;                       /* reserved */
    f->data[7] = ojt_checksum(f->data);
}

void ojt_encode_response(OjtCanFrame *f, const OjtResponse *r)
{
    frame_init(f, OJT_CAN_ID_RESPONSE);
    f->data[0] = r->opcode;
    f->data[1] = r->seq;
    f->data[2] = r->result;
    f->data[3] = r->flags;
    f->data[7] = ojt_checksum(f->data);
}

void ojt_encode_status(OjtCanFrame *f, const OjtStatus *s)
{
    frame_init(f, OJT_CAN_ID_STATUS);
    f->data[0] = s->state;
    put_u16(&f->data[1], s->index);
    put_u16(&f->data[3], s->position_s);
    put_u16(&f->data[5], s->duration_s);
    f->data[7] = s->counter;
}

void ojt_encode_health(OjtCanFrame *f, const OjtHealth *h)
{
    frame_init(f, OJT_CAN_ID_HEALTH);
    f->data[0] = h->usb_state;
    put_u16(&f->data[1], h->track_count);
    f->data[3] = h->flags;
    f->data[4] = h->last_error;
    f->data[5] = h->rejected_frames;
    f->data[7] = h->counter;
}

int32_t ojt_seek_step_ms(int32_t param)
{
    return (param == 0) ? OJT_SEEK_DEFAULT_MS : param;
}

OjtResult ojt_decode_command(const OjtCanFrame *f, OjtCommand *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    if (f->dlc >= 1) cmd->opcode = f->data[0];
    if (f->dlc >= 2) cmd->seq    = f->data[1];

    if (f->dlc != OJT_CAN_DLC)
        return OJT_RES_BAD_DLC;
    if (ojt_checksum(f->data) != f->data[7])
        return OJT_RES_BAD_CHECKSUM;

    cmd->param = get_i32(&f->data[2]);

    switch (cmd->opcode) {
    case OJT_OP_PLAY:
    case OJT_OP_PAUSE:
    case OJT_OP_NEXT:
    case OJT_OP_PREVIOUS:
    case OJT_OP_STOP:
    case OJT_OP_GET_STATUS:
        return OJT_RES_OK;                /* param is ignored */
    case OJT_OP_SEEK_FORWARD:
    case OJT_OP_SEEK_BACKWARD:
        if (cmd->param < 0 || cmd->param > OJT_SEEK_MAX_MS)
            return OJT_RES_OUT_OF_RANGE;
        return OJT_RES_OK;
    case OJT_OP_PLAY_INDEX:
        if (cmd->param < 0 || cmd->param > OJT_INDEX_MAX)
            return OJT_RES_OUT_OF_RANGE;
        return OJT_RES_OK;
    default:
        return OJT_RES_BAD_OPCODE;
    }
}

bool ojt_decode_response(const OjtCanFrame *f, OjtResponse *r)
{
    if (f->id != OJT_CAN_ID_RESPONSE || f->dlc != OJT_CAN_DLC || ojt_checksum(f->data) != f->data[7])
        return false;
    r->opcode = f->data[0];
    r->seq    = f->data[1];
    r->result = f->data[2];
    r->flags  = f->data[3];
    return true;
}

bool ojt_decode_status(const OjtCanFrame *f, OjtStatus *s)
{
    if (f->id != OJT_CAN_ID_STATUS || f->dlc != OJT_CAN_DLC)
        return false;
    s->state      = f->data[0];
    s->index      = get_u16(&f->data[1]);
    s->position_s = get_u16(&f->data[3]);
    s->duration_s = get_u16(&f->data[5]);
    s->counter    = f->data[7];
    return true;
}

bool ojt_decode_health(const OjtCanFrame *f, OjtHealth *h)
{
    if (f->id != OJT_CAN_ID_HEALTH || f->dlc != OJT_CAN_DLC)
        return false;
    h->usb_state       = f->data[0];
    h->track_count     = get_u16(&f->data[1]);
    h->flags           = f->data[3];
    h->last_error      = f->data[4];
    h->rejected_frames = f->data[5];
    h->counter         = f->data[7];
    return true;
}

const char *ojt_opcode_name(uint8_t opcode)
{
    switch (opcode) {
    case OJT_OP_PLAY:          return "PLAY";
    case OJT_OP_PAUSE:         return "PAUSE";
    case OJT_OP_NEXT:          return "NEXT";
    case OJT_OP_PREVIOUS:      return "PREVIOUS";
    case OJT_OP_SEEK_FORWARD:  return "SEEK_FORWARD";
    case OJT_OP_SEEK_BACKWARD: return "SEEK_BACKWARD";
    case OJT_OP_STOP:          return "STOP";
    case OJT_OP_PLAY_INDEX:    return "PLAY_INDEX";
    case OJT_OP_GET_STATUS:    return "GET_STATUS";
    default:                   return "UNKNOWN";
    }
}

const char *ojt_result_name(uint8_t result)
{
    switch (result) {
    case OJT_RES_OK:                 return "OK";
    case OJT_RES_REJECTED:           return "REJECTED";
    case OJT_RES_BAD_DLC:            return "BAD_DLC";
    case OJT_RES_BAD_OPCODE:         return "BAD_OPCODE";
    case OJT_RES_BAD_CHECKSUM:       return "BAD_CHECKSUM";
    case OJT_RES_OUT_OF_RANGE:       return "OUT_OF_RANGE";
    case OJT_RES_PLAYER_UNAVAILABLE: return "PLAYER_UNAVAILABLE";
    case OJT_RES_DBUS_ERROR:         return "DBUS_ERROR";
    case OJT_RES_BUSY:               return "BUSY";
    default:                         return "UNKNOWN";
    }
}

const char *ojt_state_name(uint8_t state)
{
    switch (state) {
    case OJT_STATE_STOPPED: return "STOPPED";
    case OJT_STATE_PLAYING: return "PLAYING";
    case OJT_STATE_PAUSED:  return "PAUSED";
    case OJT_STATE_ERROR:   return "ERROR";
    default:                return "UNKNOWN";
    }
}

const char *ojt_usb_name(uint8_t usb_state)
{
    switch (usb_state) {
    case OJT_USB_NO_MEDIA: return "NO_MEDIA";
    case OJT_USB_SCANNING: return "SCANNING";
    case OJT_USB_READY:    return "READY";
    default:               return "UNKNOWN";
    }
}
