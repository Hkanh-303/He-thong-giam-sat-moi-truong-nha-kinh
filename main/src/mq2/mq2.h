#ifndef __MQ2_H__
#define __MQ2_H__

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

// Khởi tạo kênh ADC cho MQ-2
esp_err_t mq2_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel);

// Đọc giá trị Analog của MQ-2
int mq2_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);

#endif // __MQ2_H__