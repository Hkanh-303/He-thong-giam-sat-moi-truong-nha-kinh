#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

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
#include "wifi.h"
#include "firebase.h"

static adc_oneshot_unit_handle_t my_adc_handle = NULL;
static const char *TAG = "MAIN_APP";

typedef struct {
    int light_percent;
    int soil_moisture_percent;
    int water_level_percent;
    int mq2_signal_percent;
    float temperature_c;
    float humidity_percent;
    bool dht11_valid;
    bool pump_is_on;
    bool has_data;
} firebase_sensor_snapshot_t;

static firebase_sensor_snapshot_t s_sensor_snapshot = {
    .light_percent = -1,
    .soil_moisture_percent = -1,
    .water_level_percent = -1,
    .mq2_signal_percent = -1,
};
static portMUX_TYPE s_sensor_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Mac dinh tu dong; lenh thu cong het han neu mat ket noi Firebase.
static firebase_pump_control_t s_pump_control = {.automatic = true, .manual_on = false};
static TickType_t s_pump_control_updated;
static portMUX_TYPE s_pump_control_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void firebase_pump_control_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        firebase_pump_control_t control;
        esp_err_t err = firebase_get_pump_control(&control);
        if (err == ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            taskENTER_CRITICAL(&s_pump_control_spinlock);
            s_pump_control = control;
            s_pump_control_updated = now;
            taskEXIT_CRITICAL(&s_pump_control_spinlock);
        } else {
            ESP_LOGW(TAG, "Khong doc duoc lenh dieu khien bom: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void firebase_upload_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Task Firebase upload da khoi dong, doi ket noi Wi-Fi...");

    // Doi den khi Wi-Fi da ket noi thanh cong
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Wi-Fi da san sang, bat dau chu ky day du lieu Firebase!");

    while (1) {
        // Gui du lieu moi 15 giay
        vTaskDelay(pdMS_TO_TICKS(15000));

        firebase_sensor_snapshot_t data_to_send;
        taskENTER_CRITICAL(&s_sensor_spinlock);
        data_to_send = s_sensor_snapshot;
        taskEXIT_CRITICAL(&s_sensor_spinlock);

        if (data_to_send.has_data) {
            esp_err_t res = firebase_send_sensor_data(
                data_to_send.light_percent,
                data_to_send.soil_moisture_percent,
                data_to_send.water_level_percent,
                data_to_send.mq2_signal_percent,
                data_to_send.temperature_c,
                data_to_send.humidity_percent,
                data_to_send.dht11_valid,
                data_to_send.pump_is_on
            );
            if (res != ESP_OK) {
                ESP_LOGW(TAG, "Ket qua gui Firebase: %s", esp_err_to_name(res));
            }
        }
    }
}

// Chuan hoa gia tri ADC 12-bit thanh phan tram cua thang do 0-4095.
static int adc_to_percent(int adc_value) {
    if (adc_value < 0) return 0;
    if (adc_value > 4095) return 100;
    return (adc_value * 100 + 2047) / 4095;
}

void app_main(void){
    ESP_LOGI(TAG, "Khoi dong he thong SmartPlan");

    // Khoi tao Flash NVS va doc du lieu do gan nhat da luu truoc khi mat nguon
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    if (firebase_restore_last_sensor_data() == ESP_OK) {
        firebase_get_last_sensor_data(&s_sensor_snapshot.light_percent,
                                      &s_sensor_snapshot.soil_moisture_percent,
                                      &s_sensor_snapshot.water_level_percent,
                                      &s_sensor_snapshot.mq2_signal_percent,
                                      &s_sensor_snapshot.temperature_c,
                                      &s_sensor_snapshot.humidity_percent,
                                      &s_sensor_snapshot.dht11_valid);
    }

    esp_err_t wifi_init_result = wifi_init();
    if (wifi_init_result != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao Wi-Fi loi (%s); van tiep tuc doc cam bien",
                 esp_err_to_name(wifi_init_result));
    }

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

    // Tao FreeRTOS task rieng voi stack 8KB de day du lieu len Firebase ma khong chan vong lap cam bien
    xTaskCreate(firebase_upload_task, "fb_upload_task", 8192, NULL, 5, NULL);
    if (xTaskCreate(firebase_pump_control_task, "fb_pump_task", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc task doc lenh may bom");
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
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien do am dat");
        }

        firebase_pump_control_t control;
        TickType_t control_updated;
        taskENTER_CRITICAL(&s_pump_control_spinlock);
        control = s_pump_control;
        control_updated = s_pump_control_updated;
        taskEXIT_CRITICAL(&s_pump_control_spinlock);

        bool desired_on;
        if (control.automatic) {
            // Khi loi cam bien, tat bom thay vi giu trang thai bat cu.
            desired_on = GIA_TRI_DO_AM >= 0 && GIA_TRI_DO_AM > NGUONG_DAT_KHO;
        } else {
            bool command_fresh = (TickType_t)(xTaskGetTickCount() - control_updated) < pdMS_TO_TICKS(30000);
            desired_on = command_fresh && control.manual_on;
            if (!command_fresh) ESP_LOGW(TAG, "Lenh thu cong het han, tat bom");
        }
        if (desired_on) maybom_on(MAYBOM_PIN);
        else maybom_off(MAYBOM_PIN);
        pump_is_on = desired_on;

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

        esp_err_t dht_err = dht11_read(&nhiet_do, &do_am);
        if(dht_err == ESP_OK){
            dht11_valid = 1;
            ESP_LOGI(TAG, "Nhiet do: %.1f C | Do am: %.1f%%", nhiet_do, do_am);
        }else{
            ESP_LOGW(TAG, "Khong the doc du lieu tu DHT11: %s", esp_err_to_name(dht_err));
        }

        // Giu gia tri hop le cuoi cung khi mot cam bien khong doc duoc.
        taskENTER_CRITICAL(&s_sensor_spinlock);
        if (light_percent >= 0) s_sensor_snapshot.light_percent = light_percent;
        if (soil_moisture_percent >= 0) s_sensor_snapshot.soil_moisture_percent = soil_moisture_percent;
        if (water_level_percent >= 0) s_sensor_snapshot.water_level_percent = water_level_percent;
        if (mq2_signal_percent >= 0) s_sensor_snapshot.mq2_signal_percent = mq2_signal_percent;
        if (dht11_valid) {
            s_sensor_snapshot.temperature_c = nhiet_do;
            s_sensor_snapshot.humidity_percent = do_am;
            s_sensor_snapshot.dht11_valid = true;
        }
        s_sensor_snapshot.pump_is_on = (pump_is_on != 0);
        s_sensor_snapshot.has_data = true;
        firebase_sensor_snapshot_t latest = s_sensor_snapshot;
        taskEXIT_CRITICAL(&s_sensor_spinlock);

        firebase_save_last_sensor_data(
            latest.light_percent, latest.soil_moisture_percent,
            latest.water_level_percent, latest.mq2_signal_percent,
            latest.temperature_c, latest.humidity_percent,
            latest.dht11_valid, latest.pump_is_on
        );

        ESP_LOGI(TAG, "-----------------------------------");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
