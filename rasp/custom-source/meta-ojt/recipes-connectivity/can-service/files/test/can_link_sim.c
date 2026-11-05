/*
 * can_link_sim.c - TEST ONLY: replaces can_link_socketcan.c on a PC without CAN.
 *
 * Connects to a UNIX SOCK_SEQPACKET socket (path in $OJT_CAN_SIM_SOCKET,
 * default /tmp/ojt-can-sim.sock) where test/can_integration_test.py plays the test node.
 * One packet = one frame, 16 bytes like struct can_frame:
 *   uint32 id (bit 29 = error frame), uint8 dlc, 3 pad bytes, uint8 data[8]
 * Same filter as production: only ID 0x100 data frames are passed up.
 */
#include "can_link.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SIM_ERR_FLAG 0x20000000u

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  pad[3];
    uint8_t  data[8];
} SimFrame;

int can_link_open(const char *ifname)
{
    (void)ifname;
    const char *path = getenv("OJT_CAN_SIM_SOCKET");
    if (!path || !*path)
        path = "/tmp/ojt-can-sim.sock";

    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int e = errno; close(fd); errno = (e == ENOENT || e == ECONNREFUSED) ? ENETDOWN : e;
        return -1;
    }
    return fd;
}

CanReadResult can_link_read(int fd, OjtCanFrame *frame, uint32_t *err_class)
{
    SimFrame sf;
    ssize_t n = recv(fd, &sf, sizeof(sf), 0);
    if (n == 0) { errno = ENETDOWN; return CAN_READ_FAIL; }      /* peer closed = "bus down" */
    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? CAN_READ_NONE : CAN_READ_FAIL;
    if (n != (ssize_t)sizeof(sf))
        return CAN_READ_NONE;

    if (sf.id & SIM_ERR_FLAG) {
        *err_class = sf.id & 0x1FFFFFFFu;
        return CAN_READ_ERROR_FRAME;
    }
    if ((sf.id & 0x7FFu) != OJT_CAN_ID_CMD || (sf.id & ~0x7FFu))
        return CAN_READ_NONE;                                    /* filtered like CAN_RAW_FILTER */

    memset(frame, 0, sizeof(*frame));
    frame->id  = sf.id;
    frame->dlc = sf.dlc > 8 ? 8 : sf.dlc;
    memcpy(frame->data, sf.data, frame->dlc);
    return CAN_READ_FRAME;
}

bool can_link_write(int fd, const OjtCanFrame *frame)
{
    SimFrame sf;
    memset(&sf, 0, sizeof(sf));
    sf.id  = frame->id;
    sf.dlc = frame->dlc;
    memcpy(sf.data, frame->data, frame->dlc);
    return send(fd, &sf, sizeof(sf), MSG_NOSIGNAL) == (ssize_t)sizeof(sf);
}

void can_link_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

const char *can_link_name(void)
{
    return "sim";
}
