#!/bin/sh
# media-manager.sh - started by media-manager.service
# "exec" replaces this shell with the binary, so systemd tracks the real process.

BIN=/usr/bin/ojt-media-manager

if [ ! -x "$BIN" ]; then
    echo "media-manager.sh: $BIN not found" >&2
    exit 1
fi

exec "$BIN" "$@"
