# ojt-can-node — Arduino UNO + MCP2515 (CAN test node)

Node test CAN của dự án (thay cho node STM32F411 cũ). Giao thức v1, lệnh console, log dòng `#F`/`#E` cho OJT CAN Monitor.
Hướng dẫn đầy đủ: đấu dây [docs/ojt-hardware.html](../../docs/ojt-hardware.html) (phần 05–06), nạp code [docs/ojt-build-deploy.html](../../docs/ojt-build-deploy.html) (phần 08), test [docs/ojt-test.html](../../docs/ojt-test.html) (phần 05).

```
ojt-can-node.ino       # setup()/loop(): chỉ gọi app_init()/app_loop()
app_can.h              # chân, crystal, bitrate
app_can.cpp            # logic node: console, nút, timeout, xác nhận STATUS (port của app_can.c)
mcp2515.h / .cpp       # driver MCP2515 dùng thư viện SPI của Arduino (port của mcp2515.c)
ojt_can_protocol.h / .c  # FILE CHUNG với can-service trên Pi (giống hệt, không sửa riêng)
```

Board: **Arduino UNO R3** (ATmega328P) hoặc bản clone (CH340). Không cần cài thêm thư viện.
UNO R4 (Minima/WiFi) **không** dùng được bản này (code dùng `printf_P` của AVR).

## Đấu dây (UNO ↔ module MCP2515)

| MCP2515 | Arduino UNO |
|---|---|
| VCC | 5V |
| GND | GND |
| SCK | D13 |
| SI (MOSI) | D11 |
| SO (MISO) | D12 |
| CS | D10 |
| INT | D2 |

Tuỳ chọn: nút nhấn D4 → GND (demo từng bước); LED + điện trở 220 Ω ở D5 xanh lá (Pi online), D6 cam (PLAYING), D7 đỏ (lỗi), D8 xanh dương (TX).
Không nối gì vào D0/D1 (đang dùng cho cổng USB/COM).

## Nạp code bằng dòng lệnh (không cần Arduino IDE)

Chi tiết: [docs/ojt-build-deploy.html](../../docs/ojt-build-deploy.html) phần 08.

**Cách A — avrdude + file .hex có sẵn** (`prebuilt/`, build từ source hiện tại):

```bat
rem tìm cổng COM:  PowerShell -> Get-PnpDevice -Class Ports -PresentOnly
cd arduino\ojt-can-node\prebuilt
avrdude -c arduino -p m328p -P COM5 -b 115200 -D -U flash:w:ojt-can-node-8mhz.hex:i
rem hoặc:  flash.bat COM5      (16 MHz: flash.bat COM5 16 ; clone cũ: flash.bat COM5 8 57600)
```

| File | Crystal module MCP2515 |
|---|---|
| `prebuilt/ojt-can-node-8mhz.hex` | 8 MHz |
| `prebuilt/ojt-can-node-16mhz.hex` | 16 MHz |

Sửa source thì `.hex` **không** tự cập nhật → dùng cách B.

**Cách B — arduino-cli** (build từ source, cần mạng tới downloads.arduino.cc lần đầu):

```bat
arduino-cli core update-index
arduino-cli core install arduino:avr
arduino-cli compile -b arduino:avr:uno arduino\ojt-can-node
arduino-cli upload  -b arduino:avr:uno -p COM5 arduino\ojt-can-node
```

Crystal 16 MHz → sửa `APP_MCP2515_OSC_HZ` trong `app_can.h` trước khi compile (Pi: `CAN_OSCILLATOR` theo module của Pi).

**Công cụ PC kiểu CANoe**: [tools/can-monitor](../../tools/can-monitor/README.md) (app Python: nút điều khiển, trace, thống kê, đồ thị). Firmware có lệnh `trace on|off` cho app này.

**Gõ lệnh**: PuTTY → Serial, `COM5`, `115200` (hoặc `arduino-cli monitor -p COM5 -c baudrate=115200`). Gõ `selftest`, `spitest`, `info`, `play`… rồi Enter.

## Trạng thái kiểm chứng

| Mục | Trạng thái |
|---|---|
| Compile avr-gcc 7.3 + ArduinoCore-avr 1.8.6, `-Wall -Wextra` | **Built**, 0 warning — flash 18 426 B (57 %), RAM 953 B (46 %) |
| Logic console/timeout/confirm trên PC (mock MCP2515 + Pi giả) | **Pass** |
| `prebuilt/*.hex` trên giả lập ATmega328P (simavr) + mô hình MCP2515: banner, selftest (5 bước), spitest, ACK / no ACK / RX kẹt / dây SI lỗi, TIMEOUT | **Pass** (8 và 16 MHz) |
| Chạy trên UNO thật, SPI tới MCP2515, CAN với Pi | **Chưa xác nhận** |
