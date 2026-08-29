# README — Nghiệp vụ & Kiến trúc Firmware Hệ thống Giám sát Ao Tôm (3 ao độc lập)

> **Tài liệu này là đặc tả bắt buộc (mandatory spec).** Mọi tác nhân AI (AI agent) tham gia viết, sửa, hoặc review code cho dự án này **PHẢI tuân thủ đúng** các nghiệp vụ và ràng buộc dưới đây. Không được tự ý bỏ bớt, đơn giản hóa, hoặc thay đổi hành vi đã quy định — trừ khi người phụ trách dự án (chủ thớt) chỉnh sửa trực tiếp tài liệu này. Nếu một yêu cầu trong code không rõ ràng, ưu tiên bám theo tài liệu này thay vì suy đoán.

---

## 1. Bối cảnh & Mục tiêu

Hệ thống gồm **3 bộ thiết bị IoT độc lập**, mỗi bộ gắn cho 1 ao tôm, tự động đo pH, DO (oxy hòa tan), dòng điện tiêu thụ của quạt oxy, tự phát hiện bất thường và cảnh báo qua **relay tại chỗ** + **app di động từ xa**.

> **Ràng buộc phạm vi quan trọng — KHÔNG được vi phạm:** Relay trong hệ thống này **chỉ dùng để bật loa/còi báo động tại chỗ** nhằm đánh thức người trực khi họ ngủ quên hoặc điện thoại mất mạng không nhận được thông báo app. Hệ thống **hoàn toàn không can thiệp/điều khiển trực tiếp thiết bị lớn trong ao** (quạt oxy, máy bơm, v.v.) — đó vẫn là hệ thống điện/cơ khí độc lập do người vận hành tự xử lý sau khi nghe cảnh báo. Mọi thiết kế relay, fail-safe, và logic cảnh báo trong tài liệu này phải tuân theo đúng phạm vi này.

Yêu cầu sống còn: **thiết bị phải tự ra quyết định cục bộ (edge processing) ngay cả khi mất Internet**, vì ao tôm ở vị trí xa, mạng không ổn định. Đây là ranh giới giữa một hệ thống IoT nghiệp dư và một hệ thống thực tế đáng tin cậy — **mọi thiết kế sau này phải phục vụ nguyên tắc này trước tiên.**

---

## 2. Kiến trúc phần cứng (cố định, không thay đổi)

| Thành phần | Chi tiết |
|---|---|
| MCU | ESP32-S3-DevKitC1 N16R8 (Flash 16MB / PSRAM 8MB) |
| Nguồn | 12V DC cho toàn hệ thống |
| Cảm biến pH | RS-PH-N01-3 — RS485 Modbus RTU | (THAY THẾ CHO CẢM BIẾN pH SỬ DỤNG TÍN HIỆU ANALOG)
| Cảm biến DO | RS-LDOS-N01-2-20-EX — RS485 Modbus RTU |
| Cảm biến dòng điện | PZEM-016 — RS485 Modbus RTU |
| Chuyển đổi RS485 | UART-TTL → RS485, Tx = GPIO17, Rx = GPIO18 |
| Relay 1 & 2 | GPIO5, GPIO6 (mức cao – active high, xem mục 5.3 về fail-safe) |
| LED trạng thái | GPIO4 |
| Giao thức http poling  Firebase Realtime Database |
| App di động | Do nhóm khác phát triển, giao tiếp qua htttp Firebase |

---

## 3. Nghiệp vụ lõi đã xác định

