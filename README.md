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
