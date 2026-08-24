#ifndef __DHT11_H__
#define __DHT11_H__

#include "driver/gpio.h"
#include "esp_err.h"

// Hàm khởi tạo chân cắm DHT11
void dht11_init(gpio_num_t pin);

// Hàm đọc dữ liệu. Trả về ESP_OK nếu thành công
esp_err_t dht11_read(float *temperature, float *humidity);

#endif // __DHT11_H__