1. Đồng bộ baudrate 3 cảm biến về 9600 lần đầu, lưu cờ "đã cấu hình" vào NVS. (bỏ qua không thực hiện riêng lẻ ở một chương trình khác)
2. Cấu hình địa chỉ Modbus riêng biệt cho từng cảm biến trên cùng 1 bus RS485 (bỏ qua tui đã cấu hình ở ).
3. So sánh giá trị đo với ngưỡng — ngưỡng **không hardcode**, có thể cập nhật từ app.
4. Khi vượt ngưỡng: dòng điện bất thường → relay đóng liên tục (bảo vệ/cảnh báo); pH/DO vượt ngưỡng → relay hoạt động theo chu kỳ bật/tắt (tránh sốc tôm).
5. Watchdog Timer (WDT) phần cứng để tự reset khi treo.
6. Toàn bộ logic đọc – so sánh – điều khiển relay chạy **độc lập tại thiết bị**, không phụ thuộc cloud (nhưng vẫn còn ràng buộc các giá trị được thiết lập trên cloude xuống nhé).
7. WiFi STA là chính, AP là dự phòng; LED GPIO4  báo trạng thái kết nối. và gpo chưa xác định (sẽ update sau)  để thông báo lỗi của cảm biến 
8. Đồng bộ thời gian qua NTP khi có mạng; dự phòng RTC ngoài về lâu dài.
9. Gói tin gửi server gồm: giá trị đo, trạng thái kết nối, trạng thái lỗi — không chỉ số liệu thô.
10. giữ lại một số dữ liệu gửi lên nhánh để không ảnh hưởng đến app  (xem phần 4.15)
---

## 4. NGHIỆP VỤ BỔ SUNG BẮT BUỘC (phần quan trọng nhất — nếu bỏ qua sẽ gây lỗi hệ thống thật ngoài ao)

Tài liệu gốc mô tả đúng luồng chính nhưng **thiếu các lớp phòng vệ** khiến hệ thống dễ gãy khi vận hành 24/7 ngoài trời, không người trực. Các mục dưới đây là **bắt buộc**, không phải tùy chọn.

### 4.1. Độ tin cậy Modbus/RS485
- Đọc lỗi (timeout, sai CRC, không phản hồi) phải **retry tối đa 3 lần** trước khi kết luận lỗi.
- Phân biệt rõ 2 trạng thái: `SENSOR_FAULT` (cảm biến hỏng/mất kết nối) khác với `VALUE_OUT_OF_RANGE` (môi trường ao thực sự bất thường). **Không được gộp chung** — nếu gộp, một lần đọc lỗi có thể bị hiểu nhầm là "pH tụt về 0" và kích cảnh báo giả.
- Vì 3 cảm biến dùng chung 1 bus RS485, **bắt buộc dùng mutex (`busMutex`)** khi gửi/nhận Modbus, với timeout hợp lý (ví dụ 200–500ms) để tránh 1 task giữ bus vô thời hạn làm treo toàn hệ thống.
- Polling round-robin: đọc tuần tự pH → DO → dòng điện, không đọc song song trên cùng bus.

### 4.2. Debounce & chống cảnh báo giả
- **Không được** kích relay/cảnh báo ngay khi vừa có 1 lần đọc vượt ngưỡng (nhiễu điện hóa của cảm biến pH/DO là bình thường).
- Chỉ kích cảnh báo khi giá trị vượt ngưỡng **liên tục ≥ N lần đọc  (mặc định đề xuất: 3 lần đọc liên tiếp ). Giá trị N phải là tham số cấu hình được, không hardcode.
- Áp dụng debounce riêng cho từng loại cảnh báo (pH, DO, dòng điện) vì bản chất nhiễu khác nhau.

### 4.3. Lưu trữ dữ liệu tạm khi mất mạng (offline buffering)
- Trong lúc mất mạng, dữ liệu đo **không được phép biến mất chỉ quan tâm đến dữ liệu trong nhánh update dùng để vẽ biểu đồ thôi chỉ ghi từ 0.5 đến 1 giờ cho mỗi lần**. Phải có vùng đệm (circular buffer trong RAM/PSRAM hoặc file trên flash — LittleFS/SPIFFS).
- Khi buffer đầy trong lúc mất mạng kéo dài: chính sách bắt buộc là **ghi đè bản ghi cũ nhất (drop-oldest)**, không phải chặn ghi mới — vì dữ liệu gần nhất quan trọng hơn cho việc phát hiện sự cố đang diễn ra.
- Khi có mạng trở lại: tự động gửi bù (backfill) dữ liệu đã đệm lên server theo đúng thứ tự thời gian, gắn cờ `is_backfilled = true` để phân biệt với dữ liệu realtime.
- **Không ghi buffer trực tiếp xuống flash mỗi lần đọc** (chu kỳ đo có thể vài giây) — điều này làm mòn flash nhanh. Gom nhiều bản ghi rồi ghi theo batch (ví dụ mỗi 1–5 phút hoặc khi buffer RAM đầy).

