#ifndef DOAMDAT_H
#define DOAMDAT_H

// goi thu vien
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

//KHai báo hàm
esp_err_t doamdat_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel);
int doamdat_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);

#endif
