#!/bin/sh
# can-service.sh - started by can-service.service
# can0 is configured by systemd-networkd (/etc/systemd/network/80-can0.network).
# can-service waits/retries by itself if can0 is not up yet.

BIN=/usr/bin/can-service
export CAN_IFACE=${CAN_IFACE:-can0}

if [ ! -x "$BIN" ]; then
    echo "can-service.sh: $BIN not found" >&2
    exit 1
fi

# add --verbose to log every STATUS/HEALTH frame
exec "$BIN" --iface "$CAN_IFACE" "$@"
