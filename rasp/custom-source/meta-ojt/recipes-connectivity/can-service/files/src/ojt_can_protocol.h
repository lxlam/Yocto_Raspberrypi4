/*
 * ojt_can_protocol.h - OJT player CAN protocol v1 (CAN Classic, 11-bit IDs, DLC 8)
 *
 * SHARED FILE: the same file is used by the Raspberry Pi can-service and by
 * the STM32F411 test node. Keep both copies identical.
 *
 *   0x100 CMD       STM32 -> Pi   opcode, seq, int32 param, checksum
 *   0x101 STATUS    Pi -> STM32   state, index, position, duration, counter
 *   0x102 RESPONSE  Pi -> STM32   opcode/seq echo, result code, flags, checksum
 *   0x103 HEALTH    Pi -> STM32   USB state, track count, service flags, errors
 *
 * Multi-byte fields are little-endian (Intel byte order).
 * Plain C99, no OS dependency.
 */
#ifndef OJT_CAN_PROTOCOL_H
#define OJT_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OJT_CAN_PROTOCOL_VERSION  1u

/* ---- CAN IDs (standard 11-bit) ---- */
#define OJT_CAN_ID_CMD       0x100u
#define OJT_CAN_ID_STATUS    0x101u
#define OJT_CAN_ID_RESPONSE  0x102u
#define OJT_CAN_ID_HEALTH    0x103u
#define OJT_CAN_DLC          8u

/* ---- Timing (ms) ---- */
#define OJT_STATUS_PERIOD_MS       500u   /* STATUS: periodic + on change   */
#define OJT_HEALTH_PERIOD_MS      1000u   /* HEALTH: periodic + on change   */
#define OJT_DBUS_TIMEOUT_MS        800u   /* Pi: D-Bus call timeout          */
#define OJT_RESPONSE_TIMEOUT_MS   1500u   /* STM32: wait for RESPONSE        */
#define OJT_DUPLICATE_WINDOW_MS   2000u   /* Pi: same frame again = retry    */
#define OJT_PEER_TIMEOUT_MS       1500u   /* STM32: no STATUS -> Pi offline  */

/* ---- Limits ---- */
#define OJT_SEEK_DEFAULT_MS      10000    /* param 0 -> 10 s                 */
#define OJT_SEEK_MAX_MS         600000    /* 10 min                          */
#define OJT_INDEX_MAX            65534    /* 0xFFFF = "no index" in STATUS   */
#define OJT_INDEX_NONE          0xFFFFu

/* ---- Opcodes (CMD byte 0) ---- */
typedef enum {
    OJT_OP_PLAY          = 0x01,
    OJT_OP_PAUSE         = 0x02,
    OJT_OP_NEXT          = 0x03,
    OJT_OP_PREVIOUS      = 0x04,
    OJT_OP_SEEK_FORWARD  = 0x05,   /* param = step ms (0 -> default) */
    OJT_OP_SEEK_BACKWARD = 0x06,   /* param = step ms (0 -> default) */
    OJT_OP_STOP          = 0x07,
    OJT_OP_PLAY_INDEX    = 0x08,   /* param = playlist index         */
    OJT_OP_GET_STATUS    = 0x09
} OjtOpcode;

/* ---- Result codes (RESPONSE byte 2) ---- */
typedef enum {
    OJT_RES_OK                 = 0x00,  /* frame valid AND D-Bus method returned true */
    OJT_RES_REJECTED           = 0x01,  /* D-Bus method returned false                */
    OJT_RES_BAD_DLC            = 0x02,
    OJT_RES_BAD_OPCODE         = 0x03,
    OJT_RES_BAD_CHECKSUM       = 0x04,
    OJT_RES_OUT_OF_RANGE       = 0x05,
    OJT_RES_PLAYER_UNAVAILABLE = 0x06,  /* org.automotive.VideoPlayer not on the bus  */
    OJT_RES_DBUS_ERROR         = 0x07,  /* D-Bus error reply or timeout               */
    OJT_RES_BUSY               = 0x08   /* previous command still waiting for D-Bus   */
} OjtResult;

