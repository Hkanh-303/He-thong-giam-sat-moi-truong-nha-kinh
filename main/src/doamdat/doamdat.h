#ifndef DOAMDAT_H
#define DOAMDAT_H

#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

// Số ADC tối thiểu giữa hai mốc để phép nội suy đủ phân giải.
#ifndef DOAMDAT_CALIBRATION_MIN_SPAN
#define DOAMDAT_CALIBRATION_MIN_SPAN 100
#endif

esp_err_t doamdat_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel);
// Trả về trung vị 16 mẫu ADC; -1 nếu đọc thất bại.
int doamdat_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);
// Mức ẩm tương đối 0..100 theo hai mốc đo; -1 nếu chưa hiệu chỉnh hoặc raw lỗi.
int doamdat_percent(int raw);
bool doamdat_is_calibrated(void);
// Lấy các mốc hiệu chuẩn hiện tại
void doamdat_get_calibration(int *dry, int *wet, bool *is_calibrated);
// Hiệu chuẩn mốc khô (0%) và mốc ướt (100%)
esp_err_t doamdat_calibrate_dry(void);
esp_err_t doamdat_calibrate_wet(void);
esp_err_t doamdat_reset_calibration(void);
// Thiết lập mốc hiệu chuẩn mặc định (Khô ~3200, Ướt ~1400)
esp_err_t doamdat_set_default_calibration(void);
// Đăng ký lệnh soil dry/wet/status/reset sau khi console đã khởi tạo.
esp_err_t doamdat_register_console_commands(void);

#endif
