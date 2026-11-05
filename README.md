# meta-ojt: USB Video Player + CAN control for Raspberry Pi 4 (Yocto Scarthgap)

Study project: plug in a USB stick -> the video list appears on the screen (7" DSI or HDMI) -> play / pause / next / previous.
The player can also be controlled over **CAN** from an **Arduino UNO + MCP2515** test node, and watched/driven from a PC
with **OJT CAN Monitor** (CANoe-like Python app). Four services on the Pi talk to each other over **D-Bus** (system bus).

**Documentation (Vietnamese): [docs/](docs/README.md)** — architecture & API · hardware · build & deploy · test.

```
 USB stick ──udev──► media-manager ──PlaylistChanged──► video-player ──draws video──► ojt-gui (Qt5, X11)
                     (C) mount/scan      GetPlaylist()   (C++, GStreamer)   window id   YouTube-style HMI
                                                              ▲   │ StatusChanged
                                              Play/Next/…     │   ▼
 PC ──USB (COM)──► Arduino UNO + MCP2515 ══CAN 500k══ MCP2515 ── can-service (C, SocketCAN can0)
 (OJT CAN Monitor / PuTTY)  (arduino/ojt-can-node)    (Pi SPI0)
```

| Path | What | Language |
|---|---|---|
| `rasp/custom-source/meta-ojt/recipes-multimedia/media-manager` | USB hotplug, mount, scan, playlist (`org.automotive.MediaManager`) | C |
| `rasp/custom-source/meta-ojt/recipes-multimedia/video-player` | GStreamer playback, commands (`org.automotive.VideoPlayer`) | C++ |
| `rasp/custom-source/meta-ojt/recipes-qt/ojt-gui` | HMI, D-Bus client | C++ / Qt5 |
| `rasp/custom-source/meta-ojt/recipes-connectivity/can-service` | CAN 0x100 commands -> D-Bus, status -> CAN 0x101–0x103 | C |
| `arduino/ojt-can-node` | CAN test node firmware (+ `prebuilt/*.hex`) | C style, Arduino |
| `tools/can-monitor` | PC tool: buttons, trace, statistics, graphics, `.asc` export | Python |
| `scripts/setup-layers.sh` | Clone poky + meta-raspberrypi + meta-openembedded + meta-qt5 (scarthgap) into `rasp/poky` | bash |

## Quick start

```bash
bash scripts/setup-layers.sh                      # layers + rasp/poky/build with OJT local.conf settings
cd rasp/poky && source oe-init-build-env build
bitbake core-image-minimal                     # -> tmp/deploy/images/raspberrypi4-64/*.rootfs.wic
```

Arduino (Windows, no IDE): `arduino-cli compile -b arduino:avr:uno -p COMxx -u arduino/ojt-can-node`
PC tool: `py -m pip install -r tools/can-monitor/requirements.txt` then `py tools/can-monitor/can_monitor.py`.
Details and tests: [docs/](docs/README.md).
