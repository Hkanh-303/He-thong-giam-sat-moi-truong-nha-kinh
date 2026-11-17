## Sơ đồ đấu dây (ESP32)

| Thiết bị | Nguồn | GND | Tín hiệu | Chân ESP32 | Trạng thái trong firmware |
|---|---|---|---|---|---|
| Cảm biến quang trở | 3V3 | GND | A0 | GPIO32 / ADC1_CH4 | Đang đọc |
| DHT11 | Ưu tiên 3V3 | GND | DATA | GPIO17 / D17 | Đang đọc |
| Cảm biến độ ẩm đất | 3V3 | GND | A0 | GPIO33 / ADC1_CH5 | Đang đọc |
| Cảm biến mực nước | 3V3 | GND | A0 | GPIO34 / ADC1_CH6 | Đang đọc một cảm biến |
| MQ-2 | 5V | GND | A0 | GPIO35 / ADC1_CH7 | Đang đọc |
| Cảm biến mưa | 3V3 | GND | A0 | GPIO39 (VN) / ADC1_CH3 | Đã khai báo chân, firmware chưa đọc |
| Module relay bơm | 5V theo module | GND chung với ESP32 | IN1 | D21 / GPIO21 | Đang điều khiển, relay kích mức cao (1 bật, 0 tắt) |

### Lưu ý đấu dây

- Các kênh analog dùng ADC1 để có thể hoạt động cùng Wi-Fi. GPIO34, GPIO35 và GPIO39 chỉ dùng làm ngõ vào; không dùng các chân này để xuất tín hiệu điều khiển.
- README cũ liệt kê hai cảm biến mực nước cùng ở GPIO34. Firmware hiện chỉ hỗ trợ một đầu ra analog mực nước tại chân này. Không nối hai đầu ra cảm biến riêng vào chung GPIO34; nếu cần cả hai, phải chọn chân ADC1 khác và bổ sung đọc cảm biến trong source.
- DHT11 dạng linh kiện rời cần điện trở kéo lên DATA khoảng 4.7–10 kΩ tới 3V3; nhiều module đã có sẵn điện trở. Nếu cấp module bằng 5V, bảo đảm DATA không bị kéo lên 5V (dùng pull-up 3V3 hoặc chuyển mức).
- Khi MQ-2 cấp nguồn 5V, kiểm tra điện áp A0 trước khi nối vào ESP32. Nếu A0 có thể vượt 3.3V, dùng cầu chia áp để hạ áp; suy hao ADC không bảo vệ chân khỏi quá áp.
- Không cấp nguồn cho máy bơm trực tiếp từ GPIO. Dùng relay hoặc mạch công suất phù hợp và nguồn riêng đủ dòng cho bơm; nối GND chung giữa ESP32 và module relay để tín hiệu IN1 có cùng mức tham chiếu.

### Đọc độ ẩm đất

Đầu ra analog `A0/AO` của module độ ẩm đất nối GPIO33 (ADC1_CH5); đầu ra số `D0/DO` không dùng cho phép đo này. Firmware lọc trung vị 16 mẫu ADC, rồi tính mức ẩm tương đối từ hai mốc thực tế. Không tự gán số ADC 941 thành 77% và không dùng mốc khô/ướt phỏng đoán.

Sau khi nạp ứng dụng chính, nhập các lệnh vào serial monitor tại prompt `greenhouse>`:

1. Đặt đầu dò trong đất khô, giữ nguyên nguồn và AO, chờ số `raw` ổn định ít nhất 6 giây. Nhập `soil dry`, nhấn Enter để lưu mốc 0%.
2. Đặt đầu dò trong đất cùng loại đã tưới ướt và để ráo, chờ ít nhất 6 giây. Nhập `soil wet`, nhấn Enter để lưu mốc 100%.
3. Nhập `soil status` để xem hai mốc đã lưu. `soil reset` xóa hiệu chỉnh nếu thay cảm biến hoặc loại đất.

