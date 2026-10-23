SUMMARY = "OJT Video Player Core (GStreamer) controlled via D-Bus"
DESCRIPTION = "Gets the playlist from media-manager and exposes org.automotive.VideoPlayer"
LICENSE = "CLOSED"

# files/src/      -> source code built by CMake
# files/testdata/ -> sample videos (package video-player-testdata)
# files/*         -> files installed by this recipe
SRC_URI = " \
    file://src/ \
    file://testdata/ \
    file://video-player.sh \
    file://video-player.service \
    file://org.automotive.VideoPlayer.conf \
    file://video-player-test.sh \
"
S = "${WORKDIR}/src"

inherit cmake pkgconfig systemd

EXTRA_OECMAKE += "-DBUILD_PLAYER_TEST=ON"

DEPENDS = "gstreamer1.0 glib-2.0"

# playbin + demuxers (mp4 = isomp4, mkv = matroska) + parsers + video sinks.
# RPi4 H.264 hardware decoder = v4l2h264dec (in plugins-good video4linux2).
RDEPENDS:${PN} += " \
    dbus glib-2.0-utils \
    gstreamer1.0-plugins-base-meta \
    gstreamer1.0-plugins-good-meta \
    gstreamer1.0-plugins-bad-videoparsersbad \
"
# Software decoder (needed if the video is not H.264):
#   add  LICENSE_FLAGS_ACCEPTED += "commercial"  in local.conf, then uncomment:
# RDEPENDS:${PN} += "gstreamer1.0-libav"

SYSTEMD_SERVICE:${PN} = "video-player.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/video-player.sh      ${D}${bindir}/
    install -m 0755 ${WORKDIR}/video-player-test.sh ${D}${bindir}/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/video-player.service ${D}${systemd_system_unitdir}/

    install -d ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${WORKDIR}/org.automotive.VideoPlayer.conf ${D}${datadir}/dbus-1/system.d/

    install -d ${D}${datadir}/video-player/testdata
    install -m 0644 ${WORKDIR}/testdata/*.mp4 ${D}${datadir}/video-player/testdata/
}

# Sample videos go to a separate package: add "video-player-testdata" to IMAGE_INSTALL only when needed
PACKAGES =+ "${PN}-testdata"
FILES:${PN}-testdata = "${datadir}/video-player/testdata"

FILES:${PN} += "${datadir}/dbus-1/system.d"
