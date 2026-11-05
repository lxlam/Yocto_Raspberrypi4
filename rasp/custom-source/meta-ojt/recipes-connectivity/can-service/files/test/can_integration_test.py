#!/usr/bin/env python3
"""
can_integration_test.py - TEST ONLY (PC, no CAN hardware)

Plays the CAN test node over the simulated CAN link (test/can_link_sim.c) and
checks the full chain:  CAN CMD -> can-service-sim -> D-Bus -> video-player
                        -> CAN RESPONSE / STATUS / HEALTH

Needs (env): CAN_SIM_BIN, VP_BIN, MM_BIN, MM_TEST (media-manager-test.sh),
             USB_DIR (folder with >= 3 videos), and a session D-Bus + DISPLAY.
Run via test/run_pc_integration.sh.
"""
import os, socket, struct, subprocess, sys, threading, time, queue

SOCK = os.environ.get("OJT_CAN_SIM_SOCKET", "/tmp/ojt-can-sim.sock")
CMD, STATUS, RESP, HEALTH = 0x100, 0x101, 0x102, 0x103
OP = dict(PLAY=1, PAUSE=2, NEXT=3, PREVIOUS=4, SEEK_FORWARD=5, SEEK_BACKWARD=6, STOP=7, PLAY_INDEX=8, GET_STATUS=9)
RES = {0: "OK", 1: "REJECTED", 2: "BAD_DLC", 3: "BAD_OPCODE", 4: "BAD_CHECKSUM", 5: "OUT_OF_RANGE",
       6: "PLAYER_UNAVAILABLE", 7: "DBUS_ERROR", 8: "BUSY"}
ST = {0: "STOPPED", 1: "PLAYING", 2: "PAUSED", 3: "ERROR", 0xFF: "UNKNOWN"}
USB = {0: "NO_MEDIA", 1: "SCANNING", 2: "READY", 0xFF: "UNKNOWN"}
FMT = "<IB3x8s"          # same as struct can_frame


def checksum(d):
    x = 0
    for b in d[:7]:
        x ^= b
    return x


def cmd_bytes(op, seq, param=0):
    d = bytearray(struct.pack("<BBiB", op, seq & 0xFF, param, 0)) + b"\x00"
    d[7] = checksum(d)
    return bytes(d)


def hexs(b):
    return "".join("%02X" % x for x in b)


