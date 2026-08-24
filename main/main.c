#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "quangtro.h"
#include "doamdat.h" 
#include "mucnuoc.h" 
#include "dht11.h" 
#include "mq2.h" // Thêm thư viện MQ-2

#define QUANGTRO_CHANNEL       ADC_CHANNEL_4 // Tương ứng GPIO 32
#define DOAMDAT_CHANNEL        ADC_CHANNEL_5 // Tương ứng GPIO 33
#define MUCNUOC_CHANNEL        ADC_CHANNEL_6 // Tương ứng GPIO 34 
#define MQ2_CHANNEL            ADC_CHANNEL_7 // Tương ứng GPIO 35 (Chân cho MQ-2)
#define DHT11_PIN              GPIO_NUM_4    // Chân tín hiệu cho DHT11

// Ngưỡng cảnh báo khí gas (Tạm thời để 1000 để test với nguồn 3.3V)
#define NGUONG_CANH_BAO_3V3  1000  

static adc_oneshot_unit_handle_t my_adc_handle = NULL;
static const char *TAG = "MAIN_APP";

void app_main(void){
    ESP_LOGI(TAG, "Khoi dong he thong SmartPlan");

    // 1. Khởi tạo cảm biến quang trở
    esp_err_t init_result = quangtro_init(&my_adc_handle, QUANGTRO_CHANNEL);
    if (init_result != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao quang tro that bai");
        return;
    }
    ESP_LOGI(TAG, "Khoi tao quang tro thanh cong");

    // 2. Khởi tạo cảm biến độ ẩm đất
    esp_err_t init_doam = doamdat_init(&my_adc_handle, DOAMDAT_CHANNEL);
    if (init_doam != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao do am dat that bai");
        return;
    }
    ESP_LOGI(TAG, "Khoi tao do am dat thanh cong");

    // 3. Khởi tạo cảm biến mực nước
    esp_err_t init_mucnuoc = mucnuoc_init(&my_adc_handle, MUCNUOC_CHANNEL);
    if (init_mucnuoc != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao muc nuoc that bai");
        return;
    }
    ESP_LOGI(TAG, "Khoi tao muc nuoc thanh cong");
    
    // 4. Khởi tạo cảm biến MQ-2
    esp_err_t init_mq2 = mq2_init(&my_adc_handle, MQ2_CHANNEL);
    if (init_mq2 != ESP_OK){
        ESP_LOGE(TAG, "Khoi tao MQ-2 that bai");
        // Không return ở đây để các cảm biến khác vẫn chạy
    } else {
        ESP_LOGI(TAG, "Khoi tao MQ-2 thanh cong");
    }

    // 5. Khởi tạo cảm biến DHT11
    dht11_init(DHT11_PIN);
    ESP_LOGI(TAG, "Khoi tao DHT11 thanh cong");
    
    ESP_LOGI(TAG, "Bat dau doc gia tri toan bo cam bien...");

    // Vòng lặp đọc cảm biến định kỳ
    while(1){

        // p1: Đọc cảm biến quang trở
        int GIA_TRI_ANH_SANG = quangtro_read(my_adc_handle, QUANGTRO_CHANNEL);
        if(GIA_TRI_ANH_SANG != -1){
            ESP_LOGI(TAG, "Gia tri anh sang: %d", GIA_TRI_ANH_SANG);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien quang tro");
        }

        // p2: Đọc cảm biến độ ẩm đất
        int GIA_TRI_DO_AM = doamdat_read(my_adc_handle, DOAMDAT_CHANNEL);
        if(GIA_TRI_DO_AM != -1){
            ESP_LOGI(TAG, "Do am dat (Raw ADC): %d", GIA_TRI_DO_AM);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien do am dat");
        }

        // p3: Đọc cảm biến mực nước
        int GIA_TRI_MUC_NUOC = mucnuoc_read(my_adc_handle, MUCNUOC_CHANNEL);
        if(GIA_TRI_MUC_NUOC != -1){
            ESP_LOGI(TAG, "Muc nuoc (Raw ADC): %d", GIA_TRI_MUC_NUOC);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien muc nuoc");
        }
        
        // p4: Đọc cảm biến MQ-2
        int GIA_TRI_KHI_GA = mq2_read(my_adc_handle, MQ2_CHANNEL);
        if(GIA_TRI_KHI_GA != -1){
            ESP_LOGI(TAG, "Nong do khi gas (Raw ADC): %d", GIA_TRI_KHI_GA);
            
            if (GIA_TRI_KHI_GA > NGUONG_CANH_BAO_3V3) {
                ESP_LOGW(TAG, "⚠️ CANH BAO: Phat hien khi gas / khoi! (%d)", GIA_TRI_KHI_GA);
            }
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien MQ-2");
        }

        // p5: Đọc cảm biến DHT11
        float nhiet_do = 0.0f;
        float do_am = 0.0f;

        if(dht11_read(&nhiet_do, &do_am) == ESP_OK){
            ESP_LOGI(TAG, "Nhiet do: %.1f °C | Do am: %.1f%%", nhiet_do, do_am);
        }else{
            ESP_LOGW(TAG, "Khong the doc du lieu tu DHT11");
        }
        
        ESP_LOGI(TAG, "-----------------------------------");
        
        // Delay 3 giây trước khi đọc chu kỳ tiếp theo
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}