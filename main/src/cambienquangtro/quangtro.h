#ifndef QUANGTRO_H
#define QUANGTRO_H

// Gọi thư viện
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

//Khai bao hàm
esp_err_t quangtro_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel); // dùng esp_err_t thay vì void để kiếm tra kết quả trả về có lỗi hay không
int quangtro_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);

#endif