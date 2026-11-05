#!/usr/bin/env python3
"""
OJT CAN Monitor - a small CANoe-like tool for the OJT USB Video Player CAN bus.

    PC (this app) --USB/COM--> Arduino UNO + MCP2515 --CAN 500k--> Raspberry Pi (can-service)

The Arduino firmware (arduino/ojt-can-node) is the gateway: this app sends the
same text commands you type in PuTTY ("play", "next", ...) and turns on
"trace on", so the Arduino prints one machine-readable line per CAN frame:

    #F,<micros>,<R|T>,<id hex>,<dlc>,<data hex>     frame received / sent
    #E,<micros>,<event>,<a>,<b>                     NOACK NOTSENT ARBLOST LOST TIMEOUT OFFLINE

Windows install (once):   py -m pip install pyserial matplotlib
Run:                      py can_monitor.py          (choose COMxx, Connect)
Without hardware:         choose "DEMO" in the port list (simulated Pi)
"""
import collections
import csv
import datetime
import queue
import random
import re
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    import serial.tools.list_ports
except ImportError:                       # app still starts, DEMO works
    serial = None

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg   # noqa: E402
from matplotlib.figure import Figure                              # noqa: E402

APP_TITLE = "OJT CAN Monitor"
BAUD = 115200
DEMO_PORT = "DEMO (simulated Pi)"
MAX_TRACE_ROWS = 3000          # rows kept in the trace table (all frames stay in memory for export)
CHART_WINDOW_S = 60            # charts show the last 60 s
BITRATE = 500000

# ======================================================================
# Protocol v1 - same as ojt_can_protocol.h
# ======================================================================
ID_CMD, ID_STATUS, ID_RESPONSE, ID_HEALTH = 0x100, 0x101, 0x102, 0x103
ID_NAMES = {ID_CMD: "CMD", ID_STATUS: "STATUS", ID_RESPONSE: "RESPONSE", ID_HEALTH: "HEALTH"}
OPCODES = {1: "PLAY", 2: "PAUSE", 3: "NEXT", 4: "PREVIOUS", 5: "SEEK_FWD", 6: "SEEK_BACK",
           7: "STOP", 8: "PLAY_INDEX", 9: "GET_STATUS"}
RESULTS = {0: "OK", 1: "REJECTED", 2: "BAD_DLC", 3: "BAD_OPCODE", 4: "BAD_CHECKSUM",
           5: "OUT_OF_RANGE", 6: "PLAYER_UNAVAILABLE", 7: "DBUS_ERROR", 8: "BUSY"}
STATES = {0: "STOPPED", 1: "PLAYING", 2: "PAUSED", 3: "ERROR", 0xFF: "UNKNOWN"}
USB_STATES = {0: "NO_MEDIA", 1: "SCANNING", 2: "READY", 0xFF: "UNKNOWN"}
EVENT_TEXT = {
    "NOACK": "no ACK: frame sent, nobody acknowledged",
    "NOTSENT": "frame NOT sent: bus never idle (transceiver?)",
    "ARBLOST": "arbitration lost",
    "LOST": "frame lost (SPI write?)",
    "TIMEOUT": "no RESPONSE after retry",
    "OFFLINE": "Pi offline: no STATUS for 1.5 s",
}


def checksum(d):
    x = 0
    for b in d[:7]:
        x ^= b
    return x


def u16(d, i):
    return d[i] | (d[i + 1] << 8)


def i32(d, i):
    v = d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | (d[i + 3] << 24)
    return v - (1 << 32) if v & 0x80000000 else v


def decode(fid, d):
    """Return (text, values dict) for one frame of the OJT protocol."""
    if len(d) != 8:
        return f"DLC {len(d)} (protocol needs 8)", {}
    ck_ok = checksum(d) == d[7]
    if fid == ID_CMD:
        v = {"opcode": d[0], "seq": d[1], "param": i32(d, 2)}
        t = f"{OPCODES.get(d[0], '?%02X' % d[0])} seq={d[1]} param={v['param']}"
        return t + ("" if ck_ok else "  [BAD CHECKSUM]"), v
    if fid == ID_RESPONSE:
        v = {"opcode": d[0], "seq": d[1], "result": d[2], "flags": d[3]}
        t = f"{OPCODES.get(d[0], '?')} seq={d[1]} -> {RESULTS.get(d[2], '?%02X' % d[2])}"
        if d[3] & 1:
            t += " (duplicate)"
        return t + ("" if ck_ok else "  [BAD CHECKSUM]"), v
    if fid == ID_STATUS:
        idx = u16(d, 1)
        v = {"state": d[0], "index": idx, "pos": u16(d, 3), "dur": u16(d, 5), "cnt": d[7]}
        it = "none" if idx == 0xFFFF else str(idx)
        return f"{STATES.get(d[0], '?')} index={it} {v['pos']}/{v['dur']} s cnt={d[7]}", v
    if fid == ID_HEALTH:
        v = {"usb": d[0], "tracks": u16(d, 1), "flags": d[3], "last_err": d[4], "rejected": d[5], "cnt": d[7]}
        fl = d[3]
        t = (f"usb={USB_STATES.get(d[0], '?')} tracks={v['tracks']} player={'up' if fl & 1 else 'DOWN'} "
             f"media={'up' if fl & 2 else 'DOWN'} dbus={'up' if fl & 4 else 'DOWN'} "
             f"last_err={RESULTS.get(d[4], '?')} rejected={d[5]}")
        return t, v
    return "", {}


def frame_bits(dlc):
    """Approximate bits on the wire for a standard frame (with ~10 % bit stuffing)."""
    return int((47 + 8 * dlc) * 1.1)


