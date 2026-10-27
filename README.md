# meta-ojt: USB Video Player for Raspberry Pi 4 (Yocto Scarthgap)

Study project: plug in a USB stick -> the video list appears on the screen (HDMI or 7" DSI) -> play / pause / next / previous.
Three processes talk to each other over **D-Bus** (system bus).

Full documentation (architecture, every module, test guide): **[docs/ojt-usb-video-player.html](docs/ojt-usb-video-player.html)**

```
 USB stick ──udev──► media-manager ──PlaylistChanged──► video-player ──draws video──┐
 (C, GLib)           detect, mount,       GetPlaylist()  (C++, GStreamer)            │ X11 window id
                     scan .mp4/.mkv  ◄────────────────                               ▼
                           ▲  StateChanged/PlaylistChanged      Play/Pause/...   ojt-gui (Qt5)
                           └──────────────────────────────────── StatusChanged ─ playlist, buttons
```

| Recipe | Process | Language | D-Bus name | Job |
|---|---|---|---|---|
| `recipes-multimedia/media-manager` | `ojt-media-manager` | C | `org.automotive.MediaManager` | USB hotplug, mount, scan, playlist |
| `recipes-multimedia/video-player` | `video-player` | C++ | `org.automotive.VideoPlayer` | GStreamer playback, commands |
| `recipes-qt/ojt-gui` | `ojt-gui` (+ X server via `xinit`) | C++ / Qt5 | client only | HMI on HDMI |
| `recipes-core/packagegroups` | – | – | – | installs all three |

## D-Bus API (short)

**org.automotive.MediaManager1** – methods `GetState() → s`, `GetPlaylist() → a(iss)`, `GetTrack(i) → (bss)`,
`Rescan() → b`, `SimulateUsb(s action, s path) → b`; signals `StateChanged(s)`, `PlaylistChanged(i)`.

**org.automotive.VideoPlayer1** – methods `Play`, `Pause`, `Stop`, `Next`, `Previous` `→ b`, `PlayIndex(i) → b`,
`Seek(x offset_ms) → b`, `SetVideoWindow(t xid) → b`, `GetStatus() → (s state, i index, s title, x pos_ms, x dur_ms)`;
signal `StatusChanged(s state, i index, s title)`.

## Build and run on Raspberry Pi 4

Step-by-step guide with explanations: section 10 of [docs/ojt-usb-video-player.html](docs/ojt-usb-video-player.html).

**1. Layers** (all on branch `scarthgap`)

```bash
mkdir ~/yocto && cd ~/yocto
git clone -b scarthgap git://git.yoctoproject.org/poky
git clone -b scarthgap https://github.com/agherzan/meta-raspberrypi
git clone -b scarthgap https://github.com/openembedded/meta-openembedded
git clone -b scarthgap https://github.com/meta-qt5/meta-qt5
# copy OJT-project-main (meta-ojt) into ~/yocto  (keep LF line endings in .sh files)

source poky/oe-init-build-env build
bitbake-layers add-layer ../meta-raspberrypi ../meta-openembedded/meta-oe ../meta-qt5 ../OJT-project-main
```

**2. `conf/local.conf`**

```bitbake
MACHINE = "raspberrypi4-64"
INIT_MANAGER = "systemd"
DISTRO_FEATURES:append = " x11 opengl"
PACKAGECONFIG:append:pn-qtbase = " fontconfig"      # Qt finds the DejaVu font (Vietnamese text)

# 3 services + kernel modules (DSI panel, touch, H.264 HW decoder, exFAT/NTFS are modules)
IMAGE_INSTALL:append = " packagegroup-ojt-multimedia kernel-modules"

# Raspberry Pi 7" Touch Display (v1.1, 800x480, DSI). Remove when using HDMI.
RPI_EXTRA_CONFIG = "dtoverlay=vc4-kms-dsi-7inch"

# development: SSH + root without password
EXTRA_IMAGE_FEATURES += "ssh-server-dropbear debug-tweaks"
IMAGE_FSTYPES = "wic.bz2 wic.bmap"

# optional: sample videos, software decoders (AAC audio, H.265...)
# IMAGE_INSTALL:append = " video-player-testdata"
# LICENSE_FLAGS_ACCEPTED += "commercial"   # + uncomment gstreamer1.0-libav in video-player_1.0.bb
```

**3. Build**

```bash
bitbake core-image-minimal      # or core-image-base
```

| Image | OK? | Note |
|---|---|---|
| `core-image-minimal` | yes | smallest; `kernel-modules` is **required** (minimal installs no modules) |
| `core-image-base` | yes | adds `packagegroup-base` (WiFi, BT, ALSA... per MACHINE_FEATURES) |
| `core-image-x11`, `core-image-sato` | **no** | `xserver-nodm-init` starts another X server on `:0` |

**4. Flash and boot**

```bash
cd tmp/deploy/images/raspberrypi4-64/
sudo bmaptool copy core-image-minimal-raspberrypi4-64.rootfs.wic.bz2 /dev/sdX
```

DSI display: ribbon cable to the **DISPLAY** port, power the display from GPIO 5V/GND (pins 2 and 6), no HDMI at the same time.
After boot the screen shows "Không có playlist".

## Videos

`.mp4` / `.mkv`, **H.264** recommended (Pi 4 hardware decoder, up to 1080p). H.265/VP9/AV1 and AAC audio need `gstreamer1.0-libav`.

```bash
ffmpeg -i input.mov -c:v libx264 -profile:v high -level 4.1 -pix_fmt yuv420p \
       -vf "scale=800:-2" -c:a aac -b:a 128k -movflags +faststart output.mp4
```

## Test on the Pi

```bash
systemctl status media-manager video-player ojt-gui
journalctl -u video-player -f
dmesg | grep -iE "dsi|tc358762|ft5|panel"                       # DSI display detected?
media-manager-test.sh sim-add /usr/share/video-player/testdata   # USB without a USB stick
video-player-test.sh play | pause | next | prev | index 2 | seek 10000 | status
```
