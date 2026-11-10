1. Kết nối esp với cảm biến quang trở
   Vcc  ->  3v3 của esp32 hoặc nguồn 3.3v
   Gnd  -> Gnd
   A0   -> GPIO 32 (ADC1_CHANNEL_4)
   D0 không dùng  

2. Kết nối esp với dht11
   +    ->  3.3V hoặc 5V
   Data ->  GPIO 4 
   -    ->  GND

3. Kết nối esp với cảm biến độ ẩm đất
    Vcc  ->  3v3 của esp32 hoặc nguồn 3.3v
   Gnd  -> Gnd
   A0   -> GPIO 33 (ADC1_CHANNEL_5)
   D0 không dùng

   4. Kết nối esp32 với cảm biến mực sâu nước
   Vcc  ->  3v3 của esp32 hoặc nguồn 3.3v
   Gnd  -> Gnd
   A0   -> GPIO 34 (ADC1_CHANNEL_6)

   5. Kết nối esp32 với cảm biến khí gas mq2
   Vcc  ->  5v 
   Gnd  -> Gnd
   A0   -> GPIO 35 (ADC1_CHANNEL_7)

   6. Kết nối esp32 với cảm biến mực nước
   Vcc  ->  3.3v 
   Gnd  -> Gnd
   A0   -> GPIO 34 (ADC1_CHANNEL_6)

   7. Kết nối esp32 với cảm biến mưa
   Vcc  ->  3.3v 
   Gnd  -> Gnd
   A0   -> VN (ADC1_CHANNEL_3)

8. máy bơm
  vcc -> 5v
  gnd
  in1 -> gpio18

## Build và nạp firmware

```bash
idf.py build
idf.py -p PORT flash monitor
```

Thay `PORT` bằng cổng nối với ESP32.

## Điều khiển máy bơm qua ứng dụng

Ứng dụng ghi cả hai trường vào Firebase Realtime Database tại `node1/pump_control`:

```json
{ "mode": "manual", "manual_on": true }
```

- `mode`: `"manual"` để điều khiển thủ công, `"auto"` để điều khiển theo độ ẩm đất.
- `manual_on`: `true` để bật, `false` để tắt trong chế độ thủ công. Luôn gửi trường này, kể cả ở chế độ tự động.
- ESP32 đọc lệnh khoảng mỗi 3 giây; thời gian phản hồi còn phụ thuộc mạng và chu kỳ đọc cảm biến. Trạng thái bơm thực tế được gửi trong `node1/sensor/pump_on`, theo chu kỳ cập nhật cảm biến hiện có (15 giây).
- Nếu chưa có `pump_control`, firmware dùng chế độ tự động. Khi mất kết nối hoặc dữ liệu lệnh không hợp lệ, chế độ tự động tiếp tục hoạt động; nếu đang ở chế độ thủ công thì bơm tắt sau 30 giây không đọc được lệnh hợp lệ. Khi kết nối lại, lệnh thủ công được áp dụng trở lại.
- Chế độ tự động bật bơm khi ADC độ ẩm đất lớn hơn `NGUONG_DAT_KHO` trong `main/config.h`, và tắt khi không vượt ngưỡng hoặc khi đọc cảm biến thất bại. Cần hiệu chỉnh ngưỡng theo cảm biến thực tế.

Kho mã này chưa có mã ứng dụng; app cần cung cấp nút chọn chế độ và bật/tắt, rồi ghi dữ liệu theo định dạng trên. Quy tắc Firebase phải cho phép app ghi và ESP32 đọc đường dẫn điều khiển.
