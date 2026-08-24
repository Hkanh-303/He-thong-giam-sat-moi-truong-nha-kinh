#include "mucnuoc.h"
#include "esp_log.h"

static const char *TAG = "MUCNUOC_CAMBIEN";

esp_err_t mucnuoc_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel){
    esp_err_t err =  ESP_OK;

    // Chỉ bật nguồn adc unit 1 lần nếu nó chưa tồn tại
    if (*adc_handle == NULL){
        ESP_LOGI(TAG, "Chua co ADC Unit, bat dau khoi tao ADC Unit 1..");
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        err = adc_oneshot_new_unit(&init_config, adc_handle);

        if (err != ESP_OK){
            ESP_LOGE(TAG, "Khoi tao ADC Unit that bai");
            return err;
        }
    }
    
    // Cấu hình chi tiết cho channel của mực nước
    ESP_LOGI(TAG, "Cau hinh ADC Channel %d voi suy hao 12dB..", channel);
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    err = adc_oneshot_config_channel(*adc_handle, channel, &config);

    if (err != ESP_OK){
        ESP_LOGE(TAG, "Cau hinh channel %d that bai", channel);
    }
    return err;
}

int mucnuoc_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel) {
    int adc_raw = 0;
    esp_err_t err = adc_oneshot_read(adc_handle, channel, &adc_raw);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Doc gia tri ADC that bai tren channel %d", channel);
        return -1;
    }
    return adc_raw;
}