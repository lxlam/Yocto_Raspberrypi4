# HƯỚNG DẪN BUILD DỰ ÁN (BUILD GUIDE)
## Yocto Scarthgap cho Raspberry Pi 4 (64-bit) & Local PC Build

Tài liệu này hướng dẫn chi tiết quy trình thiết lập môi trường, cấu hình và biên dịch hệ điều hành nhúng Linux cho Raspberry Pi 4 sử dụng Yocto Project (nhánh `scarthgap`), tích hợp layer `meta-ojt` cùng các tiến trình phát video.

---

## 1. Yêu Cầu Phần Cứng & Môi Trường Host

### 1.1. Cấu hình máy tính Build (Host PC)
* **Hệ điều hành:** Linux Ubuntu 22.04 LTS hoặc 24.04 LTS (khuyên dùng 64-bit).
* **Dung lượng ổ đĩa:** Trống tối thiểu **100 GB** (khuyên dùng SSD).
* **Bộ nhớ RAM:** Tối thiểu **16 GB** (khuyến nghị 32 GB hoặc cấu hình Swap từ 16GB trở lên để tránh OOM khi compile WebKit / Qt5).
* **CPU:** Đa nhân (từ 4 cores / 8 threads trở lên).

### 1.2. Cài đặt các gói phụ thuộc trên Ubuntu Host
```bash
sudo apt update && sudo apt install -y \
    gawk wget git diffstat unzip texinfo gcc build-essential \
    chrpath socat cpio python3 python3-pip python3-pexpect \
    xz-utils debianutils iputils-ping python3-git python3-jinja2 \
    python3-subunit zstd liblz4-tool file locales libacl1 \
    bmap-tools
```

---

## 2. Chuẩn Bị Mã Nguồn & Layers

Hệ thống sử dụng Yocto phiên bản **Scarthgap** (LTS 5.0). Cấu trúc thư mục nguồn:

```bash
mkdir -p ~/yocto && cd ~/yocto

# 1. Clone Poky Scarthgap
git clone -b scarthgap git://git.yoctoproject.org/poky

# 2. Clone BSP Layer cho Raspberry Pi
git clone -b scarthgap https://github.com/agherzan/meta-raspberrypi

# 3. Clone Meta OpenEmbedded
git clone -b scarthgap https://github.com/openembedded/meta-openembedded

# 4. Clone Meta Qt5
git clone -b scarthgap https://github.com/meta-qt5/meta-qt5

# 5. Layer meta-ojt (nằm trong thư mục dự án này)
# rasp/custom-source/meta-ojt
```

---

## 3. Khởi Tạo Môi Trường Build

```bash
cd ~/yocto
source poky/oe-init-build-env build
```

Thêm các layer bắt buộc vào cấu hình:
```bash
bitbake-layers add-layer ../meta-raspberrypi
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-qt5
bitbake-layers add-layer /home/hieupm/Documents/yocto_project/Yocto_Raspberrypi4/rasp/custom-source/meta-ojt
```

Kiểm tra danh sách layer:
```bash
bitbake-layers show-layers
```

---

## 4. Cấu Hình `conf/local.conf`

Mở file `build/conf/local.conf` và thêm cấu hình chuẩn cho Raspberry Pi 4 64-bit:

```bitbake
# 1. Kiến trúc và Machine mục tiêu
MACHINE = "raspberrypi4-64"

# 2. Hệ thống khởi tạo Init Manager (bắt buộc dùng systemd cho các service)
INIT_MANAGER = "systemd"

# 3. Tính năng Distro: Bắt buộc bật X11 và OpenGL cho Qt5 GUI và GStreamer Video Sink
DISTRO_FEATURES:append = " x11 opengl"

# 4. Cấu hình Font tiếng Việt cho Qt5
PACKAGECONFIG:append:pn-qtbase = " fontconfig"
IMAGE_INSTALL:append = " ttf-dejavu-sans"

# 5. Cài đặt các tiến trình OJT và Kernel Modules
IMAGE_INSTALL:append = " \
    media-manager \
    video-player \
    ojt-gui \
    kernel-modules \
    procps \
    sysstat \
"

# 6. Màn hình hiển thị:
# Nếu dùng màn hình cảm ứng Raspberry Pi 7" DSI Display:
RPI_EXTRA_CONFIG = "dtoverlay=vc4-kms-dsi-7inch"
# (Nếu dùng màn hình HDMI tiêu chuẩn, bỏ dòng RPI_EXTRA_CONFIG trên)

# 7. Debug và Truy cập SSH từ xa
EXTRA_IMAGE_FEATURES += "ssh-server-dropbear debug-tweaks"

# 8. Định dạng Image xuất xưởng
IMAGE_FSTYPES = "wic.bz2 wic.bmap"

# 9. Tùy chọn video mẫu và bộ giải mã thương mại (H.265 / AAC Audio)
# IMAGE_INSTALL:append = " video-player-testdata"
# LICENSE_FLAGS_ACCEPTED += "commercial"
```

---

## 5. Tiến Hành Biên Dịch Image

Biên dịch image tối giản `core-image-minimal` đã được gắn thêm các service:

```bash
bitbake core-image-minimal
```

> [!NOTE]
> Không nên build `core-image-x11` hoặc `core-image-sato` vì các image này mặc định khởi chạy `xserver-nodm-init` chiếm quyền điều khiển Display `:0`, dẫn đến xung đột với `ojt-gui.service`.

Thời gian build lần đầu mất khoảng 1.5 - 3 giờ tùy thuộc vào tốc độ mạng và CPU. Sau khi hoàn tất, file artifact nằm tại:
`tmp/deploy/images/raspberrypi4-64/core-image-minimal-raspberrypi4-64.rootfs.wic.bz2`

---

## 6. Biên Dịch Nhanh Trên Máy Tính Host (PC Linux Build)

Trong quá trình phát triển tính năng, lập trình viên có thể biên dịch và chạy thử ngay trên PC x86_64 mà không cần build lại toàn bộ Yocto:

### 6.1. Cài thư viện phụ thuộc trên Host
```bash
sudo apt install -y cmake g++ libglib2.0-dev libudev-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    qtbase5-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-libav gstreamer1.0-x
```

### 6.2. Lệnh biên dịch từng module bằng CMake

#### A. Biên dịch Media Manager:
```bash
cd /home/hieupm/Documents/yocto_project/Yocto_Raspberrypi4/rasp/custom-source/meta-ojt/recipes-multimedia/media-manager/files/src
cmake -B build
cmake --build build -j$(nproc)
```

#### B. Biên dịch Video Player:
```bash
cd /home/hieupm/Documents/yocto_project/Yocto_Raspberrypi4/rasp/custom-source/meta-ojt/recipes-multimedia/video-player/files/src
cmake -B build
cmake --build build -j$(nproc)
```

#### C. Biên dịch OJT GUI:
```bash
cd /home/hieupm/Documents/yocto_project/Yocto_Raspberrypi4/rasp/custom-source/meta-ojt/recipes-qt/ojt-gui/files/src
cmake -B build
cmake --build build -j$(nproc)
```
