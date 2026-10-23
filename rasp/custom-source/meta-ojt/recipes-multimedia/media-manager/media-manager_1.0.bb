SUMMARY = "OJT Media Manager - USB detect, mount, scan video files, publish playlist on D-Bus"
LICENSE = "CLOSED"

# files/src/  -> source code built by CMake
# files/*     -> files installed by this recipe (script, service, D-Bus policy, test)
SRC_URI = " \
    file://src/ \
    file://media-manager.sh \
    file://media-manager.service \
    file://org.automotive.MediaManager.conf \
    file://media-manager-test.sh \
"
S = "${WORKDIR}/src"

inherit cmake pkgconfig systemd

# glib-2.0: GLib/GIO (main loop + D-Bus), udev: libudev (provided by systemd)
DEPENDS = "glib-2.0 udev"

# gdbus command is needed by media-manager-test.sh
RDEPENDS:${PN} += "dbus glib-2.0-utils"

SYSTEMD_SERVICE:${PN} = "media-manager.service"
SYSTEMD_AUTO_ENABLE = "enable"

# cmake installs /usr/bin/ojt-media-manager, we add the other files here
do_install:append() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/media-manager.sh      ${D}${bindir}/
    install -m 0755 ${WORKDIR}/media-manager-test.sh ${D}${bindir}/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/media-manager.service ${D}${systemd_system_unitdir}/

    install -d ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${WORKDIR}/org.automotive.MediaManager.conf ${D}${datadir}/dbus-1/system.d/
}

FILES:${PN} += "${datadir}/dbus-1/system.d"