### 4.4. Bảo mật đường truyền
- `secrets.h` (API key, mật khẩu Firebase, WiFi AP password) tuyệt đối không commit vào git — đã quy định trong Phase 1 refactor, giữ nguyên nguyên tắc này.
- Xác thực chiều ngược lại: thiết bị phải kiểm tra lệnh điều khiển (đổi ngưỡng, bật/tắt relay từ xa) đến từ topic/nguồn hợp lệ, không thực thi lệnh không rõ nguồn gốc.

### 4.5. Độ tin cậy kết nối WiFi/HTTP Firebase — **trọng tâm bắt buộc**
> Đây là phần quan trọng nhất theo yêu cầu vì ao tôm ở vùng mạng yếu/không ổn định. Xem **Mục 5** riêng bên dưới — thiết kế chi tiết state machine.

### 4.6. An toàn vật lý của relay (fail-safe)
- Phải xác định rõ **trạng thái an toàn mặc định** của relay khi ESP32 mất điện, đang khởi động lại, hoặc bị treo/reset bởi WDT.
- Sau khi WDT reset thiết bị, firmware phải khôi phục lại đúng trạng thái relay dựa trên dữ liệu đo mới nhất, không giữ nguyên trạng thái relay "treo" từ trước khi crash.

### 4.7. Giám sát nguồn điện (brownout) (Bỏ qua không thực hiện)
- Nguồn 12V ngoài trời dễ bị sụt áp (dây dài, thời tiết, tải quạt oxy lớn khởi động cùng lúc). Phải bật/kiểm tra cơ chế brownout detector của ESP32 và log lại sự kiện brownout gần nhất vào NVS để phục vụ chẩn đoán, không để thiết bị "tự nhiên restart" mà không rõ nguyên nhân trong log.

### 4.8. Đồng bộ thời gian khi mất mạng kéo dài
- Nếu mất mạng lâu (không NTP được), timestamp phải dùng `millis()`/RTC nội bộ làm tương đối, và khi có mạng trở lại phải **hiệu chỉnh lại (reconcile)** timestamp của dữ liệu đã đệm dựa trên độ lệch giữa đồng hồ hệ thống và thời gian NTP mới nhận được — không được gửi dữ liệu backfill với timestamp sai lệch hàng giờ.

### 4.10. Cập nhật firmware từ xa an toàn (OTA)
- Vì thiết bị đặt xa, khó tiếp cận vật lý, **OTA phải có cơ chế rollback**: nếu firmware mới không gửi được "heartbeat OK" trong X phút đầu sau khi cập nhật, thiết bị tự động quay lại firmware cũ (dùng cơ chế 2 phân vùng OTA có sẵn của ESP32 — `esp_ota_mark_app_valid_cancel_rollback`).
- Không OTA khi hệ thống đang trong trạng thái cảnh báo tích cực (đang xử lý sự cố ao) — trì hoãn OTA đến khi ổn định.

### 4.11. Logging & chẩn đoán lỗi cục bộ
- Lưu log lỗi (sensor fault, brownout, WDT reset, mất kết nối kéo dài) vào NVS/flash theo dạng ring buffer nhỏ gọn, có thể truy xuất qua app/Firebase khi cần debug hiện trường — không chỉ dựa vào Serial log cần cân nhắc nếu được đi chăn nữa thì vẫn chỉ lưu log khi esp32 có hoạt động lỗi và chỉ lưu thời gian cũng như là ánh xạ mã lỗi để tiết kiệm bộ nhớ (vì không ai cắm dây debug tại ao).

### 4.12. LED trạng thái đa chế độ
-(GPIO4) để phân biệt kết nối wifi và HTTP Firebase nếu 1 trong hai lỗi thì nhấp nháy với wifi thì nháy chậm 1s 1 lần còn Firebase thì 0.5s 1 lần cả hai đều lỗi thì nháy liên tục . thêm một led ở chân gpo chưa xác định **kiểu nháy khác nhau** để mã hóa trạng thái, tối thiểu:
  - Sáng đứng liên tục = tất cả ok.
  - các chế độ nahasy khác thì là bị lỗi 1 cảm biến nào đó.

