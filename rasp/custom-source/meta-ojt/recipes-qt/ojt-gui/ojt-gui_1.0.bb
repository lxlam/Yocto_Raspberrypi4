SUMMARY = "OJT GUI - Qt5 HMI: playlist, video area, playback buttons (D-Bus client)"
LICENSE = "CLOSED"

# files/src/  -> source code built by CMake
# files/*     -> files installed by this recipe
SRC_URI = " \
    file://src/ \
    file://ojt-gui.sh \
    file://ojt-gui.service \
"
S = "${WORKDIR}/src"

# cmake_qt5 comes from meta-qt5
inherit cmake_qt5 systemd

DEPENDS = "qtbase"

# qtbase-plugins          : Qt "xcb" platform plugin (draw on X11)
# packagegroup-core-x11-xserver : X server + video driver for this MACHINE
# xinit                   : starts X server + our GUI (see ojt-gui.sh)
# ttf-dejavu-sans         : font with Vietnamese characters
RDEPENDS:${PN} += " \
    qtbase-plugins \
    packagegroup-core-x11-xserver \
    xinit \
    ttf-dejavu-sans \
"

SYSTEMD_SERVICE:${PN} = "ojt-gui.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/ojt-gui.sh ${D}${bindir}/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/ojt-gui.service ${D}${systemd_system_unitdir}/
}
