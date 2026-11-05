# Generate WIC image artifacts for images such as core-image-minimal.
IMAGE_FSTYPES = "wic"


IMAGE_INSTALL += " \
    network-config \
    media-manager \
    video-player \
    ojt-gui \
    can-service \
    can-service-test \
    kernel-modules \
"

# can-service-test = can-test.sh + can-utils (cansend/candump) for CAN bring-up.
# Test tools only: remove "can-service-test" above for a production image.
