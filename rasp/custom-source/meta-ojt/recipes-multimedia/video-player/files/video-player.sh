#!/bin/sh
# video-player.sh - started by video-player.service
# "exec" replaces this shell with the binary, so systemd tracks the real process.

BIN=/usr/bin/video-player

# Uncomment to see GStreamer warnings/errors in journalctl
# export GST_DEBUG=2

if [ ! -x "$BIN" ]; then
    echo "video-player.sh: $BIN not found" >&2
    exit 1
fi

exec "$BIN" "$@"
