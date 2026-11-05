#!/bin/sh
# TEST ONLY: run can_integration_test.py on a PC (session bus + virtual X).
# usage: run_pc_integration.sh <can-service-sim> <video-player> <ojt-media-manager> <media-manager-test.sh> <usb_dir>
HERE=$(cd "$(dirname "$0")" && pwd)
export CAN_SIM_BIN=$1 VP_BIN=$2 MM_BIN=$3 MM_TEST=$4 USB_DIR=$5
export OJT_VIDEO_SINK=${OJT_VIDEO_SINK:-ximagesink}
Xvfb :95 -screen 0 800x480x24 >/dev/null 2>&1 &
XV=$!
sleep 1
DISPLAY=:95 dbus-run-session -- python3 "$HERE/can_integration_test.py"
RC=$?
kill $XV
exit $RC
