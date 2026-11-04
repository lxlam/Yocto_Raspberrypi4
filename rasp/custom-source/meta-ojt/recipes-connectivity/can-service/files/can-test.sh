#!/bin/sh
# can-test.sh - TEST ONLY (package can-service-test): send OJT CAN commands with
# cansend and decode replies with candump. Acts like the STM32 node.
#
#   can-test.sh play | pause | stop | next | prev | status
#   can-test.sh index <n>          PLAY_INDEX n
#   can-test.sh ff [ms] | rew [ms] SEEK forward / backward (0 = 10 s default)
#   can-test.sh raw <hexbytes>     send any payload on 0x100 (e.g. bad DLC/checksum)
#   can-test.sh monitor            decode RESPONSE/STATUS/HEALTH (Ctrl+C to stop)
#   can-test.sh link               show can0 state, bitrate, error counters
# Env: IFACE (default can0), DRY=1 (print the cansend command only)

IFACE=${IFACE:-can0}
SEQ_FILE=/tmp/can-test.seq

next_seq() {
    s=$(cat "$SEQ_FILE" 2>/dev/null || echo 0)
    s=$(( (s + 1) & 255 ))
    echo "$s" > "$SEQ_FILE"
    echo "$s"
}

# cmd <opcode> [param]  -> 8 bytes: op seq p0 p1 p2 p3 00 xor
cmd() {
    op=$1; p=${2:-0}
    [ "$p" -lt 0 ] && p=$(( p + 4294967296 ))
    seq=$(next_seq)
    b2=$(( p & 255 )); b3=$(( (p >> 8) & 255 )); b4=$(( (p >> 16) & 255 )); b5=$(( (p >> 24) & 255 ))
    cs=$(( op ^ seq ^ b2 ^ b3 ^ b4 ^ b5 ))
    frame=$(printf "100#%02X%02X%02X%02X%02X%02X00%02X" "$op" "$seq" "$b2" "$b3" "$b4" "$b5" "$cs")
    send "$frame"
}

send() {
    echo "TX $1"
    if [ "${DRY:-0}" = "1" ]; then return 0; fi
    cansend "$IFACE" "$1"
}

decode() {
    # candump -L line: (time) can0 102#0101000000000000
    awk '
    function hx(c) { return index("0123456789ABCDEF", toupper(c)) - 1 }
    function hb(s, i) { return 16 * hx(substr(s, 2*i+1, 1)) + hx(substr(s, 2*i+2, 1)) }
    {
        split($3, a, "#"); id = a[1]; d = a[2]
        if (id == "102") {
            split("OK REJECTED BAD_DLC BAD_OPCODE BAD_CHECKSUM OUT_OF_RANGE PLAYER_UNAVAILABLE DBUS_ERROR BUSY", R, " ")
            split("PLAY PAUSE NEXT PREVIOUS SEEK_FORWARD SEEK_BACKWARD STOP PLAY_INDEX GET_STATUS", O, " ")
            r = hb(d, 2); printf "RESP   %-13s seq=%-3d %s%s\n", O[hb(d,0)], hb(d,1), (r in R) ? "" : r, R[r+1] (hb(d,3) ? " (duplicate)" : "")
        } else if (id == "101") {
            st = hb(d,0); split("STOPPED PLAYING PAUSED ERROR", S, " ")
            idx = hb(d,1) + 256*hb(d,2)
            printf "STATUS %-8s index=%s pos=%ds dur=%ds cnt=%d\n", (st == 255) ? "UNKNOWN" : S[st+1],
                   (idx == 65535) ? "none" : idx, hb(d,3)+256*hb(d,4), hb(d,5)+256*hb(d,6), hb(d,7)
        } else if (id == "103") {
            u = hb(d,0); split("NO_MEDIA SCANNING READY", U, " ")
            printf "HEALTH usb=%-8s tracks=%d flags=0x%02X last_err=%d rejected=%d cnt=%d\n", (u == 255) ? "UNKNOWN" : U[u+1],
                   hb(d,1)+256*hb(d,2), hb(d,3), hb(d,4), hb(d,5), hb(d,7)
        } else print
        fflush()
    }'
}

case "$1" in
    play)    cmd 1 ;;
    pause)   cmd 2 ;;
    next)    cmd 3 ;;
    prev)    cmd 4 ;;
    ff)      cmd 5 "${2:-0}" ;;
    rew)     cmd 6 "${2:-0}" ;;
    stop)    cmd 7 ;;
    index)   cmd 8 "${2:-0}" ;;
    status)  cmd 9 ;;
    raw)     send "100#$2" ;;
    monitor) candump -L "$IFACE,101:7FC" | decode ;;
    link)    ip -details -statistics link show "$IFACE" ;;
    decode)  decode ;;
    *)       sed -n '2,13p' "$0"; exit 1 ;;
esac