### 4.13. Định danh & quản lý đa thiết bị (multi-pond)
- Mỗi thiết bị phải có `DEVICE_ID` duy nhất gắn với ao cụ thể (ví dụ `pond_01`, `pond_02`, `pond_03`), dùng làm tiền tố đường dẫn (path) lưu dữ liệu trên Firebase — **không hardcode ID giống nhau** giữa 3 bộ, tránh nhầm lẫn/ghi đè dữ liệu chéo ao.

---
### 4.14. Giữ lại các nhánh dữ liệu gửi lênh  app
-  config, latest, và update giữ lại các nhánh này  
## 5. Thiết kế chống mất kết nối WiFi/HTTP Firebase (bắt buộc áp dụng đúng theo state machine dưới đây)

Đây là yêu cầu trọng tâm: **hệ thống phải tự phục hồi kết nối mà không cần can thiệp thủ công**, đồng thời không được để việc quản lý WiFi làm block các task đo lường/relay.

### 5.1. Nguyên tắc bắt buộc
1. **Không dùng hàm blocking** kiểu `while(WiFi.status() != WL_CONNECTED) delay(500);` trong bất kỳ task nào ngoài giai đoạn khởi động lần đầu có timeout rõ ràng.
2. Quản lý WiFi bằng **event-driven** (`WiFi.onEvent()`), không polling trạng thái trong vòng lặp chính — phản ứng ngay khi có sự kiện `ARDUINO_EVENT_WIFI_STA_DISCONNECTED`.
3. Việc quản lý kết nối (WiFi + firebase) phải nằm trong **1 task riêng** (`netTask` hoặc gộp vào `cloudTask` theo Phase 3), tách biệt hoàn toàn khỏi `measureTask` — mất mạng không được làm chậm/dừng việc đọc cảm biến và điều khiển relay.

### 5.2. State machine kết nối (bắt buộc triển khai đúng các state sau)

```
INIT
 └─▶ WIFI_CONNECTING  ──(timeout 15s, thất bại)──▶ AP_FALLBACK
        │ (kết nối OK)
        ▼
      FIREBASE──(timeout, thất bại)──▶ WIFI_RECONNECT_BACKOFF
        │ (kết nối OK, cập nhật trạng thái "online" lên Firebase)
        ▼
     CONNECTED (hoạt động bình thường)
        │ (mất WiFi hoặc FB ping timeout)
        ▼
   WIFI_RECONNECT_BACKOFF ──▶ WIFI_CONNECTING (quay lại, backoff tăng dần)

AP_FALLBACK (bật AP để người dùng cấu hình WiFi qua app/web)
   └─▶ mỗi 5 phút tự thử lại WIFI_CONNECTING ở nền (không tắt AP ngay,
       cho phép người dùng vẫn cấu hình được trong lúc chờ)
```

### 5.3. Quy tắc chi tiết bắt buộc
- **Exponential backoff có giới hạn trần**: lần reconnect sau tăng dần 1s → 2s → 4s → 8s → 16s → tối đa 60s, **không** thử lại liên tục ngay lập tức (tránh spam router/AP, tránh WiFi driver ESP32 vào trạng thái lỗi do reconnect dồn dập).
- **Cơ chế phát hiện mất kết nối (Heartbeat/Presence)**: Vì không còn dùng MQTT LWT, khi gửi dữ liệu HTTP lên Firebase, thiết bị cần gửi heartbeat (cập nhật timestamp/trạng thái `online`) định kỳ. App/server sẽ dựa vào việc quá hạn heartbeat để tự động phát hiện thiết bị đang `offline` khi gặp sự cố (mất điện, rớt mạng).
- **Chu kỳ gửi dữ liệu/heartbeat HTTP** đặt hợp lý (đề xuất 30–60s) — quá ngắn gây tốn băng thông/tăng overhead HTTP trên mạng yếu, quá dài làm chậm phát hiện mất kết nối thật.
- Tắt WiFi power-save (`WiFi.setSleep(false)`) vì hệ thống dùng nguồn 12V liên tục, không cần tiết kiệm điện kiểu pin — ưu tiên độ ổn định/độ trễ phản hồi hơn tiết kiệm năng lượng.
- **WDT riêng cho netTask** (ngoài WDT hệ thống chính): một số lỗi driver WiFi ESP32 có thể khiến task treo mà không rơi vào vòng lặp lỗi thông thường — cần watchdog phụ theo dõi riêng tác vụ mạng.
- **Cân nhắc restart định kỳ có kiểm soát** (ví dụ mỗi 48–72 giờ, vào thời điểm hệ thống không đang xử lý cảnh báo) như một biện pháp phòng ngừa rò rỉ bộ nhớ/trạng thái WiFi stack tích lũy theo thời gian — đây là thực hành phổ biến cho thiết bị IoT chạy 24/7 dài hạn, không phải dấu hiệu thiết kế tệ.
- Ghi log RSSI (cường độ tín hiệu) định kỳ gửi kèm gói trạng thái lên server — giúp phát hiện sớm vị trí đặt thiết bị có sóng yếu để xử lý vật lý (thêm anten/repeater) trước khi mất kết nối hoàn toàn.
- AP fallback **không được vô hiệu hóa vĩnh viễn tính năng đo/relay cục bộ** — dù đang ở chế độ AP để chờ cấu hình, `measureTask` và `AlertManager` vẫn phải chạy bình thường (đúng nguyên tắc edge-first ở mục 1).

