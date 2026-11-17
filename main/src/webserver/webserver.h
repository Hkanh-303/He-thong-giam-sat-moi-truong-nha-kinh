#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEBSERVER_MAX_LOGS 50

typedef enum {
    WEBSERVER_PUMP_AUTO = 0,
    WEBSERVER_PUMP_MANUAL_ON,
    WEBSERVER_PUMP_MANUAL_OFF
} webserver_pump_mode_t;

typedef struct {
    char time_str[24];
    int light_percent;
    int soil_moisture_percent;
    int soil_adc_raw;
    bool soil_calibrated;
    int water_level_percent;
    int mq2_signal_percent;
    float temperature_c;
    float humidity_percent;
    bool dht11_valid;
    bool pump_is_on;
} webserver_log_entry_t;

/**
 * @brief Khởi tạo webserver và đăng ký nhận sự kiện kết nối Wi-Fi.
 */
esp_err_t webserver_init(void);

/**
 * @brief Bắt đầu HTTP server nếu chưa chạy.
 */
esp_err_t webserver_start(void);

/**
 * @brief Dừng HTTP server.
 */
void webserver_stop(void);

/**
 * @brief Kiểm tra HTTP server có đang chạy không.
 */
bool webserver_is_running(void);

/**
 * @brief Ghi nhận một bản tin cảm biến vào snapshot và danh sách log lịch sử.
 */
void webserver_record_sensor_data(int light_percent,
                                  int soil_moisture_percent,
                                  int soil_adc_raw,
                                  bool soil_calibrated,
                                  int water_level_percent,
                                  int mq2_signal_percent,
                                  float temperature_c,
                                  float humidity_percent,
                                  bool dht11_valid,
                                  bool pump_is_on);

/**
 * @brief Xóa toàn bộ lịch sử log cảm biến.
 */
void webserver_clear_logs(void);

/**
 * @brief Lấy chế độ điều khiển máy bơm hiện tại từ Webserver (Auto / Bật thủ công / Tắt thủ công).
 */
webserver_pump_mode_t webserver_get_pump_mode(void);

/**
 * @brief Thiết lập chế độ điều khiển máy bơm từ Webserver.
 */
void webserver_set_pump_mode(webserver_pump_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
