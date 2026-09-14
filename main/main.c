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
#include "cambienmua.h" 

// Tich hop thu vien BLE
#include "ble_manager.h" 

// Thu vien may bom
#include "maybom.h"

static adc_oneshot_unit_handle_t my_adc_handle = NULL;
static const char *TAG = "MAIN_APP";

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
    
    // 6. Khoi tao cam bien Mua
    esp_err_t init_mua = cambienmua_init(&my_adc_handle, CAMBIENMUA_CHANNEL);
    if (init_mua != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao cam bien mua that bai");
    } else {
        ESP_LOGI(TAG, "Khoi tao cam bien mua thanh cong");
    }

    // 7. Khoi tao may bom
    maybom_init(MAYBOM_PIN);
    ESP_LOGI(TAG, "Khoi tao may bom (GPIO %d) thanh cong", MAYBOM_PIN);

    // 8. Khoi tao he thong BLE
    ble_app_init();
    ESP_LOGI(TAG, "Khoi tao BLE thanh cong! Dang doi Raspberry Pi ket noi...");

    ESP_LOGI(TAG, "Bat dau doc gia tri toan bo cam bien...");

    // Vong lap doc cam bien dinh ky
    while(1){

        // p1: Doc cam bien quang tro
        int GIA_TRI_ANH_SANG = quangtro_read(my_adc_handle, QUANGTRO_CHANNEL);
        if(GIA_TRI_ANH_SANG != -1){
            ESP_LOGI(TAG, "Gia tri anh sang: %d", GIA_TRI_ANH_SANG);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien quang tro");
        }

        // p2: Doc cam bien do am dat va dieu khien bom
        int GIA_TRI_DO_AM = doamdat_read(my_adc_handle, DOAMDAT_CHANNEL);
        if(GIA_TRI_DO_AM != -1){
            ESP_LOGI(TAG, "Do am dat (Raw ADC): %d", GIA_TRI_DO_AM);
            
            // Logic bom nuoc tu dong
            if (GIA_TRI_DO_AM > NGUONG_DAT_KHO) {
                ESP_LOGW(TAG, "CANH BAO: Dat qua kho (%d > %d)! Dang bat may bom...", GIA_TRI_DO_AM, NGUONG_DAT_KHO);
                maybom_on(MAYBOM_PIN);
            } else {
                ESP_LOGI(TAG, "Dat du am, tat may bom.");
                maybom_off(MAYBOM_PIN);
            }
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien do am dat");
        }

        // p3: Doc cam bien muc nuoc
        int GIA_TRI_MUC_NUOC = mucnuoc_read(my_adc_handle, MUCNUOC_CHANNEL);
        if(GIA_TRI_MUC_NUOC != -1){
            ESP_LOGI(TAG, "Muc nuoc (Raw ADC): %d", GIA_TRI_MUC_NUOC);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien muc nuoc");
        }
        
        // p4: Doc cam bien MQ-2
        int GIA_TRI_KHI_GA = mq2_read(my_adc_handle, MQ2_CHANNEL);
        if(GIA_TRI_KHI_GA != -1){
            ESP_LOGI(TAG, "Nong do khi gas (Raw ADC): %d", GIA_TRI_KHI_GA);
            
            if (GIA_TRI_KHI_GA > NGUONG_CANH_BAO_3V3) {
                ESP_LOGW(TAG, "CANH BAO: Phat hien khi gas / khoi! (%d)", GIA_TRI_KHI_GA);
            }
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien MQ-2");
        }

        // p5: Doc cam bien DHT11
        float nhiet_do = 0.0f;
        float do_am = 0.0f;

        if(dht11_read(&nhiet_do, &do_am) == ESP_OK){
            ESP_LOGI(TAG, "Nhiet do: %.1f C | Do am: %.1f%%", nhiet_do, do_am);
        }else{
            ESP_LOGW(TAG, "Khong the doc du lieu tu DHT11");
        }

        // p6: Doc cam bien mua
        int GIA_TRI_MUA = cambienmua_read(my_adc_handle, CAMBIENMUA_CHANNEL);
        if(GIA_TRI_MUA != -1){
            ESP_LOGI(TAG, "Cam bien mua (Raw ADC): %d", GIA_TRI_MUA);
            
            // Logic canh bao mua dua theo nguong
            if (GIA_TRI_MUA < NGUONG_MUA) {
                ESP_LOGW(TAG, "CANH BAO: Troi dang mua! (%d)", GIA_TRI_MUA);
            }
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien mua");
        }
        
        // --- DONG GOI JSON VA GUI QUA BLE ---
        char ble_payload[256]; 
        snprintf(ble_payload, sizeof(ble_payload), 
                 "{\"nhietdo\":%.1f,\"doam\":%.1f,\"khigas\":%d,\"mua\":%d,\"anhsang\":%d,\"dat\":%d,\"nuoc\":%d}", 
                 nhiet_do, do_am, GIA_TRI_KHI_GA, GIA_TRI_MUA, GIA_TRI_ANH_SANG, GIA_TRI_DO_AM, GIA_TRI_MUC_NUOC);
        
        ble_update_sensor_data(ble_payload);
        ESP_LOGI(TAG, "Da day len BLE Mesh: %s", ble_payload);
        // ------------------------------------

        ESP_LOGI(TAG, "-----------------------------------");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}