---

**FreeRTOS Tasks bắt buộc:**
| Task | Trách nhiệm | Ràng buộc |
|---|---|---|
| `measureTask` | Đọc 3 cảm biến (qua `busMutex`), debounce, đánh giá ngưỡng, điều khiển relay cục bộ | Không được gọi trực tiếp API mạng |
| `netTask` (hoặc `cloudTask`) | Quản lý state machine WiFi/HTTP Firebase, nhận dữ liệu từ `Queue`, gửi dữ liệu lên Firebase, xử lý lệnh từ app, backfill offline buffer | Không block quá lâu ảnh hưởng heartbeat WDT |
| `sysTask` | WDT feed, giám sát brownout, giám sát RSSI, log hệ thống | Ưu tiên cao, nhẹ, chạy chu kỳ ngắn |

---

## 7. Quy tắc bắt buộc cho AI Agent khi triển khai code

1. **Không hardcode ngưỡng cảnh báo, thời gian debounce, hay thông tin WiFi/Firebase** trực tiếp trong logic xử lý — luôn đọc từ `AppConfig`/`secrets.h`.
2. **Không viết bất kỳ đoạn code nào dùng `delay()` chặn trong `netTask` hoặc `measureTask`** để chờ mạng — vi phạm nguyên tắc edge-first & event-driven ở Mục 5.
3. **Không gộp trạng thái lỗi cảm biến với trạng thái vượt ngưỡng** trong bất kỳ hàm đánh giá cảnh báo nào (Mục 4.1).
4. **Mọi truy cập bus RS485 phải qua `busMutex`**, không có ngoại lệ, kể cả khi chỉ đọc 1 cảm biến.
5. **Không ghi flash/NVS mỗi chu kỳ đo** — chỉ ghi theo batch hoặc khi có thay đổi trạng thái quan trọng (Mục 4.3).
6. **Relay điều khiển quạt oxy mặc định phải ở trạng thái an toàn theo kiểu fail-safe (NC)** — không tự ý đổi sang thiết kế NO trừ khi có xác nhận từ người phụ trách phần cứng.
7. **Mọi lệnh nhận từ app qua HTTP/Firebase (polling hoặc SSE) phải xử lý idempotent**, kèm kiểm tra timestamp/mã lệnh để loại lệnh cũ.
8. **Mọi kết nối HTTP phải thông qua HTTPS (port 443)** trong cấu hình mặc định của môi trường thật để đảm bảo an toàn.
9. **DEVICE_ID phải duy nhất theo từng ao**, không được để giá trị mặc định giống nhau khi nhân bản code cho 3 bộ thiết bị.
10. Khi có mâu thuẫn giữa yêu cầu mới trong hội thoại và tài liệu này, **AI agent phải hỏi lại người phụ trách trước khi tự ý thay đổi hành vi đã quy định ở đây**, không tự suy diễn.


- [ ] OTA có cơ chế rollback tự động nếu firmware mới không "healthy" sau X phút.
- [ ] DEVICE_ID và đường dẫn Firebase là duy nhất, không đụng giữa 3 thiết bị.
- [ ] Không có đoạn code nào chặn (blocking) quá 1 chu kỳ đo trong `measureTask`.

---

*Tài liệu này nên được cập nhật song song với code — mọi thay đổi hành vi hệ thống phải phản ánh lại vào README này trước khi merge.*