Hai mốc cần chênh ít nhất 100 mức ADC để tránh chia theo tín hiệu quá nhỏ. Firmware hỗ trợ cả cảm biến có ADC khô cao hơn ướt và chiều ngược lại. Công thức nội suy là `100 × (raw_kho − raw) / (raw_kho − raw_uot)`, giới hạn 0–100%; các mốc được lưu trong NVS và giữ qua lần khởi động/nạp firmware thông thường.

Khi chưa đủ hai mốc, chỉ báo `raw` và `chua hieu chinh`, chưa công bố % độ ẩm đất. Mức 0–100% là mức tương đối theo hai mốc của cảm biến/đất này; để đo độ ẩm thể tích cần hiệu chuẩn riêng theo phương pháp và thiết bị phù hợp.

Khi rút dây analog, chân ADC có thể thả nổi và vẫn trả về một số hợp lệ; riêng ADC không xác nhận cảm biến đang kết nối. Firmware chưa có phần cứng phát hiện rút dây.

### Đọc DHT11 và trạng thái dữ liệu

Ứng dụng chính dùng `esp-idf-lib/dht` 1.2.0 qua wrapper `main/src/dht11/dht11.c`, DATA tại GPIO17 (D17). Driver phát START open-drain, chuyển sang input và kiểm tra checksum; các lần đọc cách nhau ít nhất 2 giây. Source driver được Component Manager tải theo phiên bản đã khóa trong `main/idf_component.yml` và `dependencies.lock`.

Firebase luôn gửi `dht11_valid`, `soil_calibrated` và `soil_adc`. Khi DHT11 đọc lỗi, các trường hiện tại `temperature`/`humidity` được xóa bằng PATCH null và `dht11_valid=false`. Khi đất chưa hiệu chỉnh hoặc đọc lỗi, trường `soil_moisture` hiện tại được xóa; số ADC hiện tại và trạng thái hiệu chỉnh vẫn được công bố. App cần dùng các trạng thái này để hiện “chưa sẵn sàng”, thay vì coi số cũ/thiếu trường là phép đo mới. Kho mã này chưa có source app để sửa cách hiển thị.

## Build và nạp firmware

```bash
idf.py build
idf.py -p PORT flash monitor
```

Thay `PORT` bằng cổng nối với ESP32.

## Kiểm tra riêng DHT11

Bản [chẩn đoán DHT11](diagnostics/dht11/README.md) đọc luân phiên bằng bản lưu driver cũ và thư viện `esp-idf-lib` hiện dùng trong ứng dụng, cùng GPIO17 (D17). Bản này giữ relay tắt và chỉ chạy phần đọc DHT11 để đối chiếu lỗi phần mềm.

```bash
idf.py -C diagnostics/dht11 build
idf.py -C diagnostics/dht11 -p PORT flash monitor
```

## Chỉ mục lịch sử Firebase

Truy vấn lịch sử cần chỉ mục `timestamp` tại `node1/history`. Trong Firebase Realtime Database Rules, gộp chỉ mục này vào quy tắc hiện có và giữ nguyên các quy tắc, chỉ mục khác:

```json
{
  "rules": {
    "node1": {
      "history": {
        ".indexOn": ["timestamp"]
      }
    }
  }
}
```

