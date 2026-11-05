/*
 * can_link.h - small CAN transport API used by can_service.c
 *
 * Production: can_link_socketcan.c (Linux SocketCAN, e.g. can0 = MCP2515)
 * Test only:  test/can_link_sim.c   (UNIX socket, PC test without CAN hardware)
 * Exactly one of them is linked into a binary.
 */
#ifndef CAN_LINK_H
#define CAN_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "ojt_can_protocol.h"

typedef enum {
    CAN_READ_NONE = 0,     /* nothing to read (EAGAIN)                      */
    CAN_READ_FRAME,        /* data frame in *frame (only ID 0x100 accepted) */
    CAN_READ_ERROR_FRAME,  /* controller/bus error, class bits in *err_class */
    CAN_READ_FAIL          /* interface down / socket broken -> reopen      */
} CanReadResult;

/* Error classes reported in CAN_READ_ERROR_FRAME (same bits as linux/can/error.h) */
#define CAN_LINK_ERR_TX_TIMEOUT  0x00000001u
#define CAN_LINK_ERR_LOSTARB     0x00000002u
#define CAN_LINK_ERR_CRTL        0x00000004u
#define CAN_LINK_ERR_PROT        0x00000008u
#define CAN_LINK_ERR_TRX         0x00000010u
#define CAN_LINK_ERR_ACK         0x00000020u
#define CAN_LINK_ERR_BUSOFF      0x00000040u
#define CAN_LINK_ERR_BUSERROR    0x00000080u
#define CAN_LINK_ERR_RESTARTED   0x00000100u

/* Open + bind. Returns a non-blocking fd, or -1 (errno set; ENETDOWN/ENODEV = retry later). */
int  can_link_open(const char *ifname);
CanReadResult can_link_read(int fd, OjtCanFrame *frame, uint32_t *err_class);
/* Returns false on error (errno set, e.g. ENOBUFS when nobody ACKs on the bus). */
bool can_link_write(int fd, const OjtCanFrame *frame);
void can_link_close(int fd);
const char *can_link_name(void);   /* "socketcan" or "sim" */

#endif /* CAN_LINK_H */
