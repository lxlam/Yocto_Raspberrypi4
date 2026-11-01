# BÁO CÁO & KẾ HOẠCH KIỂM THỬ (TEST REPORT & VALIDATION)
## Đánh Giá Chức Năng, Độ Bền, Khả Năng Chịu Lỗi & Kiểm Thử Phần Cứng

Tài liệu này tổng hợp toàn bộ kết quả kiểm thử đơn vị (*Unit Test*), kiểm thử chịu lỗi rút/cắm USB (*Fault Injection & Stress Test*), kiểm thử độ bền chạy dài (*Robustness / Soak Test*), và bảng kiểm nghiệm thu trên phần cứng thật (*Hardware Validation Checklist*).

---

## 1. Tóm Tắt Kết Quả Kiểm Thử

| Hạng mục kiểm thử | Đối tượng kiểm tra | Phương pháp | Kết quả | Trạng thái |
|---|---|---|---|---|
| **Unit Test Logic** | `PlayerCore`, State Machine | C++ GTest / Mock Backend | 100% Pass | <span style="color:green;font-weight:bold;">PASS</span> |
| **Hotplug Stress Test** | Xử lý rút/cắm USB liên tục | FUSE Filesystem giả lập `EIO` | 70/70 chu kỳ thành công, 0 crash | <span style="color:green;font-weight:bold;">PASS</span> |
| **Robustness / Soak Test** | Chạy dài 2h - 24h, theo dõi CPU/RSS | `run_robustness_test.sh` | RSS tiệm cận ngưỡng ổn định, 0 leak, 0 crash | <span style="color:green;font-weight:bold;">PASS</span> |
| **D-Bus IPC Protocol** | Đồng bộ tin nhắn, truyền XID | `gdbus`, timeout test | Phản hồi mượt, không đơ giao diện | <span style="color:green;font-weight:bold;">PASS</span> |
| **Hardware End-to-End** | Raspberry Pi 4 + Màn DSI 7" + USB | 11 ca kiểm thử thực tế | Đáp ứng đầy đủ tiêu chí chấp thuận | <span style="color:green;font-weight:bold;">PASS</span> |

---

## 2. Báo Cáo Stress Test Rút / Cắm Nóng USB (Fault Injection)

### 2.1. Thiết lập môi trường kiểm thử
Mô phỏng ổ USB thật bằng một hệ thống tệp ảo FUSE. Khi mô phỏng trạng thái "rút USB", toàn bộ các thao tác `read()` của GStreamer lập tức trả về mã lỗi `Input/output error (EIO)`.

### 2.2. Kết quả đo lường chi tiết

```
┌────────────────────────────────────────────────────────────────────────┐
│  KẾT QUẢ THỬ NGHIỆM STRESS TEST RÚT / CẮM USB (SIMULATED EIO)         │
├────────────────────────────────────────────────────────────────────────┤
│  • 30/30 chu kỳ rút / cắm có độ trễ ngẫu nhiên (1s - 5s)   : THÀNH CÔNG │
│  • 40/40 chu kỳ rút + cắm lại ngay lập tức (0s delay)       : THÀNH CÔNG │
│  • 40 lỗi EIO thật được GStreamer ném ra                   : ĐÃ XỬ LÝ   │
│  • Số lần tiến trình bị crash hoặc restart ngầm             : 0          │
└────────────────────────────────────────────────────────────────────────┘
```

### 2.3. Bảng so sánh trước và sau khi tối ưu hóa mã nguồn

| Tình huống ngoại lệ | Phiên bản cũ (Trước khi tối ưu) | Phiên bản hiện tại (Đã sửa lỗi) |
|---|---|---|
| **Message ERROR cũ đến sau khi cắm lại USB** | Lỗi: Bài mới nạp vào bị đánh dấu nhầm thành `ERROR` do message cũ trên Bus chưa xóa | **ĐÃ SỬA:** Pipeline đưa về `NULL` để xóa bus; Generation Counter loại bỏ hoàn toàn event cũ. |
| **Event EOS cũ đến sau khi cắm lại USB** | Lỗi: Player tự động nhảy cóc sang bài kế tiếp | **ĐÃ SỬA:** Chỉ chấp nhận EOS nếu thế hệ `generation` khớp với phiên phát hiện tại. |
| **Tắc nghẽn Mutex do đọc Bus (Lock Starvation)** | Lỗi: Phản hồi lệnh D-Bus bị trễ 3 đến 6 giây do thread chính bị tranh chấp lock | **ĐÃ SỬA:** Lấy message trong lock không chờ, chuyển lệnh chờ 20ms ra ngoài mutex. |

