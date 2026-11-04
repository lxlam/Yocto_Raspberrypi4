# Generate WIC image artifacts for images such as core-image-minimal.
IMAGE_FSTYPES = "wic"


IMAGE_INSTALL += " \
    file-operation \
    network-config \
    media-manager \
    video-player \
    ojt-gui \
    kernel-modules \
    can-service \
"