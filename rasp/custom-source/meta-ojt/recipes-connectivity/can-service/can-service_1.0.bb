SUMMARY = "OJT CAN service: CAN 0x100 commands (SocketCAN/MCP2515) -> D-Bus VideoPlayer, status -> CAN"
LICENSE = "CLOSED"

# files/src/  -> production source (CMake)
# files/test/ -> TEST ONLY (PC simulation + unit test), not fetched/built here
# files/*     -> installed files
SRC_URI = " \
    file://src/ \
    file://can-service.sh \
    file://can-service.service \
    file://80-can0.network \
    file://can-test.sh \
"
S = "${WORKDIR}/src"

inherit cmake pkgconfig systemd

DEPENDS = "glib-2.0"
RDEPENDS:${PN} = "dbus"

SYSTEMD_SERVICE:${PN} = "can-service.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/can-service.sh ${D}${bindir}/can-service.sh
    install -m 0755 ${WORKDIR}/can-test.sh    ${D}${bindir}/can-test.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/can-service.service ${D}${systemd_system_unitdir}/

    # can0 bitrate / restart for systemd-networkd
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/80-can0.network ${D}${sysconfdir}/systemd/network/
}

FILES:${PN} += "${sysconfdir}/systemd/network/80-can0.network"

# Test tools in a separate package: add "can-service-test" to IMAGE_INSTALL only for development
PACKAGES =+ "${PN}-test"
FILES:${PN}-test = "${bindir}/can-test.sh"
RDEPENDS:${PN}-test = "can-utils iproute2"