class NodeSim:
    def __init__(self):
        if os.path.exists(SOCK):
            os.unlink(SOCK)
        self.srv = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.srv.bind(SOCK)
        self.srv.listen(1)
        self.conn = None
        self.resp = queue.Queue()
        self.status = None
        self.status_times = []
        self.health = None
        self.lock = threading.Lock()
        self.seq = 0
        self.rx_log = []

    def accept(self, timeout=10):
        self.srv.settimeout(timeout)
        self.conn, _ = self.srv.accept()
        threading.Thread(target=self._rx, args=(self.conn,), daemon=True).start()

    def close_link(self):                       # simulate "bus down"
        c, self.conn = self.conn, None
        if c:
            c.close()

    def _rx(self, conn):
        while True:
            try:
                pkt = conn.recv(16)
            except OSError:
                return
            if not pkt:
                return
            cid, dlc, data = struct.unpack(FMT, pkt)
            data = data[:dlc]
            self.rx_log.append((time.time(), cid, data))
            if cid == RESP:
                self.resp.put(data)
            elif cid == STATUS:
                with self.lock:
                    self.status = dict(state=ST.get(data[0], data[0]), index=data[1] | data[2] << 8,
                                       pos=data[3] | data[4] << 8, dur=data[5] | data[6] << 8, counter=data[7])
                    self.status_times.append(time.time())
            elif cid == HEALTH:
                with self.lock:
                    self.health = dict(usb=USB.get(data[0], data[0]), tracks=data[1] | data[2] << 8,
                                       flags=data[3], last_error=RES.get(data[4], data[4]),
                                       rejected=data[5], counter=data[7])

    def send_raw(self, data, cid=CMD):
        pkt = struct.pack(FMT, cid, len(data), bytes(data).ljust(8, b"\x00"))
        self.conn.send(pkt)

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFF
        return self.seq

    def drain(self):
        while not self.resp.empty():
            self.resp.get_nowait()

    def command(self, op, param=0, seq=None, timeout=1.5):
        """Send CMD, return (response dict or None, latency ms)."""
        seq = self.next_seq() if seq is None else seq
        self.drain()
        t0 = time.time()
        self.send_raw(cmd_bytes(op, seq, param))
        return self.wait_resp(t0, timeout)

    def wait_resp(self, t0, timeout=1.5):
        try:
            d = self.resp.get(timeout=timeout)
        except queue.Empty:
            return None, None
        ms = (time.time() - t0) * 1000
        return dict(op=d[0], seq=d[1], result=RES.get(d[2], d[2]), flags=d[3], ok_cs=(checksum(d) == d[7]),
                    raw=hexs(d)), ms

    def wait_status(self, pred, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                s = dict(self.status) if self.status else None
            if s and pred(s):
                return s
            time.sleep(0.05)
        with self.lock:
            return dict(self.status) if self.status else None

    def wait_health(self, pred, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                h = dict(self.health) if self.health else None
            if h and pred(h):
                return h
            time.sleep(0.05)
        with self.lock:
            return dict(self.health) if self.health else None


RESULTS = []


def record(tid, name, ok, actual):
    RESULTS.append((tid, name, "PASS" if ok else "FAIL", actual))
    print("%-4s %-4s %-52s %s" % (tid, "PASS" if ok else "FAIL", name, actual), flush=True)


def mm(*args):
    subprocess.run(["sh", os.environ["MM_TEST"], *args], env=dict(os.environ, BUS="--session"),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)


def start(binary, *args, log):
    return subprocess.Popen([binary, *args], stdout=open(log, "w"), stderr=subprocess.STDOUT)


def main():
    usb = os.environ["USB_DIR"]
    sim = NodeSim()
    procs = {}
    procs["mm"] = start(os.environ["MM_BIN"], "--session", log="/tmp/it_mm.log")
    procs["vp"] = start(os.environ["VP_BIN"], "--session", log="/tmp/it_vp.log")
    time.sleep(1.0)
    procs["can"] = start(os.environ["CAN_SIM_BIN"], "--session", log="/tmp/it_can.log")
    sim.accept()
    time.sleep(1.0)

    try:
        # ---------- no USB ----------
        r, ms = sim.command(OP["GET_STATUS"])
        s = sim.wait_status(lambda s: True, 1)
        h = sim.wait_health(lambda h: h["flags"] & 3 == 3)
        record("T01", "GET_STATUS, no USB -> OK + STATUS + HEALTH", r and r["result"] == "OK" and s["index"] == 0xFFFF
               and h["usb"] == "NO_MEDIA", "resp=%s %.0fms status=%s health=%s" % (r and r["result"], ms or -1, s, h))
        r, _ = sim.command(OP["PLAY"])
        record("T02", "PLAY with empty playlist -> REJECTED", r and r["result"] == "REJECTED", r)
        r, _ = sim.command(OP["PLAY_INDEX"], 0)
        record("T03", "PLAY_INDEX 0 with empty playlist -> REJECTED", r and r["result"] == "REJECTED", r)

        # ---------- USB in ----------
        mm("sim-add", usb)
        h = sim.wait_health(lambda h: h["usb"] == "READY" and h["tracks"] >= 3, 5)
        record("T04", "USB inserted -> HEALTH READY, tracks>=3", h and h["usb"] == "READY" and h["tracks"] >= 3, h)
        ntracks = h["tracks"] if h else 3

        r, ms = sim.command(OP["PLAY"])
        s = sim.wait_status(lambda s: s["state"] == "PLAYING", 3)
        record("T05", "PLAY -> OK, STATUS PLAYING (confirmed)", r and r["result"] == "OK" and s["state"] == "PLAYING",
               "resp=%s %.0fms status=%s" % (r and r["result"], ms or -1, s))
        lat_play = ms
        r, ms = sim.command(OP["PAUSE"])
        s = sim.wait_status(lambda s: s["state"] == "PAUSED", 3)
        record("T06", "PAUSE -> OK, STATUS PAUSED", r and r["result"] == "OK" and s["state"] == "PAUSED", s)
        r, _ = sim.command(OP["PAUSE"])
        record("T07", "PAUSE when already PAUSED -> REJECTED", r and r["result"] == "REJECTED", r)
        r, _ = sim.command(OP["PLAY"])
        s = sim.wait_status(lambda s: s["state"] == "PLAYING", 3)
        record("T08", "PLAY (resume) -> PLAYING", r and r["result"] == "OK" and s["state"] == "PLAYING", s)

        r, _ = sim.command(OP["NEXT"])
        s = sim.wait_status(lambda s: s["index"] == 1, 3)
        record("T09", "NEXT -> index 1", r and r["result"] == "OK" and s["index"] == 1, s)
        r, _ = sim.command(OP["PREVIOUS"])
        s = sim.wait_status(lambda s: s["index"] == 0, 3)
        record("T10", "PREVIOUS -> index 0", r and r["result"] == "OK" and s["index"] == 0, s)
        r, _ = sim.command(OP["PREVIOUS"])
        s = sim.wait_status(lambda s: s["index"] == ntracks - 1, 3)
        record("T11", "PREVIOUS at 0 -> wraps to last", r and r["result"] == "OK" and s["index"] == ntracks - 1, s)

        r, _ = sim.command(OP["PLAY_INDEX"], 2)
        s = sim.wait_status(lambda s: s["index"] == 2 and s["state"] == "PLAYING", 3)
        record("T12", "PLAY_INDEX 2 -> PLAYING index 2", r and r["result"] == "OK" and s["index"] == 2, s)
        r, _ = sim.command(OP["PLAY_INDEX"], ntracks)
        record("T13", "PLAY_INDEX = track count -> OUT_OF_RANGE", r and r["result"] == "OUT_OF_RANGE", r)
        r, _ = sim.command(OP["PLAY_INDEX"], -1)
        record("T14", "PLAY_INDEX -1 -> OUT_OF_RANGE", r and r["result"] == "OUT_OF_RANGE", r)
        r, _ = sim.command(OP["PLAY_INDEX"], 70000)
        record("T15", "PLAY_INDEX 70000 -> OUT_OF_RANGE", r and r["result"] == "OUT_OF_RANGE", r)

        # ---------- seek ----------
        sim.command(OP["PLAY_INDEX"], 0)
        sim.wait_status(lambda s: s["index"] == 0 and s["state"] == "PLAYING" and s["dur"] > 0, 3)
        time.sleep(0.6)
        p0 = sim.wait_status(lambda s: True)["pos"]
        r, _ = sim.command(OP["SEEK_FORWARD"], 8000)
        s = sim.wait_status(lambda s: s["pos"] >= p0 + 6, 3)
        record("T16", "SEEK_FORWARD 8000 ms -> position +~8 s", r and r["result"] == "OK" and s["pos"] >= p0 + 6,
               "before=%ds after=%s" % (p0, s))
        p1 = s["pos"]
        r, _ = sim.command(OP["SEEK_BACKWARD"], 0)          # 0 -> default 10 s
        s = sim.wait_status(lambda s: s["pos"] <= max(0, p1 - 8), 3)
        record("T17", "SEEK_BACKWARD 0 (default 10 s) -> position -~10 s", r and r["result"] == "OK" and s["pos"] <= max(0, p1 - 8),
               "before=%ds after=%s" % (p1, s))
        r, _ = sim.command(OP["SEEK_BACKWARD"], 600000)
        s = sim.wait_status(lambda s: s["pos"] <= 1, 3)
        record("T18", "SEEK_BACKWARD 600000 -> clamped at 0", r and r["result"] == "OK" and s["pos"] <= 1, s)
        r, _ = sim.command(OP["SEEK_FORWARD"], 600001)
        record("T19", "SEEK_FORWARD 600001 -> OUT_OF_RANGE", r and r["result"] == "OUT_OF_RANGE", r)
        r, _ = sim.command(OP["SEEK_FORWARD"], 600000)
        s = sim.wait_status(lambda s: s["index"] == 1, 4)
        record("T20", "SEEK_FORWARD past end -> clamp to end, EOS -> next track",
               r and r["result"] == "OK" and s["index"] == 1, s)

        r, _ = sim.command(OP["STOP"])
        s = sim.wait_status(lambda s: s["state"] == "STOPPED", 3)
        record("T21", "STOP -> STOPPED", r and r["result"] == "OK" and s["state"] == "STOPPED", s)

        # ---------- invalid frames ----------
        h0 = sim.wait_health(lambda h: True)["rejected"]
        sim.drain(); t0 = time.time(); sim.send_raw(cmd_bytes(OP["PLAY"], 50)[:5])
        r, _ = sim.wait_resp(t0)
        record("T22", "DLC 5 -> BAD_DLC (opcode/seq echoed)", r and r["result"] == "BAD_DLC" and r["seq"] == 50, r)
        bad = bytearray(cmd_bytes(OP["PLAY"], 51)); bad[7] ^= 0xFF
        sim.drain(); t0 = time.time(); sim.send_raw(bad)
        r, _ = sim.wait_resp(t0)
        record("T23", "wrong checksum -> BAD_CHECKSUM", r and r["result"] == "BAD_CHECKSUM", r)
        sim.drain(); t0 = time.time(); sim.send_raw(cmd_bytes(0x55, 52))
        r, _ = sim.wait_resp(t0)
        record("T24", "opcode 0x55 -> BAD_OPCODE", r and r["result"] == "BAD_OPCODE", r)
        sim.drain(); t0 = time.time(); sim.send_raw(cmd_bytes(OP["PLAY"], 53), cid=0x200)
        r, _ = sim.wait_resp(t0, 0.5)
        record("T25", "frame with other ID 0x200 -> ignored (no response)", r is None, r)
        h = sim.wait_health(lambda h: h["rejected"] >= h0 + 3, 3)
        record("T26", "HEALTH rejected counter +3, last_error set", h["rejected"] >= h0 + 3 and h["last_error"] != "OK", h)
        s1 = sim.wait_status(lambda s: True)
        record("T27", "invalid frames do not change playback", s1["state"] == "STOPPED", s1)

        # ---------- duplicate / busy ----------
        sim.command(OP["PLAY_INDEX"], 0)
        sim.wait_status(lambda s: s["index"] == 0 and s["state"] == "PLAYING", 3)
        seq = sim.next_seq()
        r1, _ = sim.command(OP["NEXT"], seq=seq)
        r2, _ = sim.command(OP["NEXT"], seq=seq)              # same 8 bytes = node retry
        s = sim.wait_status(lambda s: s["index"] == 1, 2)
        time.sleep(0.8)
        s = sim.wait_status(lambda s: True)
        record("T28", "same CMD twice -> 2nd is DUPLICATE, executed once",
               r1 and r1["result"] == "OK" and r2 and (r2["flags"] & 1) and s["index"] == 1,
               "r1=%s r2=%s index=%s" % (r1 and r1["result"], r2 and (r2["result"], r2["flags"]), s["index"]))
        sim.drain(); t0 = time.time()
        sim.send_raw(cmd_bytes(OP["NEXT"], sim.next_seq())); sim.send_raw(cmd_bytes(OP["PREVIOUS"], sim.next_seq()))
        ra, _ = sim.wait_resp(t0); rb, _ = sim.wait_resp(t0)
        results = sorted([ra and ra["result"], rb and rb["result"]], key=str)
        record("T29", "2 CMDs back-to-back -> one OK, one BUSY", results == ["BUSY", "OK"], results)

        # ---------- USB removed while controlling ----------
        sim.command(OP["PLAY"])
        sim.wait_status(lambda s: s["state"] == "PLAYING", 3)
        mm("sim-remove")
        s = sim.wait_status(lambda s: s["index"] == 0xFFFF and s["state"] == "STOPPED", 3)
        h = sim.wait_health(lambda h: h["usb"] == "NO_MEDIA" and h["tracks"] == 0, 3)
        record("T30", "USB removed while PLAYING -> STATUS STOPPED/none, HEALTH NO_MEDIA",
               s["state"] == "STOPPED" and s["index"] == 0xFFFF and h["usb"] == "NO_MEDIA", "status=%s health=%s" % (s, h))
        r, _ = sim.command(OP["PLAY"])
        record("T31", "PLAY after USB removed -> REJECTED", r and r["result"] == "REJECTED", r)
        mm("sim-add", usb)
        sim.wait_health(lambda h: h["usb"] == "READY", 5)

        # ---------- video-player restart ----------
        procs["vp"].terminate(); procs["vp"].wait(5)
        h = sim.wait_health(lambda h: not (h["flags"] & 1), 3)
        r, _ = sim.command(OP["PLAY"])
        s = sim.wait_status(lambda s: s["state"] == "UNKNOWN", 2)
        record("T32", "video-player stopped -> PLAYER_UNAVAILABLE, STATUS UNKNOWN",
               r and r["result"] == "PLAYER_UNAVAILABLE" and s["state"] == "UNKNOWN" and not (h["flags"] & 1),
               "resp=%s status=%s flags=0x%02X" % (r and r["result"], s["state"], h["flags"]))
        procs["vp"] = start(os.environ["VP_BIN"], "--session", log="/tmp/it_vp2.log")
        h = sim.wait_health(lambda h: h["flags"] & 1, 5)
        time.sleep(1.0)
        r, _ = sim.command(OP["PLAY"])
        s = sim.wait_status(lambda s: s["state"] == "PLAYING", 3)
        record("T33", "video-player restarted -> PLAY OK again", r and r["result"] == "OK" and s["state"] == "PLAYING", s)

        # ---------- CAN link down / recovery ----------
        sim.close_link()
        time.sleep(1.5)
        sim.accept(timeout=5)                                 # can-service reconnects by itself
        r, ms = sim.command(OP["GET_STATUS"])
        record("T34", "CAN link down -> can-service reopens, commands work", r and r["result"] == "OK",
               "resp=%s %.0fms" % (r and r["result"], ms or -1))

        # ---------- status period ----------
        with sim.lock:
            sim.status_times = []
        time.sleep(3.2)
        with sim.lock:
            ts = list(sim.status_times)
        gaps = [b - a for a, b in zip(ts, ts[1:])]
        maxgap = max(gaps) if gaps else 99
        record("T35", "STATUS periodic (max gap <= 0.6 s)", maxgap <= 0.6, "%d frames in 3.2 s, max gap %.3f s" % (len(ts), maxgap))
        record("T36", "RESPONSE latency PLAY < 200 ms", lat_play is not None and lat_play < 200, "%.1f ms" % (lat_play or -1))

        # ---------- clean shutdown ----------
        t0 = time.time()
        procs["can"].terminate()
        rc = procs["can"].wait(5)
        dt = time.time() - t0
        log = open("/tmp/it_can.log").read()
        record("T37", "SIGTERM -> exit 0, 'stopped' logged, < 1 s", rc == 0 and "can-service stopped" in log and dt < 1,
               "rc=%s %.2fs" % (rc, dt))
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()

    fails = [r for r in RESULTS if r[2] != "PASS"]
    print("\nSUMMARY: %d passed, %d failed" % (len(RESULTS) - len(fails), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
