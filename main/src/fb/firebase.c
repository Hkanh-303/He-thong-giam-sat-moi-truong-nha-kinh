#include "firebase.h"

#include <stdio.h>
#include <math.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "wifi.h"

/* Gioi han so lan gui du lieu len Firebase truoc khi reset chip */
#define MAX_UPLOAD_COUNT     100

/* URL goc Realtime Database cua ban */
#define FIREBASE_BASE_URL    "https://pi4-iot-1b7bb-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SENSOR_URL  FIREBASE_BASE_URL "/node1/sensor.json"

/* 
 * ID co dinh trong history (khop voi muc dang co san trong anh Firebase cua ban).
 * Khi dung PATCH len ID nay, du lieu se chi thay doi ben trong dung 1 history nay,
 * khong tao them cac ID moi lam dai danh sach.
 */
#define HISTORY_ITEM_ID      "-P2vwH5U9OlDTubrEDFn"
#define FIREBASE_HISTORY_URL FIREBASE_BASE_URL "/node1/history/" HISTORY_ITEM_ID ".json"

/* 
 * Dat 1 de bo qua kiem tra x509 Root CA.
 * Giup ket noi HTTPS Firebase thanh cong ngay ca khi mang Lab chan UDP/NTP
 * (gio he thong ESP32 van la nam 1970) hoac qua proxy/firewall.
 */
#define FIREBASE_SKIP_CERT_VERIFY 1

static const char *TAG = "FIREBASE";

typedef struct {
    int light_percent;
    int soil_moisture_percent;
    int water_level_percent;
    int mq2_signal_percent;
    float temperature_c;
    float humidity_percent;
    bool dht11_valid;
    bool pump_is_on;
    bool has_sent_first;
} last_sent_cache_t;

static last_sent_cache_t s_last_sent = {0};
static int s_upload_count = 0;

static bool has_sensor_data_changed(int light, int soil, int water, int mq2,
                                    float temp, float hum, bool dht_valid, bool pump)
{
    // Lan dau tien sau khi khoi dong: bat buoc phai ghi mot lan
    if (!s_last_sent.has_sent_first) {
        return true;
    }

    if (s_last_sent.light_percent != light) return true;
    if (s_last_sent.soil_moisture_percent != soil) return true;
    if (s_last_sent.water_level_percent != water) return true;
    if (s_last_sent.mq2_signal_percent != mq2) return true;
    if (s_last_sent.dht11_valid != dht_valid) return true;
    if (s_last_sent.pump_is_on != pump) return true;

    if (dht_valid) {
        if (fabsf(s_last_sent.temperature_c - temp) >= 0.1f) return true;
        if (fabsf(s_last_sent.humidity_percent - hum) >= 0.1f) return true;
    }

    return false;
}

static void format_sensor_value(char *buffer, size_t buffer_size, int value)
{
    if (value < 0) {
        snprintf(buffer, buffer_size, "null");
    } else {
        snprintf(buffer, buffer_size, "%d", value);
    }
}

