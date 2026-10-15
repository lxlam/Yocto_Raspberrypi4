SUMMARY = "Ethernet DHCP configuration for systemd-networkd"
LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "${THISDIR}/source:"
SRC_URI = "file://20-wired.network"

S = "${WORKDIR}"

inherit systemd

do_install() {
    install -d ${D}${sysconfdir}/systemd/network

    install -m 0644 ${WORKDIR}/20-wired.network ${D}${sysconfdir}/systemd/network/20-wired.network
}

FILES:${PN} += "${sysconfdir}/systemd/network/20-wired.network"