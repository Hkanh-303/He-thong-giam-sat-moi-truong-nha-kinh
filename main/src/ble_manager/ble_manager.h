#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include "esp_err.h"

// Hàm khởi tạo Bluetooth Mesh
void ble_app_init(void);

// Hàm gửi gói tin chứa chuỗi JSON cảm biến qua Mesh Model
void ble_update_sensor_data(const char *data_str);

#endif