#include "cambienmua.h"
#include "esp_log.h"

static const char *TAG = "CAMBIENMUA";

esp_err_t cambienmua_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel){
    esp_err_t err = ESP_OK;

    // Chỉ khởi tạo UNIT 1 nếu nó chưa được khởi tạo bởi các cảm biến trước đó
    if (*adc_handle == NULL){
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        err = adc_oneshot_new_unit(&init_config, adc_handle);
        if (err != ESP_OK) return err;
    }
    
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    err = adc_oneshot_config_channel(*adc_handle, channel, &config);
    return err;
}

int cambienmua_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel) {
    int adc_raw = 0;
    esp_err_t err = adc_oneshot_read(adc_handle, channel, &adc_raw);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi doc ADC cam bien mua");
        return -1;
    }
    return adc_raw;
}