# ======================================================================
# Links: real serial port or DEMO. Both put (host_time, line) into a queue.
# ======================================================================
class SerialLink:
    def __init__(self, port, rxq):
        self.ser = serial.Serial(port, BAUD, timeout=0.05)
        self.rxq = rxq
        self.alive = True
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b""
        while self.alive:
            try:
                chunk = self.ser.read(1024)
            except Exception as e:            # cable pulled out
                self.rxq.put((time.time(), f"!LINK {e}"))
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.rxq.put((time.time(), line.decode("ascii", "replace").rstrip("\r")))

    def write(self, text):
        self.ser.write((text + "\n").encode("ascii", "replace"))

    def close(self):
        self.alive = False
        try:
            self.ser.close()
        except Exception:
            pass


class DemoLink:
    """Pretends to be Arduino + Pi: answers commands, sends STATUS 500 ms / HEALTH 1 s."""

    def __init__(self, rxq):
        self.rxq = rxq
        self.alive = True
        self.t0 = time.time()
        self.trace = False
        self.seq = 0
        self.state, self.index, self.pos, self.cnt, self.hcnt = 0, 0xFFFF, 0, 0, 0
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()
        self._say("=== OJT CAN test node (DEMO, protocol v1) ===")
        self._say("MCP2515 ready: 500000 bit/s, crystal 8000000 Hz, NORMAL mode")

    def _us(self):
        return int((time.time() - self.t0) * 1e6) & 0xFFFFFFFF

    def _say(self, line):
        self.rxq.put((time.time(), line))

    def _frame(self, d, fid, data):
        if self.trace:
            self._say(f"#F,{self._us()},{d},{fid:03X},{len(data)},{bytes(data).hex().upper()}")

    def _run(self):
        next_st, next_h = time.time() + 0.5, time.time() + 1.0
        while self.alive:
            now = time.time()
            with self.lock:
                if now >= next_st:
                    next_st += 0.5 + random.uniform(-0.004, 0.004)
                    if self.state == 1:
                        self.pos = min(self.pos + 0.5, 212)
                    idx = self.index
                    self._frame("R", ID_STATUS, [self.state, idx & 255, idx >> 8, int(self.pos) & 255,
                                                 int(self.pos) >> 8, 212, 0, self.cnt & 255])
                    self.cnt += 1
                if now >= next_h:
                    next_h += 1.0
                    self._frame("R", ID_HEALTH, [2, 4, 0, 7, 0, 0, 0, self.hcnt & 255])
                    self.hcnt += 1
            time.sleep(0.005)

    def write(self, text):
        self._say("> " + text)
        parts = text.split()
        if not parts:
            return
        cmd, arg = parts[0], (parts[1] if len(parts) > 1 else "")
        ops = {"play": 1, "pause": 2, "next": 3, "prev": 4, "ff": 5, "rew": 6, "stop": 7, "index": 8, "status": 9}
        with self.lock:
            if cmd == "trace":
                self.trace = (arg == "on")
                self._say(f"#TRACE,{1 if self.trace else 0}")
            elif cmd in ops:
                self.seq = (self.seq + 1) & 255
                op = ops[cmd]
                try:
                    p = int(arg, 0) if arg else 0
                except ValueError:
                    p = 0
                d = [op, self.seq] + list((p & 0xFFFFFFFF).to_bytes(4, "little")) + [0, 0]
                d[7] = checksum(d)
                self._say(f"CMD  {OPCODES[op]} seq={self.seq} param={p}")
                self._frame("T", ID_CMD, d)
                res = self._execute(op, p)
                r = [op, self.seq, res, 0, 0, 0, 0, 0]
                r[7] = checksum(r)
                threading.Timer(random.uniform(0.004, 0.015), self._reply, args=(r,)).start()
            elif cmd == "regs":
                self._say("CANSTAT=00 CANCTRL=07 EFLG=00 TEC=0 REC=0 CANINTF=00 TXB0CTRL=00")
            else:
                self._say("(demo) only player commands, trace and regs are simulated")

    def _reply(self, r):
        with self.lock:
            self._frame("R", ID_RESPONSE, r)
            self._say(f"RESP {OPCODES[r[0]]} seq={r[1]} -> {RESULTS[r[2]]}")

    def _execute(self, op, p):
        if op == 1:
            self.state = 1
            self.index = 0 if self.index == 0xFFFF else self.index
        elif op == 2:
            if self.state != 1:
                return 1
            self.state = 2
        elif op in (3, 4):
            if self.index == 0xFFFF:
                return 1
            self.index = (self.index + (1 if op == 3 else 3)) % 4
            self.pos = 0
        elif op == 5:
            self.pos = min(212, self.pos + (p or 10000) / 1000)
        elif op == 6:
            self.pos = max(0, self.pos - (p or 10000) / 1000)
        elif op == 7:
            self.state, self.index, self.pos = 0, 0xFFFF, 0
        elif op == 8:
            if p < 0 or p > 3:
                return 5
            self.state, self.index, self.pos = 1, p, 0
        return 0

    def close(self):
        self.alive = False


# ======================================================================
# Main window
# ======================================================================
Frame = collections.namedtuple("Frame", "t dir id dlc data text vals dt_ms")


