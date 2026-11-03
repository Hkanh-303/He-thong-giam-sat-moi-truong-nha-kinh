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

## Gửi dữ liệu ESP32 sang Raspberry Pi qua Wi-Fi

ESP32 chạy chế độ Wi-Fi station, kết nối vào điểm phát Wi-Fi của Raspberry Pi rồi gửi HTTP POST dạng JSON đến cổng 5000.

1. Chép `main/src/raspberry_pi/receiver.py` lên Raspberry Pi và chạy:
   ```sh
   python3 receiver.py
   ```
   Mỗi mẫu nhận được sẽ hiện trên terminal. Server lắng nghe tại `/sensors` và có endpoint kiểm tra `/health`.
2. Lấy địa chỉ IP mà Pi dùng trên mạng Wi-Fi của nó bằng `hostname -I`.
3. Sửa `WIFI_SSID`, `WIFI_PASSWORD` và `RPI_SENSOR_URL` trong `main/src/wifi_sender/wifi_sender_config.h`. Đặt URL thành `http://<IP-cua-Pi>:5000/sensors`.
4. Build và nạp lại firmware. ESP32 sẽ thử kết nối lại nếu Wi-Fi bị ngắt; dữ liệu được gửi mỗi chu kỳ đọc cảm biến.

Các trường analog được gửi dưới dạng phần trăm thang ADC; độ ẩm đất là giá trị ước tính. Nhiệt độ, độ ẩm không khí và trạng thái bơm được gửi riêng trong JSON.
