# Kiểm tra riêng DHT11

Project ESP-IDF này dùng cùng chân trong `main/config.h` của ứng dụng: DHT11 tại GPIO17 (D17) và relay tại GPIO21. Nó đọc luân phiên bằng bản lưu driver cũ trong `main/dht11_legacy.c` của project chẩn đoán (`CUSTOM`) và `esp-idf-lib/dht` phiên bản 1.2.0 (`REFERENCE`). Ứng dụng chính hiện cũng dùng `esp-idf-lib/dht`. Hai lần đọc cách nhau ít nhất 3 giây; GPIO được cấu hình lại giữa hai driver.

Firmware chẩn đoán giữ relay ở mức tắt theo cấu hình kích LOW của dự án. Nó không khởi tạo ADC, Wi-Fi, NVS hoặc Firebase. Khi nạp, nó thay firmware ứng dụng đang chạy trên board; bản ứng dụng vẫn build được từ thư mục gốc.

## Đấu dây và chạy

Giữ kết nối DHT11: `+ → 3V3`, `OUT → GPIO17/D17`, `− → GND`.

Chạy từ thư mục gốc của kho mã, trong terminal ESP-IDF:

```bash
idf.py -C diagnostics/dht11 build
idf.py -C diagnostics/dht11 -p /dev/ttyUSB0 flash monitor
```

Nếu monitor đang dùng cổng serial, thoát monitor bằng `Ctrl+]` trước khi nạp. Thay `/dev/ttyUSB0` nếu cổng thực tế khác.

## Đọc kết quả

- `CUSTOM OK` và `REFERENCE OK`: cả hai driver đọc được trong firmware tối giản; cần tiếp tục kiểm tra ảnh hưởng từ ứng dụng đầy đủ.
- `CUSTOM FAIL`, `REFERENCE OK`: thư viện hiện dùng trong ứng dụng đọc được, còn driver cũ thất bại trong điều kiện đối chiếu này.
- Cả hai `FAIL`: vẫn cần xem lỗi cụ thể; kết quả này chưa xác định cảm biến hỏng.

Nếu driver cũ thấy HIGH khi đang phát START, nó in `START_SNAPSHOT` một lần mỗi phiên. Các giá trị `latch`, `oe`, `pad`, `od`, `matrix` được chụp **trước khi nhả DATA**. Phần `IO DUMP` được in sau khi đã nhả để tránh giữ LOW trong lúc ghi log.

Ví dụ `latch=0 oe=1 pad=1` nghĩa là thanh ghi đầu ra yêu cầu LOW, đầu ra được bật nhưng thanh ghi đầu vào vẫn đọc HIGH. Cần đối chiếu thêm định tuyến GPIO trong `matrix` và `IO DUMP` để tìm nguyên nhân.

Driver tham chiếu dùng output-only open-drain khi phát START rồi chuyển sang input để nhận dữ liệu. Nó không có phép tự đọc mức START; không được suy luận mức điện áp vật lý từ việc đọc GPIO khi input đang tắt. Chỉ dòng `REFERENCE OK` mới xác nhận nó đã nhận khung dữ liệu qua kiểm tra checksum.

Gửi log từ lúc khởi động đến hết hai lượt `CUSTOM` và `REFERENCE` để đối chiếu.

## Trở lại ứng dụng đầy đủ

Thoát monitor bằng `Ctrl+]`, rồi chạy từ thư mục gốc:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Thư viện tham chiếu

- [esp-idf-lib/dht 1.2.0](https://components.espressif.com/components/esp-idf-lib/dht/versions/1.2.0/readme), giấy phép BSD-3-Clause.
- [esp-idf-lib/esp_idf_lib_helpers 1.4.0](https://components.espressif.com/components/esp-idf-lib/esp_idf_lib_helpers/versions/1.4.0/readme), giấy phép ISC.

Các dependency được khai báo trong `main/idf_component.yml`; Component Manager tải chúng vào `managed_components` khi build.