/* RESPONSE byte 3 flags */
#define OJT_RESP_FLAG_DUPLICATE  0x01u  /* cached response re-sent, command NOT re-executed */

/* ---- Player state (STATUS byte 0) ---- */
typedef enum {
    OJT_STATE_STOPPED = 0x00,
    OJT_STATE_PLAYING = 0x01,
    OJT_STATE_PAUSED  = 0x02,
    OJT_STATE_ERROR   = 0x03,
    OJT_STATE_UNKNOWN = 0xFF   /* video-player not available */
} OjtPlayerState;

/* ---- USB state (HEALTH byte 0) ---- */
typedef enum {
    OJT_USB_NO_MEDIA = 0x00,
    OJT_USB_SCANNING = 0x01,
    OJT_USB_READY    = 0x02,
    OJT_USB_UNKNOWN  = 0xFF    /* media-manager not available */
} OjtUsbState;

/* HEALTH byte 3 flags */
#define OJT_HEALTH_PLAYER_UP  0x01u
#define OJT_HEALTH_MEDIA_UP   0x02u
#define OJT_HEALTH_DBUS_UP    0x04u

/* ---- Frame + decoded messages ---- */
typedef struct {
    uint32_t id;        /* 11-bit standard ID */
    uint8_t  dlc;       /* 0..8               */
    uint8_t  data[8];
} OjtCanFrame;

typedef struct {
    uint8_t opcode;
    uint8_t seq;
    int32_t param;
} OjtCommand;

typedef struct {
    uint8_t opcode;
    uint8_t seq;
    uint8_t result;     /* OjtResult            */
    uint8_t flags;      /* OJT_RESP_FLAG_*      */
} OjtResponse;

typedef struct {
    uint8_t  state;          /* OjtPlayerState        */
    uint16_t index;          /* OJT_INDEX_NONE = none */
    uint16_t position_s;     /* saturates at 65535    */
    uint16_t duration_s;
    uint8_t  counter;        /* rolling               */
} OjtStatus;

typedef struct {
    uint8_t  usb_state;      /* OjtUsbState           */
    uint16_t track_count;
    uint8_t  flags;          /* OJT_HEALTH_*          */
    uint8_t  last_error;     /* last non-OK OjtResult */
    uint8_t  rejected_frames;/* saturates at 255      */
    uint8_t  counter;        /* rolling               */
} OjtHealth;

/* XOR of data[0..6], stored in data[7] of CMD and RESPONSE */
uint8_t ojt_checksum(const uint8_t data[8]);

/* Encoders always produce DLC 8. */
void ojt_encode_command(OjtCanFrame *f, uint8_t opcode, uint8_t seq, int32_t param);
void ojt_encode_response(OjtCanFrame *f, const OjtResponse *r);
void ojt_encode_status(OjtCanFrame *f, const OjtStatus *s);
void ojt_encode_health(OjtCanFrame *f, const OjtHealth *h);

/*
 * Validate + decode a CMD frame (order: DLC, checksum, opcode, param range).
 * Returns OJT_RES_OK or the error code to send back. On error, opcode/seq are
 * still filled when the frame has enough bytes (0 otherwise).
 */
OjtResult ojt_decode_command(const OjtCanFrame *f, OjtCommand *cmd);

/* Decoders for the STM32 side. Return false on wrong ID/DLC/checksum. */
bool ojt_decode_response(const OjtCanFrame *f, OjtResponse *r);
bool ojt_decode_status(const OjtCanFrame *f, OjtStatus *s);
bool ojt_decode_health(const OjtCanFrame *f, OjtHealth *h);

/* Seek step to use for SEEK_* (param 0 -> default). */
int32_t ojt_seek_step_ms(int32_t param);

const char *ojt_opcode_name(uint8_t opcode);
const char *ojt_result_name(uint8_t result);
const char *ojt_state_name(uint8_t state);
const char *ojt_usb_name(uint8_t usb_state);

#ifdef __cplusplus
}
#endif

#endif /* OJT_CAN_PROTOCOL_H */
