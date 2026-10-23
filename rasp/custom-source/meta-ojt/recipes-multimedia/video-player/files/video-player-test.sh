#!/bin/sh
# video-player-test.sh - act like the GUI / CAN service using gdbus
#
# On target:   video-player-test.sh <command>
# On PC:       BUS=--session ./video-player-test.sh <command>
#
# Commands:
#   monitor        watch StatusChanged signals (run in a 2nd terminal)
#   status         GetStatus -> (state, index, title, position_ms, duration_ms)
#   play | pause | stop | next | prev
#   index <n>      PlayIndex(n)
#   seek <ms>      Seek relative, e.g. seek 10000 / seek -10000

BUS=${BUS:---system}
DEST=org.automotive.VideoPlayer
OBJ=/org/automotive/VideoPlayer
IFACE=org.automotive.VideoPlayer1

call() {
    method=$1
    shift
    echo ">> $method $*"
    gdbus call $BUS --dest $DEST --object-path $OBJ --method "$IFACE.$method" "$@"
}

case "$1" in
    monitor) gdbus monitor $BUS --dest $DEST ;;
    status)  call GetStatus ;;
    play)    call Play ;;
    pause)   call Pause ;;
    stop)    call Stop ;;
    next)    call Next ;;
    prev)    call Previous ;;
    index)   call PlayIndex "${2:-0}" ;;
    seek)    call Seek -- "${2:-10000}" ;;
    *)
        sed -n '2,13p' "$0"
        exit 1
        ;;
esac
