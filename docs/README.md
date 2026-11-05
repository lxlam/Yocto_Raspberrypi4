# Tài liệu — OJT USB Video Player + điều khiển CAN (Raspberry Pi 4, Yocto Scarthgap, Arduino UNO)

Mở bằng trình duyệt (chạy offline). 4 file liên kết với nhau qua thanh trên cùng.

| File | Nội dung |
|---|---|
| [ojt-architecture.html](ojt-architecture.html) | Kiến trúc, block, module/file, **API** từng block và quan hệ API giữa các block, luồng hoạt động (sequence), state machine, giao thức CAN v1, USB, video trong GUI, giao diện Qt, cây source |
| [ojt-hardware.html](ojt-hardware.html) | **Kết nối phần cứng**: linh kiện, Pi 4 (nguồn, màn DSI/HDMI, USB, mạng, UART), sơ đồ CAN, Pi ↔ MCP2515, UNO ↔ MCP2515, PC ↔ UNO, bus/J1/termination, checklist, lỗi phần cứng đã gặp |
| [ojt-build-deploy.html](ojt-build-deploy.html) | **Build & triển khai**: `scripts/setup-layers.sh`, `local.conf`, build image, ghi thẻ SD, boot, SSH, cập nhật nhanh, chuẩn bị video, nạp firmware Arduino bằng CLI, cài OJT CAN Monitor |
| [ojt-test.html](ojt-test.html) | **Kiểm thử**: test PC, test service trên Pi, checklist USB/GUI, test CAN với Arduino UNO, OJT CAN Monitor, test không cần Arduino, soak test, trạng thái kiểm chứng, xử lý sự cố |

Lịch sử: 05/11/2026 gộp `ojt-usb-video-player.html`, `ojt-arduino-can-test.html`, `CAN_DESIGN.md`, `CAN_BUILD_TEST.md`,
`can-wiring.svg`, `pc-stm32-wiring.svg` và `SOURCE_TRACKING.md` vào 4 file trên; node test STM32F411 được thay bằng Arduino UNO.