static esp_err_t send_to_firebase(const char *url, esp_http_client_method_t method, const char *payload, int payload_length, const char *target_name)
{
    esp_http_client_config_t http_config = {
        .url = url,
        .method = method,
        .timeout_ms = 10000,
#if !FIREBASE_SKIP_CERT_VERIFY
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) {
        err = esp_http_client_set_post_field(client, payload, payload_length);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Dang gui [%s]: %s", target_name, url);
        err = esp_http_client_perform(client);
    }

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 200 && status_code < 300) {
            ESP_LOGI(TAG, "[%s] Gui thanh cong (HTTP %d)", target_name, status_code);
        } else {
            ESP_LOGE(TAG, "[%s] Firebase phan hoi HTTP %d", target_name, status_code);
            char resp_buf[128] = {0};
            int read_len = esp_http_client_read(client, resp_buf, sizeof(resp_buf) - 1);
            if (read_len > 0) {
                ESP_LOGE(TAG, "[%s] Chi tiet phan hoi: %s", target_name, resp_buf);
            }
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "[%s] Gui that bai: %s", target_name, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t firebase_send_sensor_data(int light_percent,
                                    int soil_moisture_percent,
                                    int water_level_percent,
                                    int mq2_signal_percent,
                                    float temperature_c,
                                    float humidity_percent,
                                    bool dht11_valid,
                                    bool pump_is_on)
{
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Khong gui Firebase: Wi-Fi chua ket noi");
        return ESP_ERR_INVALID_STATE;
    }

    if (!wifi_is_time_synced()) {
        ESP_LOGW(TAG, "Canh bao: thoi gian ESP32 chua dong bo NTP, van tiep tuc thu gui...");
    }

    // Kiem tra du lieu co thay doi so voi lan truoc khong (neu khong thay doi thi khong luu)
    if (!has_sensor_data_changed(light_percent, soil_moisture_percent,
                                 water_level_percent, mq2_signal_percent,
                                 temperature_c, humidity_percent,
                                 dht11_valid, pump_is_on)) {
        ESP_LOGI(TAG, "Du lieu cam bien khong thay doi, bo qua khong luu len Firebase.");
        return ESP_OK;
    }

    char light_json[12];
    char soil_json[12];
    char water_json[12];
    char mq2_json[12];
    char temperature_json[20];
    char humidity_json[20];
    char payload[384];

    format_sensor_value(light_json, sizeof(light_json), light_percent);
    format_sensor_value(soil_json, sizeof(soil_json), soil_moisture_percent);
    format_sensor_value(water_json, sizeof(water_json), water_level_percent);
    format_sensor_value(mq2_json, sizeof(mq2_json), mq2_signal_percent);

    if (dht11_valid) {
        snprintf(temperature_json, sizeof(temperature_json), "%.1f", temperature_c);
        snprintf(humidity_json, sizeof(humidity_json), "%.1f", humidity_percent);
    } else {
        snprintf(temperature_json, sizeof(temperature_json), "null");
        snprintf(humidity_json, sizeof(humidity_json), "null");
    }

    // JSON format khop voi cau truc tren Firebase Console
    int payload_length = snprintf(
        payload, sizeof(payload),
        "{\"temperature\":%s,"
        "\"humidity\":%s,"
        "\"soil_moisture\":%s,"
        "\"light\":%s,"
        "\"water_level\":%s,"
        "\"gas_mq2\":%s,"
        "\"pump_on\":%s,"
        "\"timestamp\":{\".sv\":\"timestamp\"}}",
        temperature_json, humidity_json,
        soil_json, light_json,
        water_json, mq2_json,
        pump_is_on ? "true" : "false");

    if (payload_length < 0 || (size_t)payload_length >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    // 1. Cap nhat trang thai hien tai (PATCH len node1/sensor.json)
    esp_err_t err_sensor = send_to_firebase(FIREBASE_SENSOR_URL, HTTP_METHOD_PATCH, payload, payload_length, "node1/sensor");

    // 2. Cap nhat vao dung 1 muc history co dinh (PATCH, khong tao them ID moi)
    esp_err_t err_history = send_to_firebase(FIREBASE_HISTORY_URL, HTTP_METHOD_PATCH, payload, payload_length, "node1/history/item");

    if (err_sensor == ESP_OK || err_history == ESP_OK) {
        // Luu lai gia tri moi nhat vua gui de so sanh o chu ky sau
        s_last_sent.light_percent = light_percent;
        s_last_sent.soil_moisture_percent = soil_moisture_percent;
        s_last_sent.water_level_percent = water_level_percent;
        s_last_sent.mq2_signal_percent = mq2_signal_percent;
        s_last_sent.temperature_c = temperature_c;
        s_last_sent.humidity_percent = humidity_percent;
        s_last_sent.dht11_valid = dht11_valid;
        s_last_sent.pump_is_on = pump_is_on;
        s_last_sent.has_sent_first = true;

        s_upload_count++;
        ESP_LOGI(TAG, "==> Da gui du lieu len Firebase thanh cong (Lan %d / %d)", s_upload_count, MAX_UPLOAD_COUNT);

        // Kiem tra neu da dat 100 lan thi reset
        if (s_upload_count >= MAX_UPLOAD_COUNT) {
            ESP_LOGW(TAG, "Da dat gioi han %d lan gui! Tien hanh khoi dong lai he thong (esp_restart)...", MAX_UPLOAD_COUNT);
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_restart();
        }

        return ESP_OK;
    }

    return err_sensor;
}
