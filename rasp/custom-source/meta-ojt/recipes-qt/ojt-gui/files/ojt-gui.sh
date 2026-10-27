#!/bin/sh
# ojt-gui.sh - started by ojt-gui.service
#
# xinit starts the X server on display :0 (HDMI) and then runs ojt-gui inside it.
# When ojt-gui exits, X stops too, and systemd restarts both (Restart=always).
# video-player draws the video into the GUI window on the same display :0.

GUI=/usr/bin/ojt-gui
XSERVER=/usr/bin/X

export QT_QPA_PLATFORM=xcb      # Qt draws through X11

if [ ! -x "$GUI" ]; then
    echo "ojt-gui.sh: $GUI not found" >&2
    exit 1
fi

# -s 0 -dpms : never blank the screen     (add -nocursor to hide the mouse pointer)
# -nolisten tcp : no network access to X  vt1 : use the first console
exec xinit "$GUI" "$@" -- "$XSERVER" :0 vt1 -nolisten tcp -s 0 -dpms
