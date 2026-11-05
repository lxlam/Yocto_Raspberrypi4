# OJT CAN Monitor (PC tool, kiểu CANoe)

App Python trên PC để **xem và điều khiển bus CAN** của dự án qua Arduino UNO + MCP2515
(`arduino/ojt-can-node`). Cài đặt: [docs/ojt-build-deploy.html](../../docs/ojt-build-deploy.html) phần 09 · cách dùng: [docs/ojt-test.html](../../docs/ojt-test.html) phần 06.

```
PC (can_monitor.py) --USB/COM--> Arduino UNO + MCP2515 --CAN 500k--> Raspberry Pi (can-service)
```

## Cài đặt (Windows, 1 lần)

1. Cài **Python 3.8+** từ python.org (tick *Add python.exe to PATH*). Tkinter đã có sẵn trong bản cài này.
2. Mở cmd trong thư mục này:
   ```
   py -m pip install -r requirements.txt
   ```
3. Arduino phải chạy firmware có lệnh `trace` (bản trong repo từ 05/11/2026): nạp lại bằng
   `arduino-cli compile -b arduino:avr:uno -p COMxx -u ..\..\arduino\ojt-can-node`.

## Chạy

```
py can_monitor.py          (hoặc double-click run.bat)
```
Chọn cổng `COMxx` → **Connect**. Đóng PuTTY / `arduino-cli monitor` trước (một cổng COM chỉ một chương trình mở được).
Không có phần cứng: chọn **DEMO (simulated Pi)** để thử giao diện.

## Chức năng

| Phần | Nội dung |
|---|---|
| Player control | Nút Prev / Play / Pause / Stop / Next / Status, REW / FF (ms), Play index, gửi GET_STATUS định kỳ |
| Raw frame | Gửi payload tuỳ ý lên 0x100, tự thêm XOR checksum (test BAD_DLC / BAD_CHECKSUM / BAD_OPCODE) |
| Arduino node | selftest, spitest, info, regs |
| Signals | Giải mã STATUS / HEALTH / RESPONSE: state, index, vị trí phát, USB, cờ service, latency |
| Trace | Mọi frame Rx/Tx: thời gian (s), Δt cùng ID (ms), ID, DLC, data, giải mã; lỗi (no ACK, timeout…) tô đỏ; chế độ *Fixed position*; lọc ID; pause |
| Statistics | Theo ID: số frame, tần số, chu kỳ avg/min/max, jitter; bus load; latency CMD→RESPONSE |
| Graphics | Vị trí phát theo thời gian, chu kỳ STATUS/HEALTH so với 500/1000 ms, frame/s theo ID, latency từng lệnh |
| Console | Toàn bộ text từ Arduino + ô gõ lệnh tự do |
| Export | `.asc` (Vector ASCII, mở được bằng CANoe/CANalyzer) và `.csv` |

Thời gian là `micros()` của Arduino lúc nó đọc frame từ MCP2515 (sai số cỡ 1–4 ms do vòng lặp và in Serial),
đủ để xem chu kỳ 500 ms / 1000 ms và latency lệnh, không dùng để đo µs như phần cứng Vector.

## Trạng thái kiểm chứng

| Mục | Trạng thái |
|---|---|
| Chạy end-to-end: firmware thật trên giả lập ATmega328P (simavr) + mô hình MCP2515 + Pi giả lập, nối với app qua cổng serial ảo: trace, giải mã, Δt, thống kê, đồ thị, latency, raw frame BAD_CHECKSUM, REJECTED/OUT_OF_RANGE | **Pass** (Linux, Python 3.12) |
| Chế độ DEMO | **Pass** |
| Windows + UNO thật + Pi | **Chưa xác nhận** |
