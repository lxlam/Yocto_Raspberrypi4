#!/bin/sh
# media-manager-test.sh - act like the Player Core using gdbus (no player needed)
#
# On target:   media-manager-test.sh <command>
# On PC:       BUS=--session ./media-manager-test.sh <command>
#
# Commands:
#   monitor            watch all signals (run this in a 2nd terminal)
#   state              GetState
#   playlist           GetPlaylist
#   track <id>         GetTrack
#   rescan             Rescan
#   sim-add [dir]      SimulateUsb add  (default dir /tmp/usb_test)
#   sim-remove         SimulateUsb remove
#   demo               create fake videos in /tmp/usb_test and run all steps

BUS=${BUS:---system}
DEST=org.automotive.MediaManager
OBJ=/org/automotive/MediaManager
IFACE=org.automotive.MediaManager1

call() {
    method=$1
    shift
    echo ">> $method $*"
    gdbus call $BUS --dest $DEST --object-path $OBJ --method "$IFACE.$method" "$@"
}

make_fake_usb() {
    mkdir -p /tmp/usb_test/movies
    echo "fake" > "/tmp/usb_test/b_video.mp4"
    echo "fake" > "/tmp/usb_test/a_video.MKV"
    echo "fake" > "/tmp/usb_test/movies/c_video.mp4"
    echo "fake" > "/tmp/usb_test/note.txt"          # must be ignored
    echo "fake" > "/tmp/usb_test/.hidden.mp4"       # must be ignored
}

case "$1" in
    monitor)    gdbus monitor $BUS --dest $DEST ;;
    state)      call GetState ;;
    playlist)   call GetPlaylist ;;
    track)      call GetTrack "${2:-0}" ;;
    rescan)     call Rescan ;;
    sim-add)    call SimulateUsb add "${2:-/tmp/usb_test}" ;;
    sim-remove) call SimulateUsb remove "" ;;
    demo)
        make_fake_usb
        call GetState
        call SimulateUsb add /tmp/usb_test
        call GetState
        call GetPlaylist
        call GetTrack 0
        call GetTrack 99
        call SimulateUsb remove ""
        call GetState
        call GetPlaylist
        ;;
    *)
        sed -n '2,16p' "$0"
        exit 1
        ;;
esac