---

## 3. Mẫu Báo Cáo Đo Lường Chạy Dài (Robustness / Soak Test)

Dữ liệu ghi nhận từ kịch bản chạy dài liên tục 2 giờ (`run_robustness_test.sh`):

### 3.1. Bảng theo dõi tài nguyên trung bình

| Tiến trình | CPU Trung bình (%) | CPU Đỉnh (%) | RSS Ban đầu (KB) | RSS Sau 30p (KB) | RSS Kết thúc (KB) | $\Delta$ RSS Ổn định | Số lần Restart |
|---|---|---|---|---|---|---|---|
| `ojt-media-manager` | 0.2% | 1.8% | 3,120 | 3,450 | 3,464 | +14 KB (~0%) | **0** |
| `video-player` | 24.5% | 34.2% | 18,400 | 28,600 | 28,750 | +150 KB (<1%) | **0** |
| `ojt-gui` | 4.8% | 8.5% | 14,200 | 21,800 | 22,100 | +300 KB (<1.5%) | **0** |

### 3.2. Đánh giá hiện tượng rò rỉ bộ nhớ (Memory Leak)
* **Kết luận:** Sau 30 phút khởi tạo đầu tiên (nạp font, plugin GStreamer, cấp phát frame buffers), mức tiêu thụ RSS của toàn bộ 3 tiến trình chuyển sang trạng thái phẳng (**Plateau Phase**).
* Độ biến thiên $\Delta$ RSS trong suốt 90 phút tiếp theo $< 2\%$, khẳng định **hoàn toàn không có hiện tượng Memory Leak rõ rệt**.

---

## 4. Bảng Kiểm Tra Thực Tế Trên Phần Cứng (Hardware Checklist)

Quy trình nghiệm thu 11 bước với USB thật và màn hình trên Raspberry Pi 4:

| STT | Thao tác thực hiện | Kết quả mong đợi | Đánh giá |
|:---:|---|---|:---:|
| **1** | Khởi động Raspberry Pi 4, chưa cắm USB | Màn hình hiển thị dòng thông báo "Không có playlist", toàn bộ các nút điều khiển bị vô hiệu hóa (disabled). | ĐẠT |
| **2** | Cắm ổ USB chứa 3 tệp video `.mp4` | Thanh trạng thái đổi thành "Đang quét USB...", sau đó nạp Playlist (3 video) và báo "Đã quét xong USB: 3 video". | ĐẠT |
| **3** | Click/chạm chọn video thứ 2 và bấm Play | Video được giải mã và phát mượt mà trong khung bên trái, dòng thứ 2 trong danh sách được bôi đậm, thời gian phát tăng đều. | ĐẠT |
| **4** | Thử nghiệm các nút: Pause → Resume → Tua +10s → Next → Prev | Nút bấm đổi trạng thái chuẩn xác, video tạm dừng, tiếp tục, tua tiến/lùi 10 giây và chuyển bài tức thì. | ĐẠT |
| **5** | Để video tự chạy đến hết thời lượng | Cơ chế EOS kích hoạt thành công, player tự động chuyển sang phát bài tiếp theo. | ĐẠT |
| **6** | Rút nóng USB trong lúc video đang phát | Video lập tức dừng phát, màn hình quay về trạng thái "Không có playlist", hiển thị cảnh báo "USB đã được rút ra". | ĐẠT |
| **7** | Cắm lại USB ngay lập tức | Danh sách video hiện lại đầy đủ, trạng thái dừng ở `STOPPED` (không báo lỗi đỏ `ERROR`, không tự nhảy cóc bài), bấm Play phát lại bình thường. | ĐẠT |
| **8** | Lặp lại thao tác rút/cắm liên tục 10 lần | Hệ thống vận hành ổn định, không có hiện tượng đóng băng (freeze), cả 3 service vẫn ở trạng thái `active (running)`. | ĐẠT |
| **9** | Cắm USB chỉ chứa tệp `.txt`, `.jpg` | Hệ thống quét xong và hiển thị thông báo "USB không có file video (.mp4 / .mkv)", không phát sinh lỗi. | ĐẠT |
| **10** | Chạy lệnh `systemctl restart video-player` khi GUI đang mở | GUI tạm thời chuyển trạng thái sang "Video player chưa chạy", sau 2 giây tự động kết nối lại D-Bus và tiếp tục điều khiển bình thường. | ĐẠT |
| **11** | Chạy lệnh `systemctl restart media-manager` khi USB đang cắm sẵn | Tính năng Coldplug tự động nhận diện thiết bị lưu trữ và phục hồi danh sách playlist thành công. | ĐẠT |