Nếu chưa có chỉ mục này, truy vấn lịch sử có thể trả về HTTP 400. Xem [hướng dẫn tạo chỉ mục của Firebase](https://firebase.google.com/docs/database/security/indexing-data).

## Webserver xem log cảm biến trực tiếp

Firmware tích hợp một HTTP Webserver (cổng 80). Khi ESP32 kết nối Wi-Fi thành công, IP của thiết bị được hiển thị trong log terminal:
```text
==========================================================
  WEBSERVER LOG CAM BIEN SAN SANG: http://192.168.x.x
==========================================================
```

Mở trình duyệt bất kỳ cùng mạng Wi-Fi và truy cập `http://<IP_ESP32>`:
- **Giao diện Dashboard trực quan:** Xem giá trị tức thời của Nhiệt độ (DHT11), Độ ẩm không khí, Độ ẩm đất (% & raw ADC), Mực nước, Quang trở, Nồng độ khí gas MQ-2 và Trạng thái máy bơm (Bật/Tắt).
- **Bảng Log Lịch Sử:** Lưu trữ 50 bản ghi đo gần nhất kèm mốc thời gian / thời gian hoạt động (uptime).
- **Cơ chế cập nhật tự động:** Trang tự động gọi API `/api/data` mỗi 3 giây mà không cần reload lại trang web. Có nút "Làm mới" và "Xóa log".
- **Điều khiển máy bơm trực tiếp:** Thẻ máy bơm trên Dashboard tích hợp 3 nút điều khiển:
  - `⚡ Bật`: Kích hoạt relay bật máy bơm ngay lập tức (bỏ qua điều kiện đất).
  - `⛔ Tắt`: Ngắt relay tắt máy bơm.
  - `🔄 Auto`: Trả về chế độ tự động điều khiển theo độ ẩm đất (< 30% bật, >= 45% tắt) hoặc lệnh từ Firebase.
- **REST API:**
  - `GET /api/data`: Trả về dữ liệu JSON của chu kỳ hiện tại và danh sách các log đo gần nhất.
  - `POST /api/pump?action=on|off|auto` (hoặc `GET`): Điều khiển bật/tắt hoặc đặt chế độ tự động cho máy bơm.
  - `POST /api/clear`: Xóa toàn bộ lịch sử log trên RAM.

## Điều khiển máy bơm qua ứng dụng

Ứng dụng ghi ít nhất hai trường vào Firebase Realtime Database tại `node1/sensor/pump`:

```json
{ "mode": "manual", "command": "on" }
```

- `mode`: `"manual"` để điều khiển thủ công, `"auto"` để điều khiển theo độ ẩm đất.
- `command`: `"on"` để bật, `"off"` để tắt trong chế độ thủ công. Luôn gửi trường này, kể cả ở chế độ tự động.
- Các trường khác như `enabled`, `manual_override` và thông tin cập nhật có thể được lưu cùng đối tượng, nhưng firmware hiện chưa dùng chúng để điều khiển bơm.
- ESP32 đọc lệnh khoảng mỗi 3 giây; thời gian phản hồi còn phụ thuộc mạng và chu kỳ đọc cảm biến. Trạng thái bơm thực tế được gửi trong `node1/sensor/pump_on`, theo chu kỳ cập nhật cảm biến hiện có (15 giây).
- Nếu chưa có `pump`, firmware dùng chế độ tự động. Khi mất kết nối hoặc dữ liệu lệnh không hợp lệ, chế độ tự động tiếp tục hoạt động; nếu đang ở chế độ thủ công thì bơm tắt sau 30 giây không đọc được lệnh hợp lệ. Khi kết nối lại, lệnh thủ công được áp dụng trở lại.
- Chế độ tự động chỉ hoạt động sau khi hiệu chỉnh đất: bật dưới 30%, tắt từ 45%, giữ trạng thái trong khoảng giữa hai ngưỡng để tránh relay đóng/ngắt liên tục. Chỉnh `DOAMDAT_BAT_BOM_PERCENT` và `DOAMDAT_TAT_BOM_PERCENT` trong `main/config.h` theo cây/đất thực tế. Khi chưa hiệu chỉnh hoặc đọc ADC thất bại, bơm tự động tắt. Điều khiển thủ công vẫn theo lệnh app và thời hạn 30 giây.

Kho mã này chưa có mã ứng dụng; app cần cung cấp nút chọn chế độ và bật/tắt, rồi ghi dữ liệu theo định dạng trên. Quy tắc Firebase phải cho phép app ghi và ESP32 đọc đường dẫn điều khiển.