class App:
    def __init__(self, root):
        self.root = root
        root.title(APP_TITLE)
        root.geometry("1360x840")
        root.minsize(1100, 700)
        self.rxq = queue.Queue()
        self.link = None

        # data
        self.frames = []                                   # every frame + event (for export)
        self.last_t_by_key = {}                            # (dir,id) -> time, for delta t
        self.stats = {}                                    # (dir,id) -> dict
        self.pending_cmd = {}                              # (opcode,seq) -> tx time
        self.ts_offset, self.last_raw, self.reset_seen = 0, None, False
        self.t_host0 = None
        self.pos_series = collections.deque()              # (t, pos, dur, state)
        self.period_series = {ID_STATUS: collections.deque(), ID_HEALTH: collections.deque()}
        self.lat_series = collections.deque()              # (t, ms, result)
        self.frame_times = collections.deque()             # (t, id, bits) for rate / bus load
        self.events = 0
        self.n_frames = 0
        self.tec = self.rec = None
        self.fixed_items = {}
        self.paused = False
        self.last_status_host = 0

        self._build_style()
        self._build_toolbar()
        body = ttk.Panedwindow(root, orient="horizontal")
        body.pack(fill="both", expand=True, padx=6, pady=(0, 4))
        left = ttk.Frame(body, width=300)
        body.add(left, weight=0)
        right = ttk.Frame(body)
        body.add(right, weight=1)
        self._build_controls(left)
        self._build_tabs(right)
        self._build_statusbar()
        self.refresh_ports()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        root.after(50, self.poll_queue)
        root.after(1000, self.tick_1s)

    # ------------------------------------------------------------------ UI
    def _build_style(self):
        st = ttk.Style()
        try:
            st.theme_use("clam")
        except tk.TclError:
            pass
        st.configure("Treeview", rowheight=20, font=("Consolas", 9))
        st.configure("Treeview.Heading", font=("Segoe UI", 9, "bold"))
        st.configure("Big.TButton", font=("Segoe UI", 10, "bold"), padding=4)
        st.configure("Hdr.TLabel", font=("Segoe UI", 10, "bold"))
        st.configure("Val.TLabel", font=("Consolas", 10))

    def _build_toolbar(self):
        bar = ttk.Frame(self.root, padding=(6, 6, 6, 4))
        bar.pack(fill="x")
        ttk.Label(bar, text="Port").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(bar, textvariable=self.port_var, width=26, state="readonly")
        self.port_box.pack(side="left", padx=4)
        ttk.Button(bar, text="⟳", width=3, command=self.refresh_ports).pack(side="left")
        self.conn_btn = ttk.Button(bar, text="Connect", command=self.toggle_connect, style="Big.TButton")
        self.conn_btn.pack(side="left", padx=6)
        self.led = tk.Canvas(bar, width=16, height=16, highlightthickness=0)
        self.led.pack(side="left")
        self.led_id = self.led.create_oval(2, 2, 14, 14, fill="#999", outline="")
        self.conn_label = ttk.Label(bar, text="not connected")
        self.conn_label.pack(side="left", padx=(4, 16))
        self.pi_led = tk.Canvas(bar, width=16, height=16, highlightthickness=0)
        self.pi_led.pack(side="left")
        self.pi_led_id = self.pi_led.create_oval(2, 2, 14, 14, fill="#999", outline="")
        self.pi_label = ttk.Label(bar, text="Pi: -")
        self.pi_label.pack(side="left", padx=(4, 16))

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=6)
        self.pause_btn = ttk.Button(bar, text="⏸ Pause trace", command=self.toggle_pause)
        self.pause_btn.pack(side="left", padx=2)
        ttk.Button(bar, text="Clear", command=self.clear_all).pack(side="left", padx=2)
        ttk.Button(bar, text="Save .asc", command=lambda: self.export("asc")).pack(side="left", padx=2)
        ttk.Button(bar, text="Save .csv", command=lambda: self.export("csv")).pack(side="left", padx=2)
        self.regs_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(bar, text="Read TEC/REC every 2 s", variable=self.regs_var).pack(side="left", padx=10)

    def _build_controls(self, parent):
        # --- player commands (CANoe "panel") ---
        g = ttk.LabelFrame(parent, text=" Player control (CMD 0x100) ", padding=6)
        g.pack(fill="x", pady=(0, 6))
        row = ttk.Frame(g)
        row.pack(fill="x")
        for i, (txt, cmd) in enumerate([("⏮ Prev", "prev"), ("▶ Play", "play"), ("⏸ Pause", "pause"),
                                        ("⏹ Stop", "stop"), ("⏭ Next", "next")]):
            b = ttk.Button(row, text=txt, command=lambda c=cmd: self.send(c), style="Big.TButton", width=8)
            b.grid(row=i // 3, column=i % 3, padx=2, pady=2, sticky="ew")
        ttk.Button(row, text="Status", command=lambda: self.send("status"), style="Big.TButton",
                   width=8).grid(row=1, column=2, padx=2, pady=2, sticky="ew")
        for c in range(3):
            row.columnconfigure(c, weight=1)

        r2 = ttk.Frame(g)
        r2.pack(fill="x", pady=(6, 0))
        ttk.Label(r2, text="Seek ms").grid(row=0, column=0, sticky="w")
        self.seek_var = tk.StringVar(value="10000")
        ttk.Entry(r2, textvariable=self.seek_var, width=8).grid(row=0, column=1, padx=4)
        ttk.Button(r2, text="⏪ REW", command=lambda: self.send("rew " + self.seek_var.get())).grid(row=0, column=2)
        ttk.Button(r2, text="FF ⏩", command=lambda: self.send("ff " + self.seek_var.get())).grid(row=0, column=3)
        ttk.Label(r2, text="Index").grid(row=1, column=0, sticky="w", pady=(4, 0))
        self.index_var = tk.StringVar(value="0")
        ttk.Spinbox(r2, from_=0, to=999, textvariable=self.index_var, width=6).grid(row=1, column=1, padx=4,
                                                                                   pady=(4, 0))
        ttk.Button(r2, text="Play index", command=lambda: self.send("index " + self.index_var.get())).grid(
            row=1, column=2, columnspan=2, sticky="ew", pady=(4, 0))

        # cyclic send (like a CANoe interactive generator)
        r3 = ttk.Frame(g)
        r3.pack(fill="x", pady=(6, 0))
        self.cyc_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(r3, text="Send GET_STATUS every", variable=self.cyc_var,
                        command=self.cyclic_tick).pack(side="left")
        self.cyc_ms = tk.StringVar(value="2000")
        ttk.Entry(r3, textvariable=self.cyc_ms, width=6).pack(side="left", padx=3)
        ttk.Label(r3, text="ms").pack(side="left")

        # --- raw frame ---
        g2 = ttk.LabelFrame(parent, text=" Raw frame on 0x100 (error tests) ", padding=6)
        g2.pack(fill="x", pady=(0, 6))
        self.raw_var = tk.StringVar(value="01 01 00 00 00 00 00")
        ttk.Entry(g2, textvariable=self.raw_var, width=28, font=("Consolas", 10)).pack(fill="x")
        r4 = ttk.Frame(g2)
        r4.pack(fill="x", pady=(4, 0))
        self.ck_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(r4, text="add XOR checksum (7 bytes -> 8)", variable=self.ck_var).pack(side="left")
        ttk.Button(r4, text="Send raw", command=self.send_raw).pack(side="right")

        # --- node ---
        g3 = ttk.LabelFrame(parent, text=" Arduino node ", padding=6)
        g3.pack(fill="x", pady=(0, 6))
        for i, c in enumerate(["selftest", "spitest", "info", "regs"]):
            ttk.Button(g3, text=c, command=lambda c=c: self.send(c), width=9).grid(row=0, column=i, padx=1)

        # --- signals (decoded values) ---
        g4 = ttk.LabelFrame(parent, text=" Signals ", padding=6)
        g4.pack(fill="both", expand=True)
        self.sig = {}
        rows = [("STATUS 0x101", None), ("state", "state"), ("index", "index"), ("position", "pos"),
                ("HEALTH 0x103", None), ("USB", "usb"), ("tracks", "tracks"), ("player / media / dbus", "flags"),
                ("last error", "last_err"), ("rejected frames", "rejected"),
                ("RESPONSE 0x102", None), ("last result", "result"), ("latency", "latency")]
        for r, (name, key) in enumerate(rows):
            if key is None:
                ttk.Label(g4, text=name, style="Hdr.TLabel").grid(row=r, column=0, columnspan=2, sticky="w",
                                                                   pady=(6 if r else 0, 1))
                continue
            ttk.Label(g4, text=name).grid(row=r, column=0, sticky="w", padx=(8, 6))
            lbl = ttk.Label(g4, text="-", style="Val.TLabel")
            lbl.grid(row=r, column=1, sticky="w")
            self.sig[key] = lbl
        self.pos_bar = ttk.Progressbar(g4, length=180, maximum=100)
        self.pos_bar.grid(row=len(rows), column=0, columnspan=2, sticky="ew", pady=(6, 0))

    def _build_tabs(self, parent):
        self.nb = ttk.Notebook(parent)
        self.nb.pack(fill="both", expand=True)

        # --- Trace ---
        tr = ttk.Frame(self.nb)
        self.nb.add(tr, text="  Trace  ")
        opt = ttk.Frame(tr, padding=(0, 4))
        opt.pack(fill="x")
        self.fixed_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(opt, text="Fixed position (1 row per ID)", variable=self.fixed_var,
                        command=self.rebuild_trace).pack(side="left")
        self.autoscroll = tk.BooleanVar(value=True)
        ttk.Checkbutton(opt, text="Auto scroll", variable=self.autoscroll).pack(side="left", padx=10)
        ttk.Label(opt, text="Filter ID (e.g. 100,102)").pack(side="left", padx=(10, 4))
        self.filter_var = tk.StringVar()
        e = ttk.Entry(opt, textvariable=self.filter_var, width=14)
        e.pack(side="left")
        e.bind("<Return>", lambda _e: self.rebuild_trace())
        ttk.Button(opt, text="Apply", command=self.rebuild_trace).pack(side="left", padx=4)

        cols = ("time", "dt", "dir", "id", "name", "dlc", "data", "decoded")
        heads = ("Time (s)", "Δt (ms)", "Dir", "ID", "Name", "DLC", "Data", "Decoded")
        widths = (90, 70, 40, 50, 80, 40, 190, 520)
        self.trace = self._tree(tr, cols, heads, widths)
        self.trace.tag_configure("Tx", foreground="#1d4ed8")
        self.trace.tag_configure("Rx", foreground="#111")
        self.trace.tag_configure("err", foreground="#b91c1c", background="#fde8e8")
        self.trace.tag_configure("bad", foreground="#b45309")

        # --- Statistics ---
        stt = ttk.Frame(self.nb)
        self.nb.add(stt, text="  Statistics  ")
        cols = ("id", "name", "dir", "count", "rate", "avg", "min", "max", "jitter", "last")
        heads = ("ID", "Name", "Dir", "Count", "Rate (fr/s)", "Period avg (ms)", "min", "max", "Jitter (ms)",
                 "Last data")
        widths = (60, 90, 40, 70, 90, 110, 70, 70, 90, 300)
        self.stat_tree = self._tree(stt, cols, heads, widths)
        self.bus_label = ttk.Label(stt, text="", style="Val.TLabel", padding=6)
        self.bus_label.pack(fill="x")

        # --- Graphics ---
        gr = ttk.Frame(self.nb)
        self.nb.add(gr, text="  Graphics  ")
        self.fig = Figure(figsize=(9, 6), dpi=90, tight_layout=True)
        self.ax_pos = self.fig.add_subplot(221)
        self.ax_per = self.fig.add_subplot(222)
        self.ax_rate = self.fig.add_subplot(223)
        self.ax_lat = self.fig.add_subplot(224)
        self.canvas = FigureCanvasTkAgg(self.fig, master=gr)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)

        # --- Console ---
        co = ttk.Frame(self.nb)
        self.nb.add(co, text="  Console  ")
        self.console = tk.Text(co, height=10, font=("Consolas", 9), wrap="none", bg="#0f172a", fg="#e2e8f0",
                               insertbackground="#e2e8f0")
        sb = ttk.Scrollbar(co, command=self.console.yview)
        self.console.configure(yscrollcommand=sb.set)
        ent = ttk.Frame(co)
        ent.pack(side="bottom", fill="x", pady=4)
        ttk.Label(ent, text="Command >").pack(side="left")
        self.cmd_var = tk.StringVar()
        ce = ttk.Entry(ent, textvariable=self.cmd_var, font=("Consolas", 10))
        ce.pack(side="left", fill="x", expand=True, padx=4)
        ce.bind("<Return>", lambda _e: self.send_console())
        ttk.Button(ent, text="Send", command=self.send_console).pack(side="left")
        sb.pack(side="right", fill="y")
        self.console.pack(fill="both", expand=True)
        self.console.tag_configure("err", foreground="#fca5a5")
        self.console.tag_configure("me", foreground="#93c5fd")

    def _tree(self, parent, cols, heads, widths):
        fr = ttk.Frame(parent)
        fr.pack(fill="both", expand=True)
        tv = ttk.Treeview(fr, columns=cols, show="headings")
        for c, h, w in zip(cols, heads, widths):
            tv.heading(c, text=h)
            tv.column(c, width=w, anchor="w", stretch=(c in ("decoded", "last")))
        ys = ttk.Scrollbar(fr, orient="vertical", command=tv.yview)
        tv.configure(yscrollcommand=ys.set)
        ys.pack(side="right", fill="y")
        tv.pack(fill="both", expand=True)
        return tv

    def _build_statusbar(self):
        self.status = ttk.Label(self.root, text="", relief="sunken", anchor="w", padding=(6, 2))
        self.status.pack(fill="x", side="bottom")

    # ------------------------------------------------------------ connect
    def refresh_ports(self):
        ports = []
        if serial is not None:
            ports = [p.device for p in serial.tools.list_ports.comports()]
        ports.append(DEMO_PORT)
        self.port_box["values"] = ports
        if self.port_var.get() not in ports:
            self.port_var.set(ports[0])

    def toggle_connect(self):
        if self.link:
            self.link.close()
            self.link = None
            self.conn_btn.configure(text="Connect")
            self.set_led(self.led, self.led_id, "#999")
            self.conn_label.configure(text="not connected")
            return
        port = self.port_var.get()
        try:
            if port == DEMO_PORT:
                self.link = DemoLink(self.rxq)
                self.root.after(300, lambda: self.send("trace on", echo=False))
            else:
                if serial is None:
                    messagebox.showerror(APP_TITLE, "pyserial is missing:\n  py -m pip install pyserial")
                    return
                self.link = SerialLink(port, self.rxq)
                # the UNO resets when the port opens: "trace on" is sent after its banner,
                # and once more here in case the banner was missed
                self.root.after(2500, lambda: self.send("trace on", echo=False))
        except Exception as e:
            messagebox.showerror(APP_TITLE, f"Cannot open {port}:\n{e}\n\nClose PuTTY / arduino-cli monitor first.")
            self.link = None
            return
        self.conn_btn.configure(text="Disconnect")
        self.set_led(self.led, self.led_id, "#f59e0b")
        self.conn_label.configure(text=f"{port}: waiting for trace...")

    def set_led(self, cv, item, color):
        cv.itemconfigure(item, fill=color)

    def send(self, text, echo=True):
        if not self.link:
            self.log("not connected", "err")
            return
        try:
            self.link.write(text)
        except Exception as e:
            self.log(f"write failed: {e}", "err")
            return
        if echo:
            self.log(f">>> {text}", "me")
            if text == "regs":
                self.console_wanted = True           # show the next regs answer in the console

    def send_console(self):
        t = self.cmd_var.get().strip()
        if t:
            self.send(t)
            self.cmd_var.set("")

    def send_raw(self):
        txt = self.raw_var.get().replace(" ", "").replace(",", "")
        try:
            data = bytes.fromhex(txt)
        except ValueError:
            messagebox.showerror(APP_TITLE, "Raw data must be hex bytes, e.g. 01 01 00 00 00 00 00")
            return
        if self.ck_var.get() and len(data) == 7:
            data = data + bytes([checksum(data + b"\0")])
        if len(data) > 8:
            messagebox.showerror(APP_TITLE, "Max 8 bytes")
            return
        self.send("raw " + data.hex().upper())

    def cyclic_tick(self):
        if not self.cyc_var.get():
            return
        if self.link:
            self.send("status", echo=False)
        try:
            ms = max(200, int(self.cyc_ms.get()))
        except ValueError:
            ms = 2000
        self.root.after(ms, self.cyclic_tick)

    # ------------------------------------------------------------ incoming
    def poll_queue(self):
        new_rows = []
        n = 0
        while n < 400:
            try:
                host_t, line = self.rxq.get_nowait()
            except queue.Empty:
                break
            n += 1
            row = self.handle_line(host_t, line)
            if row is not None:
                new_rows.append(row)
        if new_rows and not self.paused:
            self.add_trace_rows(new_rows)
        self.root.after(50, self.poll_queue)

    def unwrap(self, raw):
        """Arduino micros() -> seconds since the first frame (handles 32-bit wrap and board reset)."""
        if self.last_raw is not None and raw < self.last_raw:
            if self.reset_seen:                      # board restarted: continue the time line
                self.ts_offset += self.last_raw - raw + 1000
            else:
                self.ts_offset += 1 << 32
        self.reset_seen = False
        self.last_raw = raw
        us = raw + self.ts_offset
        if self.t_host0 is None:
            self.t_host0 = us
        return (us - self.t_host0) / 1e6

    def handle_line(self, host_t, line):
        if line.startswith("#F,"):
            try:
                _, us, d, fid, dlc, hexdata = line.split(",", 5)
                data = bytes.fromhex(hexdata) if hexdata else b""
                return self.on_frame(self.unwrap(int(us)), "Rx" if d == "R" else "Tx", int(fid, 16), data)
            except ValueError:
                self.log(line, "err")
                return None
        if line.startswith("#E,"):
            parts = line.split(",")
            try:
                t = self.unwrap(int(parts[1]))
            except (ValueError, IndexError):
                return None
            return self.on_event(t, parts[2] if len(parts) > 2 else "?", parts[3:])
        if line.startswith("#TRACE,1"):
            self.set_led(self.led, self.led_id, "#16a34a")
            self.conn_label.configure(text=f"{self.port_var.get()}: trace on")
            return None
        if line.startswith("!LINK"):
            self.log("serial link lost: " + line[6:], "err")
            self.toggle_connect()
            return None
        if line in ("> regs", "> trace on") and not self.console_wanted:
            return None                              # echo of the app's own background commands
        if "=== OJT CAN test node" in line:          # board (re)started -> trace is off again
            self.reset_seen = True
            self.root.after(300, lambda: self.send("trace on", echo=False))
        m = re.search(r"TEC=(\d+) REC=(\d+)", line)
        if m:
            self.tec, self.rec = int(m.group(1)), int(m.group(2))
            if line.startswith("CANSTAT"):
                if not self.console_wanted:
                    return None                      # periodic regs: keep the console clean
                self.console_wanted = False
        self.log(line, "err" if line.startswith("ERR") or "FAIL" in line else None)
        return None

    console_wanted = False

    def on_frame(self, t, d, fid, data):
        key = (d, fid)
        prev = self.last_t_by_key.get(key)
        dt_ms = (t - prev) * 1000 if prev is not None else None
        self.last_t_by_key[key] = t
        text, vals = decode(fid, data)
        f = Frame(t, d, fid, len(data), data, text, vals, dt_ms)
        self.frames.append(f)
        self.n_frames += 1
        self.frame_times.append((t, fid, frame_bits(len(data))))

        st = self.stats.setdefault(key, {"count": 0, "periods": collections.deque(maxlen=200), "last": b""})
        st["count"] += 1
        st["last"] = data
        st["t_last"] = t
        if dt_ms is not None:
            st["periods"].append(dt_ms)

        if fid == ID_STATUS and vals:
            self.pos_series.append((t, vals["pos"], vals["dur"], vals["state"]))
            if dt_ms is not None:
                self.period_series[ID_STATUS].append((t, dt_ms))
            self.update_status_signals(vals)
        elif fid == ID_HEALTH and vals:
            if dt_ms is not None:
                self.period_series[ID_HEALTH].append((t, dt_ms))
            self.update_health_signals(vals)
        elif fid == ID_CMD and vals:
            self.pending_cmd[(vals["opcode"], vals["seq"])] = t
        elif fid == ID_RESPONSE and vals:
            t0 = self.pending_cmd.pop((vals["opcode"], vals["seq"]), None)
            res = RESULTS.get(vals["result"], "?")
            self.sig["result"].configure(text=f"{OPCODES.get(vals['opcode'], '?')} -> {res}",
                                         foreground="#15803d" if vals["result"] == 0 else "#b91c1c")
            if t0 is not None:
                ms = (t - t0) * 1000
                self.lat_series.append((t, ms, vals["result"]))
                self.sig["latency"].configure(text=f"{ms:.1f} ms")
        return f

    def on_event(self, t, ev, extra):
        self.events += 1
        text = EVENT_TEXT.get(ev, ev)
        if ev == "TIMEOUT" and len(extra) >= 2:
            text += f"  ({OPCODES.get(int(extra[0]), '?')} seq={extra[1]})"
        if ev in ("NOACK", "NOTSENT") and extra:
            text += f"  (TEC={extra[0]})"
            self.tec = int(extra[0])
        if ev == "OFFLINE":
            self.set_led(self.pi_led, self.pi_led_id, "#dc2626")
            self.pi_label.configure(text="Pi: OFFLINE")
        f = Frame(t, "Err", None, 0, b"", text, {"event": ev}, None)
        self.frames.append(f)
        self.log(f"[{t:10.3f}] {ev}: {text}", "err")
        return f

    # ------------------------------------------------------------ views
    def _filter_ids(self):
        txt = self.filter_var.get().strip()
        if not txt:
            return None
        ids = set()
        for p in txt.replace(" ", "").split(","):
            try:
                ids.add(int(p, 16))
            except ValueError:
                pass
        return ids or None

    def _row_values(self, f):
        if f.id is None:
            return (f"{f.t:.6f}", "", "Err", "", "ERROR", "", "", f.text), ("err",)
        dt = f"{f.dt_ms:.1f}" if f.dt_ms is not None else ""
        data = " ".join(f"{b:02X}" for b in f.data)
        tag = f.dir if f.dlc == 8 else "bad"
        return (f"{f.t:.6f}", dt, f.dir, f"{f.id:03X}", ID_NAMES.get(f.id, ""), f.dlc, data, f.text), (tag,)

    def add_trace_rows(self, rows):
        ids = self._filter_ids()
        for f in rows:
            if ids is not None and f.id is not None and f.id not in ids:
                continue
            vals, tags = self._row_values(f)
            if self.fixed_var.get():
                key = (f.dir, f.id)
                item = self.fixed_items.get(key)
                if item and self.trace.exists(item):
                    self.trace.item(item, values=vals, tags=tags)
                else:
                    self.fixed_items[key] = self.trace.insert("", "end", values=vals, tags=tags)
            else:
                self.trace.insert("", "end", values=vals, tags=tags)
        if not self.fixed_var.get():
            kids = self.trace.get_children()
            if len(kids) > MAX_TRACE_ROWS:
                self.trace.delete(*kids[:len(kids) - MAX_TRACE_ROWS])
            if self.autoscroll.get() and kids:
                self.trace.yview_moveto(1.0)

    def rebuild_trace(self):
        self.trace.delete(*self.trace.get_children())
        self.fixed_items = {}
        self.add_trace_rows(self.frames[-MAX_TRACE_ROWS:])

    def toggle_pause(self):
        self.paused = not self.paused
        self.pause_btn.configure(text="▶ Resume trace" if self.paused else "⏸ Pause trace")
        if not self.paused:
            self.rebuild_trace()

    def clear_all(self):
        self.frames.clear()
        self.stats.clear()
        self.last_t_by_key.clear()
        self.pending_cmd.clear()
        self.pos_series.clear()
        self.lat_series.clear()
        self.frame_times.clear()
        for d in self.period_series.values():
            d.clear()
        self.events = 0
        self.n_frames = 0
        self.t_host0 = None
        self.rebuild_trace()
        self.stat_tree.delete(*self.stat_tree.get_children())
        self.console.delete("1.0", "end")

    def update_status_signals(self, v):
        state = STATES.get(v["state"], "?")
        color = {"PLAYING": "#15803d", "PAUSED": "#b45309", "ERROR": "#b91c1c"}.get(state, "#111")
        self.sig["state"].configure(text=state, foreground=color)
        self.sig["index"].configure(text="none" if v["index"] == 0xFFFF else str(v["index"]))
        self.sig["pos"].configure(text=f"{v['pos'] // 60}:{v['pos'] % 60:02d} / {v['dur'] // 60}:{v['dur'] % 60:02d}")
        self.pos_bar["value"] = (100 * v["pos"] / v["dur"]) if v["dur"] else 0
        self.last_status_host = time.time()
        self.set_led(self.pi_led, self.pi_led_id, "#16a34a")
        self.pi_label.configure(text="Pi: online")

    def update_health_signals(self, v):
        fl = v["flags"]
        self.sig["usb"].configure(text=USB_STATES.get(v["usb"], "?"))
        self.sig["tracks"].configure(text=str(v["tracks"]))
        self.sig["flags"].configure(text=" / ".join("up" if fl & b else "DOWN" for b in (1, 2, 4)))
        self.sig["last_err"].configure(text=RESULTS.get(v["last_err"], "?"))
        self.sig["rejected"].configure(text=str(v["rejected"]))

    def log(self, text, tag=None):
        self.console.insert("end", text + "\n", tag)
        if int(self.console.index("end-1c").split(".")[0]) > 5000:
            self.console.delete("1.0", "1000.0")
        self.console.see("end")

    # ------------------------------------------------------------ 1 s timer
    def tick_1s(self):
        now_t = self.frames[-1].t if self.frames else 0
        while self.frame_times and self.frame_times[0][0] < now_t - CHART_WINDOW_S:
            self.frame_times.popleft()
        last1 = [x for x in self.frame_times if x[0] >= now_t - 1.0]
        load = 100.0 * sum(b for _, _, b in last1) / BITRATE
        if self.link and time.time() - self.last_status_host > 1.6 and self.last_status_host:
            self.set_led(self.pi_led, self.pi_led_id, "#dc2626")
            self.pi_label.configure(text="Pi: no STATUS")
        tec = "-" if self.tec is None else self.tec
        rec = "-" if self.rec is None else self.rec
        self.status.configure(text=f"  Frames: {self.n_frames}    Rate: {len(last1)} fr/s    Bus load ≈ {load:.2f} %    "
                                   f"Error events: {self.events}    TEC={tec}  REC={rec}    "
                                   f"Time: {now_t:.1f} s")
        self.update_stats(now_t, load)
        if self.nb.index(self.nb.select()) == 2:      # Graphics tab visible
            self.draw_charts(now_t)
        if self.link and self.regs_var.get():
            self._regs_counter = getattr(self, "_regs_counter", 0) + 1
            if self._regs_counter % 2 == 0:
                self.send("regs", echo=False)
        self.root.after(1000, self.tick_1s)

    def update_stats(self, now_t, load):
        self.stat_tree.delete(*self.stat_tree.get_children())
        for (d, fid), st in sorted(self.stats.items(), key=lambda kv: kv[0][1]):
            p = list(st["periods"])
            rate = sum(1 for x in self.frame_times if x[1] == fid and x[0] >= now_t - 10) / 10.0
            if p:
                avg, mn, mx = sum(p) / len(p), min(p), max(p)
                jit = (sum((x - avg) ** 2 for x in p) / len(p)) ** 0.5
                ps = (f"{avg:.1f}", f"{mn:.1f}", f"{mx:.1f}", f"{jit:.2f}")
            else:
                ps = ("", "", "", "")
            self.stat_tree.insert("", "end", values=(f"{fid:03X}", ID_NAMES.get(fid, ""), d, st["count"],
                                                     f"{rate:.1f}", *ps,
                                                     " ".join(f"{b:02X}" for b in st["last"])))
        lat = [m for _, m, _ in self.lat_series]
        lat_txt = (f"CMD->RESPONSE latency: avg {sum(lat) / len(lat):.1f} ms, min {min(lat):.1f}, max {max(lat):.1f} "
                   f"({len(lat)} cmds)") if lat else "CMD->RESPONSE latency: -"
        self.bus_label.configure(text=f"Bus load ≈ {load:.2f} % of {BITRATE // 1000} kbit/s    |    {lat_txt}    |    "
                                      f"Expected: STATUS 500 ms, HEALTH 1000 ms")

    def draw_charts(self, now_t):
        t_min = now_t - CHART_WINDOW_S
        a = self.ax_pos
        a.clear()
        pts = [p for p in self.pos_series if p[0] >= t_min]
        if pts:
            a.plot([p[0] for p in pts], [p[1] for p in pts], color="#2563eb", lw=1.5, label="position")
            a.plot([p[0] for p in pts], [p[2] for p in pts], color="#94a3b8", lw=1, ls="--", label="duration")
            for p in pts:
                if p[3] == 2:
                    a.axvspan(p[0] - 0.25, p[0] + 0.25, color="#fde68a", lw=0)
            a.legend(loc="upper left", fontsize=7)
        a.set_title("STATUS 0x101: playback position (s)", fontsize=9)
        a.set_xlim(max(0, t_min), max(now_t, 1))
        a.grid(alpha=0.3)

        a = self.ax_per
        a.clear()
        for fid, col in ((ID_STATUS, "#2563eb"), (ID_HEALTH, "#16a34a")):
            pts = [p for p in self.period_series[fid] if p[0] >= t_min]
            if pts:
                a.plot([p[0] for p in pts], [p[1] for p in pts], ".-", ms=3, lw=0.8, color=col,
                       label=f"{ID_NAMES[fid]} 0x{fid:03X}")
        a.axhline(500, color="#2563eb", lw=0.6, ls=":")
        a.axhline(1000, color="#16a34a", lw=0.6, ls=":")
        a.set_title("Cycle time Δt (ms)  - dotted = expected", fontsize=9)
        a.set_xlim(max(0, t_min), max(now_t, 1))
        a.grid(alpha=0.3)
        a.legend(loc="upper left", fontsize=7)

        a = self.ax_rate
        a.clear()
        bins = int(CHART_WINDOW_S)
        start = int(max(0, t_min))
        for fid, col in ((ID_CMD, "#1d4ed8"), (ID_STATUS, "#60a5fa"), (ID_RESPONSE, "#f59e0b"), (ID_HEALTH, "#16a34a")):
            counts = [0] * (bins + 1)
            for t, i, _ in self.frame_times:
                if i == fid and t >= start:
                    k = int(t) - start
                    if 0 <= k <= bins:
                        counts[k] += 1
            if any(counts):
                a.step(range(start, start + bins + 1), counts, where="post", color=col, lw=1,
                       label=f"0x{fid:03X} {ID_NAMES[fid]}")
        a.set_title("Frames per second by ID", fontsize=9)
        a.set_xlim(max(0, t_min), max(now_t, 1))
        a.grid(alpha=0.3)
        a.legend(loc="upper left", fontsize=7)

        a = self.ax_lat
        a.clear()
        pts = [p for p in self.lat_series if p[0] >= t_min]
        if pts:
            ok = [p for p in pts if p[2] == 0]
            bad = [p for p in pts if p[2] != 0]
            if ok:
                a.scatter([p[0] for p in ok], [p[1] for p in ok], s=18, color="#16a34a", label="OK")
            if bad:
                a.scatter([p[0] for p in bad], [p[1] for p in bad], s=18, color="#dc2626", marker="x",
                          label="error result")
            a.legend(loc="upper left", fontsize=7)
        a.set_title("CMD -> RESPONSE latency (ms)", fontsize=9)
        a.set_xlim(max(0, t_min), max(now_t, 1))
        a.set_ylim(bottom=0)
        a.grid(alpha=0.3)
        for ax in (self.ax_pos, self.ax_per, self.ax_rate, self.ax_lat):
            ax.tick_params(labelsize=7)
            ax.set_xlabel("time (s)", fontsize=7)
        self.canvas.draw_idle()

    # ------------------------------------------------------------ export
    def export(self, kind):
        if not self.frames:
            messagebox.showinfo(APP_TITLE, "Nothing to save yet.")
            return
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        path = filedialog.asksaveasfilename(defaultextension="." + kind, initialfile=f"ojt_can_{stamp}.{kind}",
                                            filetypes=[(kind.upper(), "*." + kind)])
        if not path:
            return
        if kind == "csv":
            with open(path, "w", newline="") as fh:
                w = csv.writer(fh)
                w.writerow(["time_s", "dt_ms", "dir", "id", "name", "dlc", "data", "decoded"])
                for f in self.frames:
                    w.writerow([f"{f.t:.6f}", "" if f.dt_ms is None else f"{f.dt_ms:.3f}", f.dir,
                                "" if f.id is None else f"{f.id:03X}", ID_NAMES.get(f.id, "ERROR"), f.dlc,
                                " ".join(f"{b:02X}" for b in f.data), f.text])
        else:   # Vector ASCII log: opens in CANoe / CANalyzer / many free viewers
            now = datetime.datetime.now().strftime("%a %b %d %I:%M:%S.000 %p %Y").lower().capitalize()
            with open(path, "w") as fh:
                fh.write(f"date {now}\nbase hex  timestamps absolute\ninternal events logged\n// version 9.0.0\n")
                fh.write(f"Begin Triggerblock {now}\n   0.000000 Start of measurement\n")
                for f in self.frames:
                    if f.id is None:
                        fh.write(f"{f.t:11.6f} 1  ErrorFrame\n")
                    else:
                        data = " ".join(f"{b:02X}" for b in f.data)
                        fh.write(f"{f.t:11.6f} 1  {f.id:<15X} {f.dir:<4} d {f.dlc} {data}\n")
                fh.write("End TriggerBlock\n")
        self.log(f"saved {len(self.frames)} entries to {path}")

    def on_close(self):
        if self.link:
            try:
                self.link.write("trace off")
            except Exception:
                pass
            self.link.close()
        self.root.destroy()


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
