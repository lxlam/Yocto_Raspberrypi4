#!/bin/sh
# video-player.sh - started by video-player.service
# "exec" replaces this shell with the binary, so systemd tracks the real process.

BIN=/usr/bin/video-player

# Uncomment to see GStreamer warnings/errors in journalctl
# export GST_DEBUG=2

# Video sink. Empty = auto (xvimagesink -> glimagesink -> ximagesink).
# Force one if video is black/slow, e.g.: export OJT_VIDEO_SINK=glimagesink
# export OJT_VIDEO_SINK=

# X server display used to draw the video (started by ojt-gui)
export DISPLAY=${DISPLAY:-:0}

if [ ! -x "$BIN" ]; then
    echo "video-player.sh: $BIN not found" >&2
    exit 1
fi

exec "$BIN" "$@"
