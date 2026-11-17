#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_console.h"

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
#include "webserver.h"

_Static_assert(DOAMDAT_BAT_BOM_PERCENT >= 0 &&
               DOAMDAT_BAT_BOM_PERCENT < DOAMDAT_TAT_BOM_PERCENT &&
               DOAMDAT_TAT_BOM_PERCENT <= 100, "Nguong bom phai nam trong 0..100 va BAT < TAT");

static adc_oneshot_unit_handle_t my_adc_handle = NULL;
static const char *TAG = "MAIN_APP";

typedef struct {
    int light_percent;
    int soil_moisture_percent;
    int soil_adc_raw;
    bool soil_calibrated;
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
    .soil_adc_raw = -1,
    .water_level_percent = -1,
    .mq2_signal_percent = -1,
};
static portMUX_TYPE s_sensor_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Mac dinh tu dong; lenh thu cong het han neu sau 30 giay khong co lenh moi.
static firebase_pump_control_t s_pump_control = {.automatic = true, .manual_on = false, .updated_ts = 0};
static TickType_t s_pump_control_updated = 0;
static portMUX_TYPE s_pump_control_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void firebase_pump_control_task(void *pvParameters)
{
    (void)pvParameters;
    firebase_pump_control_t last_control = {.automatic = true, .manual_on = false, .updated_ts = 0};
    bool first_fetch = true;

    while (1) {
        firebase_pump_control_t control;
        esp_err_t err = firebase_get_pump_control(&control);
        if (err == ESP_OK) {
            TickType_t now = xTaskGetTickCount();
            bool report_stale_manual = false;
            bool report_new_control = false;
            taskENTER_CRITICAL(&s_pump_control_spinlock);

            if (first_fetch) {
                first_fetch = false;
                s_pump_control = control;
                last_control = control;
                // Khi vua khoi dong, neu Firebase ton dong lenh manual cu thi danh dau da het han
                if (!control.automatic) {
                    // Subtraction is deliberately unsigned: it preserves the 35-second
                    // age even when the system has only been running for a few ticks.
                    s_pump_control_updated = now - pdMS_TO_TICKS(35000);
                    report_stale_manual = true;
                } else {
                    s_pump_control_updated = now;
                }
            } else {
                bool changed = (control.automatic != last_control.automatic) ||
                               (control.manual_on != last_control.manual_on) ||
                               (control.updated_ts > 0 && control.updated_ts != last_control.updated_ts);
                if (changed) {
                    s_pump_control = control;
                    last_control = control;
                    s_pump_control_updated = now;
                    report_new_control = true;
                }
            }
            taskEXIT_CRITICAL(&s_pump_control_spinlock);

            // Logging may take internal locks, so keep it outside the critical section.
            if (report_stale_manual) {
                ESP_LOGW(TAG, "Firebase ton dong lenh manual cu, bo qua de uu tien tu dong");
            }
            if (report_new_control) {
                ESP_LOGI(TAG, "Nhan lenh MOI tu Firebase: mode=%s, manual_on=%d",
                         control.automatic ? "auto" : "manual", control.manual_on);
            }
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
                data_to_send.pump_is_on,
                data_to_send.soil_adc_raw,
                data_to_send.soil_calibrated
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

typedef struct {
    bool initialized;
    float value;
} sensor_ema_t;

/* EMA low-pass filter: alpha smaller -> steadier output, slower response. */
static float sensor_ema_update(float sample, sensor_ema_t *filter, float alpha)
{
    if (!filter->initialized) {
        filter->value = sample;
        filter->initialized = true;
    } else {
        filter->value += alpha * (sample - filter->value);
    }
    return filter->value;
}

static int sensor_ema_percent(int sample, sensor_ema_t *filter, float alpha)
{
    if (sample < 0) return -1;
    float value = sensor_ema_update((float)sample, filter, alpha);
    if (value < 0.0f) value = 0.0f;
    if (value > 100.0f) value = 100.0f;
    return (int)(value + 0.5f);
}

static esp_err_t start_calibration_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "greenhouse> ";
    config.max_cmdline_length = 96;
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &config, &repl);
    if (err != ESP_OK) return err;
    err = doamdat_register_console_commands();
    if (err == ESP_OK) err = esp_console_start_repl(repl);
    if (err != ESP_OK) repl->del(repl);
    return err;
}

void app_main(void){
    ESP_LOGI(TAG, "Khoi dong he thong SmartPlan");
    maybom_init(MAYBOM_PIN);
    ESP_LOGI(TAG, "Relay GPIO%d mac dinh OFF", MAYBOM_PIN);

    // Khoi tao Flash NVS va doc du lieu do gan nhat da luu truoc khi mat nguon
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_err = nvs_flash_erase();
        if (nvs_err == ESP_OK) nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS khong san sang: %s; giu bom tat", esp_err_to_name(nvs_err));
        return;
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
    // Du lieu phuc hoi la lich su; doi phep do moi de xac nhan trang thai hien tai.
    s_sensor_snapshot.dht11_valid = false;
    s_sensor_snapshot.soil_moisture_percent = -1;

    esp_err_t wifi_init_result = wifi_init();
    if (wifi_init_result != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao Wi-Fi loi (%s); van tiep tuc doc cam bien",
                 esp_err_to_name(wifi_init_result));
    }

    esp_err_t web_err = webserver_init();
    if (web_err != ESP_OK) {
        ESP_LOGW(TAG, "Khoi tao Webserver that bai: %s", esp_err_to_name(web_err));
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

    // 5. Cau hinh chan DHT11; chi xac nhan cam bien sau khi doc duoc du lieu.
    esp_err_t init_dht11 = dht11_init(DHT11_PIN);
    if (init_dht11 != ESP_OK) {
        ESP_LOGE(TAG, "Cau hinh DHT11 that bai: %s", esp_err_to_name(init_dht11));
    }
    
    esp_err_t console_err = start_calibration_console();
    if (console_err != ESP_OK) {
        ESP_LOGE(TAG, "Khong mo duoc console hieu chinh: %s", esp_err_to_name(console_err));
    }

    // Tao FreeRTOS task rieng voi stack 8KB de day du lieu len Firebase ma khong chan vong lap cam bien
    xTaskCreate(firebase_upload_task, "fb_upload_task", 8192, NULL, 5, NULL);
    if (xTaskCreate(firebase_pump_control_task, "fb_pump_task", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc task doc lenh may bom");
    }

    ESP_LOGI(TAG, "Bat dau doc gia tri toan bo cam bien...");

    // Vong lap doc cam bien dinh ky
    int pump_is_on = 0;
    sensor_ema_t light_filter = {0};
    sensor_ema_t soil_filter = {0};
    sensor_ema_t water_filter = {0};
    sensor_ema_t gas_filter = {0};
    sensor_ema_t temperature_filter = {0};
    sensor_ema_t humidity_filter = {0};
    while(1){
        int light_percent = -1;
        int soil_moisture_percent = -1;
        int water_level_percent = -1;
        int mq2_signal_percent = -1;

        // p1: Doc cam bien quang tro
        int GIA_TRI_ANH_SANG = quangtro_read(my_adc_handle, QUANGTRO_CHANNEL);
        if(GIA_TRI_ANH_SANG != -1){
            light_percent = sensor_ema_percent(adc_to_percent(GIA_TRI_ANH_SANG), &light_filter, SENSOR_ANALOG_FILTER_ALPHA);
            ESP_LOGI(TAG, "Tin hieu quang tro: %d%% thang ADC", light_percent);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien quang tro");
        }

        // p2: Doc cam bien do am dat va dieu khien bom
        int GIA_TRI_DO_AM = doamdat_read(my_adc_handle, DOAMDAT_CHANNEL);
        if(GIA_TRI_DO_AM != -1){
            soil_moisture_percent = sensor_ema_percent(doamdat_percent(GIA_TRI_DO_AM), &soil_filter, SENSOR_ANALOG_FILTER_ALPHA);
            ESP_LOGI(TAG, "Do am dat GPIO%d: raw=%d/4095 | do am=%d%%",
                     DOAMDAT_PIN, GIA_TRI_DO_AM, soil_moisture_percent);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien do am dat (kiem tra day vao chan GPIO%d)", DOAMDAT_PIN);
        }
        bool soil_calibrated = doamdat_is_calibrated();

        firebase_pump_control_t control;
        TickType_t control_updated;
        taskENTER_CRITICAL(&s_pump_control_spinlock);
        control = s_pump_control;
        control_updated = s_pump_control_updated;
        taskEXIT_CRITICAL(&s_pump_control_spinlock);

        webserver_pump_mode_t web_pump_mode = webserver_get_pump_mode();

        bool desired_on = false;
        if (web_pump_mode == WEBSERVER_PUMP_MANUAL_ON) {
            desired_on = true;
        } else if (web_pump_mode == WEBSERVER_PUMP_MANUAL_OFF) {
            desired_on = false;
        } else {
            // Webserver dang o CHE DO TU DONG
            // Kiem tra xem Firebase co lenh thu cong moi (< 30 giay) hay khong:
            bool firebase_manual_fresh = false;
            TickType_t now_tick = xTaskGetTickCount();
            if (!control.automatic) {
                if ((TickType_t)(now_tick - control_updated) < pdMS_TO_TICKS(30000)) {
                    firebase_manual_fresh = true;
                } else {
                    static TickType_t s_last_timeout_warn = 0;
                    if ((TickType_t)(now_tick - s_last_timeout_warn) > pdMS_TO_TICKS(15000)) {
                        s_last_timeout_warn = now_tick;
                        ESP_LOGW(TAG, "Lenh thu cong Firebase het han (>30s), bom van hanh tu dong theo do am dat");
                    }
                }
            }

            if (firebase_manual_fresh) {
                desired_on = control.manual_on;
            } else {
                // CHE DO TU DONG THEO DO AM DAT
                // Duoi DOAMDAT_BAT_BOM_PERCENT (30%): Bat bom
                // Tu DOAMDAT_TAT_BOM_PERCENT (45%) tro len: Tat bom
                if (soil_moisture_percent >= 0) {
                    if (pump_is_on) {
                        desired_on = (soil_moisture_percent < DOAMDAT_TAT_BOM_PERCENT);
                    } else {
                        desired_on = (soil_moisture_percent < DOAMDAT_BAT_BOM_PERCENT);
                    }
                } else {
                    // Chua co so do hop le -> tat bom dam bao an toan
                    desired_on = false;
                }
            }
        }
        if (desired_on) maybom_on(MAYBOM_PIN);
        else maybom_off(MAYBOM_PIN);
        pump_is_on = desired_on;

        // p3: Doc cam bien muc nuoc
        int GIA_TRI_MUC_NUOC = mucnuoc_read(my_adc_handle, MUCNUOC_CHANNEL);
        if(GIA_TRI_MUC_NUOC != -1){
            water_level_percent = sensor_ema_percent(adc_to_percent(GIA_TRI_MUC_NUOC), &water_filter, SENSOR_ANALOG_FILTER_ALPHA);
            ESP_LOGI(TAG, "Muc nuoc: %d%% thang ADC", water_level_percent);
        } else {
            ESP_LOGW(TAG, "Khong the doc du lieu tu cam bien muc nuoc");
        }
        
        // p4: Doc cam bien MQ-2
        int GIA_TRI_KHI_GA = mq2_read(my_adc_handle, MQ2_CHANNEL);
        if(GIA_TRI_KHI_GA != -1){
            mq2_signal_percent = sensor_ema_percent(adc_to_percent(GIA_TRI_KHI_GA), &gas_filter, SENSOR_ANALOG_FILTER_ALPHA);
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

        esp_err_t dht_err = init_dht11 == ESP_OK ? dht11_read(&nhiet_do, &do_am) : init_dht11;
        if(dht_err == ESP_OK){
            nhiet_do = sensor_ema_update(nhiet_do, &temperature_filter, SENSOR_DHT_FILTER_ALPHA);
            do_am = sensor_ema_update(do_am, &humidity_filter, SENSOR_DHT_FILTER_ALPHA);
            dht11_valid = 1;
            ESP_LOGI(TAG, "Nhiet do: %.1f C | Do am: %.1f%%", nhiet_do, do_am);
        }else{
            ESP_LOGW(TAG, "Khong the doc du lieu tu DHT11: %s", esp_err_to_name(dht_err));
        }

        // Soil/DHT mang trang thai cua chu ky hien tai, tranh cong bo lai so lieu cu.
        taskENTER_CRITICAL(&s_sensor_spinlock);
        if (light_percent >= 0) s_sensor_snapshot.light_percent = light_percent;
        s_sensor_snapshot.soil_moisture_percent = soil_moisture_percent;
        s_sensor_snapshot.soil_adc_raw = GIA_TRI_DO_AM;
        s_sensor_snapshot.soil_calibrated = soil_calibrated;
        if (water_level_percent >= 0) s_sensor_snapshot.water_level_percent = water_level_percent;
        if (mq2_signal_percent >= 0) s_sensor_snapshot.mq2_signal_percent = mq2_signal_percent;
        if (dht11_valid) {
            s_sensor_snapshot.temperature_c = nhiet_do;
            s_sensor_snapshot.humidity_percent = do_am;
        }
        s_sensor_snapshot.dht11_valid = (dht11_valid != 0);
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

        webserver_record_sensor_data(
            light_percent,
            soil_moisture_percent,
            GIA_TRI_DO_AM,
            soil_calibrated,
            water_level_percent,
            mq2_signal_percent,
            nhiet_do,
            do_am,
            (dht11_valid != 0),
            (pump_is_on != 0)
        );

        ESP_LOGI(TAG, "-----------------------------------");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
