#ifndef __CAMBIENMUA_H__
#define __CAMBIENMUA_H__

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

esp_err_t cambienmua_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel);
int cambienmua_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel);

#endif // __CAMBIENMUA_H__