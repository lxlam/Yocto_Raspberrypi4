/*
 * can_link_socketcan.c - production CAN link: Linux SocketCAN (CAN_RAW)
 *
 * Only standard, non-RTR frames with ID 0x100 (CMD) are received (kernel filter),
 * plus error frames. Own TX frames are not looped back (kernel default).
 */
#define LOG_TAG "CANLINK"
#include "log.h"
#include "can_link.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

/* our error-class bits must equal the kernel ones */
_Static_assert(CAN_LINK_ERR_TX_TIMEOUT == CAN_ERR_TX_TIMEOUT, "err bit");
_Static_assert(CAN_LINK_ERR_ACK == CAN_ERR_ACK, "err bit");
_Static_assert(CAN_LINK_ERR_BUSOFF == CAN_ERR_BUSOFF, "err bit");
_Static_assert(CAN_LINK_ERR_RESTARTED == CAN_ERR_RESTARTED, "err bit");

int can_link_open(const char *ifname)
{
    struct ifreq ifr;
    struct sockaddr_can addr;
    int fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
    if (fd < 0)
        return -1;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {          /* ENODEV: no can0 (driver/overlay) */
        int e = errno; close(fd); errno = e;
        return -1;
    }
    int ifindex = ifr.ifr_ifindex;

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_UP)) {
        close(fd); errno = ENETDOWN;                   /* exists but down: networkd not up yet */
        return -1;
    }

    /* accept only CMD: standard ID 0x100, not RTR, not extended */
    struct can_filter flt = {
        .can_id   = OJT_CAN_ID_CMD,
        .can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
    };
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, &flt, sizeof(flt));

    can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT | CAN_ERR_CRTL | CAN_ERR_PROT | CAN_ERR_TRX |
                              CAN_ERR_ACK | CAN_ERR_BUSOFF | CAN_ERR_BUSERROR | CAN_ERR_RESTARTED;
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int e = errno; close(fd); errno = e;
        return -1;
    }
    return fd;
}

CanReadResult can_link_read(int fd, OjtCanFrame *frame, uint32_t *err_class)
{
    struct can_frame cf;
    ssize_t n = read(fd, &cf, sizeof(cf));

    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? CAN_READ_NONE : CAN_READ_FAIL;
    if (n != (ssize_t)sizeof(cf))
        return CAN_READ_NONE;                          /* short read: ignore */

    if (cf.can_id & CAN_ERR_FLAG) {
        *err_class = cf.can_id & CAN_ERR_MASK;
        return CAN_READ_ERROR_FRAME;
    }

    memset(frame, 0, sizeof(*frame));
    frame->id  = cf.can_id & CAN_SFF_MASK;
    frame->dlc = cf.can_dlc > 8 ? 8 : cf.can_dlc;
    memcpy(frame->data, cf.data, frame->dlc);
    return CAN_READ_FRAME;
}

bool can_link_write(int fd, const OjtCanFrame *frame)
{
    struct can_frame cf;
    memset(&cf, 0, sizeof(cf));
    cf.can_id  = frame->id & CAN_SFF_MASK;
    cf.can_dlc = frame->dlc;
    memcpy(cf.data, frame->data, frame->dlc);
    return write(fd, &cf, sizeof(cf)) == (ssize_t)sizeof(cf);
}

void can_link_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

const char *can_link_name(void)
{
    return "socketcan";
}
