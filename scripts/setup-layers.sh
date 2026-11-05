#!/bin/bash
# setup-layers.sh - get the Yocto layers (branch scarthgap) and prepare the build directory.
#
#   bash scripts/setup-layers.sh            clone/update layers + create rasp/poky/build
#   bash scripts/setup-layers.sh --no-build only clone/update the layers
#
# Result (rasp/poky/ is NOT in git, see .gitignore):
#   rasp/poky/                    poky (bitbake + openembedded-core)
#   rasp/poky/meta-raspberrypi
#   rasp/poky/meta-openembedded   (meta-oe is used)
#   rasp/poky/meta-qt5
#   rasp/poky/build/              conf/bblayers.conf + conf/local.conf ready for bitbake
# Then:  cd rasp/poky && source oe-init-build-env build && bitbake core-image-minimal
set -e

BRANCH=scarthgap
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
POKY="$REPO_DIR/rasp/poky"
META_OJT="$REPO_DIR/rasp/custom-source/meta-ojt"

get() {   # get <url> <dir>
    if [ -d "$2/.git" ]; then
        echo "== update $2"
        git -C "$2" fetch --depth 1 origin "$BRANCH" && git -C "$2" checkout -q FETCH_HEAD
    elif [ -d "$2" ] && [ -n "$(find "$2" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        echo "== skip clone $1 -> $2 (directory already exists)"
    else
        echo "== clone $1 -> $2"
        git clone --depth 1 -b "$BRANCH" "$1" "$2"
    fi
}

get https://git.yoctoproject.org/poky                  "$POKY"
get https://github.com/agherzan/meta-raspberrypi        "$POKY/meta-raspberrypi"
get https://github.com/openembedded/meta-openembedded   "$POKY/meta-openembedded"
get https://github.com/meta-qt5/meta-qt5                "$POKY/meta-qt5"

[ "$1" = "--no-build" ] && exit 0

cd "$POKY"
# oe-init-build-env reads unset variables: do not run it with "set -u"
source ./oe-init-build-env build > /dev/null

for L in "$POKY/meta-raspberrypi" "$POKY/meta-openembedded/meta-oe" "$POKY/meta-qt5" "$META_OJT"; do
    bitbake-layers show-layers | grep -q "$L" || bitbake-layers add-layer "$L"
done

if ! grep -q "OJT settings" conf/local.conf; then
    cat >> conf/local.conf <<'EOF'

# ---- OJT settings (added by scripts/setup-layers.sh) ----
MACHINE = "raspberrypi4-64"
INIT_MANAGER = "systemd"
DISTRO_FEATURES:append = " x11 opengl"
PACKAGECONFIG:append:pn-qtbase = " fontconfig"
# 7" DSI display (800x480). Remove this line for HDMI.
RPI_EXTRA_CONFIG = "dtoverlay=vc4-kms-dsi-7inch"
# Development only: SSH server, root without password
EXTRA_IMAGE_FEATURES += "ssh-server-dropbear debug-tweaks"
# Optional: sample videos in /usr/share/video-player/testdata
#IMAGE_INSTALL:append = " video-player-testdata"
EOF
    echo "== OJT settings appended to $POKY/build/conf/local.conf"
fi

bitbake-layers show-layers
echo
echo "Ready. Build with:"
echo "  cd $POKY && source oe-init-build-env build && bitbake core-image-minimal"
