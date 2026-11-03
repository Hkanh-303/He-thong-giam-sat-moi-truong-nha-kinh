#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Goi file cau hinh de nhan dien cac chan GPIO (Rat quan trong)
#include "config.h"

// Thu vien cac cam bien
#include "quangtro.h"
#include "doamdat.h" 
#include "mucnuoc.h" 
#include "dht11.h" 
#include "mq2.h" 

// Thu vien may bom
#include "maybom.h"
#include "wifi_sender.h"

static adc_oneshot_unit_handle_t my_adc_handle = NULL;
static const char *TAG = "MAIN_APP";

// Chuan hoa gia tri ADC 12-bit thanh phan tram cua thang do 0-4095.
static int adc_to_percent(int adc_value) {
    if (adc_value < 0) return 0;
    if (adc_value > 4095) return 100;
    return (adc_value * 100 + 2047) / 4095;
}

static void format_json_int(char *buffer, size_t buffer_size, int value) {
    if (value < 0) {
        snprintf(buffer, buffer_size, "null");
    } else {
        snprintf(buffer, buffer_size, "%d", value);
    }
}

void app_main(void){
    ESP_LOGI(TAG, "Khoi dong he thong SmartPlan");

    // 1. Khoi tao cam bien quang tro
    esp_err_t init_result = quangtro_init(&my_adc_handle, QUANGTRO_CHANNEL);
    if (init_result != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao quang tro that bai");
        return;
    }
    ESP_LOGI(TAG, "Khoi tao quang tro thanh cong");

    // 2. Khoi tao cam bien do am dat
    esp_err_t init_doam = doamdat_init(&my_adc_handle, DOAMDAT_CHANNEL);
    if (init_doam != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao do am dat that bai");
        return;
    }
    ESP_LOGI(TAG, "Khoi tao do am dat thanh cong");

    // 3. Khoi tao cam bien muc nuoc
    esp_err_t init_mucnuoc = mucnuoc_init(&my_adc_handle, MUCNUOC_CHANNEL);
    if (init_mucnuoc != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao muc nuoc that bai");
        return;
    }
    ESP_LOGI(TAG, "Khoi tao muc nuoc thanh cong");
    
    // 4. Khoi tao cam bien MQ-2
    esp_err_t init_mq2 = mq2_init(&my_adc_handle, MQ2_CHANNEL);
    if (init_mq2 != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao MQ-2 that bai");
    } else {
        ESP_LOGI(TAG, "Khoi tao MQ-2 thanh cong");
    }

    // 5. Khoi tao cam bien DHT11
     dht11_init(DHT11_PIN);
    ESP_LOGI(TAG, "Khoi tao DHT11 thanh cong");
    
    // 6. Khoi tao may bom
    maybom_init(MAYBOM_PIN);
    ESP_LOGI(TAG, "Khoi tao may bom (GPIO %d) thanh cong", MAYBOM_PIN);

    if (wifi_sender_init() != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao Wi-Fi that bai; van tiep tuc doc cam bien");
    }

    ESP_LOGI(TAG, "Bat dau doc gia tri toan bo cam bien...");

    // Vong lap doc cam bien dinh ky
    int pump_is_on = 0;
    while(1){
        int light_percent = -1;
        int soil_moisture_percent = -1;
        int water_level_percent = -1;
        int mq2_signal_percent = -1;

        // p1: Doc cam bien quang tro
        int GIA_TRI_ANH_SANG = quangtro_read(my_adc_handle, QUANGTRO_CHANNEL);
        if(GIA_TRI_ANH_SANG != -1){
            light_percent = adc_to_percent(GIA_TRI_ANH_SANG);
            ESP_LOGI(TAG, "Tin hieu quang tro: %d%% thang ADC", light_percent);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien quang tro");
        }

        // p2: Doc cam bien do am dat va dieu khien bom
        int GIA_TRI_DO_AM = doamdat_read(my_adc_handle, DOAMDAT_CHANNEL);
        if(GIA_TRI_DO_AM != -1){
            // Cam bien hien tai cho gia tri ADC cao khi dat kho, nen dao chieu phan tram.
            soil_moisture_percent = 100 - adc_to_percent(GIA_TRI_DO_AM);
            ESP_LOGI(TAG, "Do am dat (uoc tinh): %d%%", soil_moisture_percent);
            
            // Logic bom nuoc tu dong
            if (GIA_TRI_DO_AM > NGUONG_DAT_KHO) {
                ESP_LOGW(TAG, "CANH BAO: Dat qua kho (%d%%)! Dang bat may bom...", soil_moisture_percent);
                maybom_on(MAYBOM_PIN);
                pump_is_on = 1;
            } else {
                ESP_LOGI(TAG, "Dat du am, tat may bom.");
                maybom_off(MAYBOM_PIN);
                pump_is_on = 0;
            }
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien do am dat");
        }

        // p3: Doc cam bien muc nuoc
        int GIA_TRI_MUC_NUOC = mucnuoc_read(my_adc_handle, MUCNUOC_CHANNEL);
        if(GIA_TRI_MUC_NUOC != -1){
            water_level_percent = adc_to_percent(GIA_TRI_MUC_NUOC);
            ESP_LOGI(TAG, "Muc nuoc: %d%% thang ADC", water_level_percent);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien muc nuoc");
        }
        
        // p4: Doc cam bien MQ-2
        int GIA_TRI_KHI_GA = mq2_read(my_adc_handle, MQ2_CHANNEL);
        if(GIA_TRI_KHI_GA != -1){
            mq2_signal_percent = adc_to_percent(GIA_TRI_KHI_GA);
            ESP_LOGI(TAG, "Nong do khi gas: %d%% thang ADC", mq2_signal_percent);
            
            if (GIA_TRI_KHI_GA > NGUONG_CANH_BAO_3V3) {
                ESP_LOGW(TAG, "CANH BAO: Phat hien khi gas / khoi! (%d%% thang ADC)", mq2_signal_percent);
            }
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien MQ-2");
        }

        // p5: Doc cam bien DHT11
        float nhiet_do = 0.0f;
        float do_am = 0.0f;
        int dht11_valid = 0;

        if(dht11_read(&nhiet_do, &do_am) == ESP_OK){
            dht11_valid = 1;
            ESP_LOGI(TAG, "Nhiet do: %.1f C | Do am: %.1f%%", nhiet_do, do_am);
        }else{
            ESP_LOGW(TAG, "Khong the doc du lieu tu DHT11");
        }

        char light_json[12];
        char soil_json[12];
        char water_json[12];
        char mq2_json[12];
        char temperature_json[20];
        char humidity_json[20];
        char json_payload[256];
        format_json_int(light_json, sizeof(light_json), light_percent);
        format_json_int(soil_json, sizeof(soil_json), soil_moisture_percent);
        format_json_int(water_json, sizeof(water_json), water_level_percent);
        format_json_int(mq2_json, sizeof(mq2_json), mq2_signal_percent);
        if (dht11_valid) {
            snprintf(temperature_json, sizeof(temperature_json), "%.1f", nhiet_do);
            snprintf(humidity_json, sizeof(humidity_json), "%.1f", do_am);
        } else {
            snprintf(temperature_json, sizeof(temperature_json), "null");
            snprintf(humidity_json, sizeof(humidity_json), "null");
        }

        snprintf(json_payload, sizeof(json_payload),
                 "{\"light_adc_percent\":%s,\"soil_moisture_percent_estimated\":%s,"
                 "\"water_level_adc_percent\":%s,\"mq2_signal_adc_percent\":%s,"
                 "\"temperature_c\":%s,\"humidity_percent\":%s,\"pump_on\":%s}",
                 light_json, soil_json, water_json, mq2_json,
                 temperature_json, humidity_json, pump_is_on ? "true" : "false");

        ESP_LOGI(TAG, "JSON POST /sensors: %s", json_payload);
        esp_err_t send_result = wifi_sender_post_json(json_payload);
        if (send_result != ESP_OK && send_result != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Khong gui duoc mau du lieu len Raspberry Pi");
        }

        ESP_LOGI(TAG, "-----------------------------------");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
