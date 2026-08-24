#ifndef __MUCNUOC_H__
#define __MUCNUOC_H__

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

esp_err_t mucnuoc_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel);
int mucnuoc_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);

#endif // __MUCNUOC_H__