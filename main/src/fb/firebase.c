#include "firebase.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "cJSON.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifi.h"

/* So ban ghi lich su toi da duoc giu tren Firebase. */
#define MAX_HISTORY_COUNT 100
#define MAX_HISTORY_RESPONSE_SIZE (128 * 1024)

/* Cau hinh NVS Flash luu tru du lieu khi mat nguon */
#define NVS_NAMESPACE        "smartplan"
#define NVS_KEY_SENSOR_DATA  "last_sensor"
#define NVS_DATA_MAGIC       0x534D5254 // "SMRT"

typedef struct {
    uint32_t magic;
    int light_percent;
    int soil_moisture_percent;
    int water_level_percent;
    int mq2_signal_percent;
    float temperature_c;
    float humidity_percent;
    bool dht11_valid;
    bool pump_is_on;
    int upload_count;
} nvs_sensor_record_t;

/* URL goc Realtime Database cua ban */
#define FIREBASE_BASE_URL    "https://pi4-iot-1b7bb-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_SENSOR_URL  FIREBASE_BASE_URL "/node1/sensor.json"
#define FIREBASE_PUMP_CONTROL_URL FIREBASE_BASE_URL "/node1/pump_control.json"

/* POST tao mot ID moi cho moi lan luu lich su. */
#define FIREBASE_HISTORY_URL FIREBASE_BASE_URL "/node1/history.json"

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
static last_sent_cache_t s_last_saved = {0};
static int s_upload_count = 0;

static bool has_sensor_data_changed(const last_sent_cache_t *cache, int light, int soil, int water, int mq2,
                                    float temp, float hum, bool dht_valid, bool pump);

static esp_err_t write_record_to_nvs(const last_sent_cache_t *cache, int count)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mo NVS de ghi loi (%s)", esp_err_to_name(err));
        return err;
    }

    nvs_sensor_record_t record = {
        .magic = NVS_DATA_MAGIC,
        .light_percent = cache->light_percent,
        .soil_moisture_percent = cache->soil_moisture_percent,
        .water_level_percent = cache->water_level_percent,
        .mq2_signal_percent = cache->mq2_signal_percent,
        .temperature_c = cache->temperature_c,
        .humidity_percent = cache->humidity_percent,
        .dht11_valid = cache->dht11_valid,
        .pump_is_on = cache->pump_is_on,
        .upload_count = count,
    };

    err = nvs_set_blob(handle, NVS_KEY_SENSOR_DATA, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "[NVS Flash] Da ghi du lieu do gan nhat vao Flash (an toan khi mat nguon)");
    } else {
        ESP_LOGE(TAG, "Ghi blob NVS loi (%s)", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t firebase_restore_last_sensor_data(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "[NVS] Chua co du lieu nao duoc luu truoc do (khoi dong lan dau)");
        return err;
    }

    nvs_sensor_record_t record = {0};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, NVS_KEY_SENSOR_DATA, &record, &size);
    nvs_close(handle);

    if (err == ESP_OK && record.magic == NVS_DATA_MAGIC && size == sizeof(record)) {
        s_last_saved.light_percent = record.light_percent;
        s_last_saved.soil_moisture_percent = record.soil_moisture_percent;
        s_last_saved.water_level_percent = record.water_level_percent;
        s_last_saved.mq2_signal_percent = record.mq2_signal_percent;
        s_last_saved.temperature_c = record.temperature_c;
        s_last_saved.humidity_percent = record.humidity_percent;
        s_last_saved.dht11_valid = record.dht11_valid;
        s_last_saved.pump_is_on = record.pump_is_on;
        s_last_saved.has_sent_first = true;
        s_upload_count = record.upload_count;

        ESP_LOGW(TAG, "===========================================================");
        ESP_LOGW(TAG, "[NVS] KHOI PHUC DU LIEU DO GAN NHAT TRUOC KHI MAT NGUON:");
        ESP_LOGW(TAG, "  * Anh sang        : %d%%", record.light_percent);
        ESP_LOGW(TAG, "  * Do am dat       : %d%%", record.soil_moisture_percent);
        ESP_LOGW(TAG, "  * Muc nuoc        : %d%%", record.water_level_percent);
        ESP_LOGW(TAG, "  * Nong do khi gas : %d%%", record.mq2_signal_percent);
        if (record.dht11_valid) {
            ESP_LOGW(TAG, "  * Nhiet do        : %.1f C | Do am KK: %.1f%%", record.temperature_c, record.humidity_percent);
        } else {
            ESP_LOGW(TAG, "  * Cam bien DHT11  : Khong kha dung");
        }
        ESP_LOGW(TAG, "  * Trang thai bom  : %s", record.pump_is_on ? "BAT" : "TAT");
        ESP_LOGW(TAG, "  * So lan gui da luu: %d", s_upload_count);
        ESP_LOGW(TAG, "===========================================================");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "[NVS] Du lieu trong Flash khong hop le hoac chua khoi tao");
    return ESP_FAIL;
}

bool firebase_get_last_sensor_data(int *light, int *soil, int *water, int *mq2,
                                   float *temperature, float *humidity, bool *dht_valid)
{
    if (!s_last_saved.has_sent_first) return false;
    *light = s_last_saved.light_percent;
    *soil = s_last_saved.soil_moisture_percent;
    *water = s_last_saved.water_level_percent;
    *mq2 = s_last_saved.mq2_signal_percent;
    *temperature = s_last_saved.temperature_c;
    *humidity = s_last_saved.humidity_percent;
    *dht_valid = s_last_saved.dht11_valid;
    return true;
}

esp_err_t firebase_save_last_sensor_data(int light_percent,
                                         int soil_moisture_percent,
                                         int water_level_percent,
                                         int mq2_signal_percent,
                                         float temperature_c,
                                         float humidity_percent,
                                         bool dht11_valid,
                                         bool pump_is_on)
{
    // Chi ghi vao Flash NVS khi du lieu co thay doi de tiet kiem tuoi tho Flash
    if (!has_sensor_data_changed(&s_last_saved, light_percent, soil_moisture_percent,
                                 water_level_percent, mq2_signal_percent,
                                 temperature_c, humidity_percent,
                                 dht11_valid, pump_is_on)) {
        return ESP_OK;
    }

    s_last_saved.light_percent = light_percent;
    s_last_saved.soil_moisture_percent = soil_moisture_percent;
    s_last_saved.water_level_percent = water_level_percent;
    s_last_saved.mq2_signal_percent = mq2_signal_percent;
    s_last_saved.temperature_c = temperature_c;
    s_last_saved.humidity_percent = humidity_percent;
    s_last_saved.dht11_valid = dht11_valid;
    s_last_saved.pump_is_on = pump_is_on;
    s_last_saved.has_sent_first = true;

    return write_record_to_nvs(&s_last_saved, s_upload_count);
}

static bool has_sensor_data_changed(const last_sent_cache_t *cache, int light, int soil, int water, int mq2,
                                    float temp, float hum, bool dht_valid, bool pump)
{
    // Lan dau tien sau khi khoi dong: bat buoc phai ghi mot lan
    if (!cache->has_sent_first) {
        return true;
    }

    if (cache->light_percent != light) return true;
    if (cache->soil_moisture_percent != soil) return true;
    if (cache->water_level_percent != water) return true;
    if (cache->mq2_signal_percent != mq2) return true;
    if (cache->dht11_valid != dht_valid) return true;
    if (cache->pump_is_on != pump) return true;

    if (dht_valid) {
        if (fabsf(cache->temperature_c - temp) >= 0.1f) return true;
        if (fabsf(cache->humidity_percent - hum) >= 0.1f) return true;
    }

    return false;
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
    if (err == ESP_OK && payload != NULL) {
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

typedef struct {
    char *data;
    size_t length;
    bool failed;
} history_response_t;

static esp_err_t history_response_handler(esp_http_client_event_t *event)
{
    history_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    size_t length = response->length + (size_t)event->data_len;
    if (length > MAX_HISTORY_RESPONSE_SIZE) {
        response->failed = true;
        return ESP_FAIL;
    }
    char *data = realloc(response->data, length + 1);
    if (data == NULL) {
        response->failed = true;
        return ESP_FAIL;
    }
    response->data = data;
    memcpy(data + response->length, event->data, event->data_len);
    data[length] = '\0';
    response->length = length;
    return ESP_OK;
}

typedef struct {
    char data[256];
    size_t length;
    bool overflow;
} pump_response_t;

static esp_err_t pump_response_handler(esp_http_client_event_t *event)
{
    pump_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if ((size_t)event->data_len >= sizeof(response->data) - response->length) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->length, event->data, event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

esp_err_t firebase_get_pump_control(firebase_pump_control_t *control)
{
    if (control == NULL) return ESP_ERR_INVALID_ARG;
    if (!wifi_is_connected()) return ESP_ERR_INVALID_STATE;

    pump_response_t response = {0};
    esp_http_client_config_t config = {
        .url = FIREBASE_PUMP_CONTROL_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .event_handler = pump_response_handler,
        .user_data = &response,
#if !FIREBASE_SKIP_CERT_VERIFY
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300 || response.overflow) {
        return err != ESP_OK ? err : ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(response.data);
    if (json == NULL) return ESP_FAIL;
    firebase_pump_control_t next = {.automatic = true, .manual_on = false};
    if (!cJSON_IsNull(json)) {
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
        const cJSON *manual_on = cJSON_GetObjectItemCaseSensitive(json, "manual_on");
        if (!cJSON_IsObject(json) || !cJSON_IsString(mode) ||
            (strcmp(mode->valuestring, "auto") != 0 && strcmp(mode->valuestring, "manual") != 0) ||
            !cJSON_IsBool(manual_on)) {
            cJSON_Delete(json);
            return ESP_ERR_INVALID_RESPONSE;
        }
        next.automatic = strcmp(mode->valuestring, "auto") == 0;
        next.manual_on = cJSON_IsTrue(manual_on);
    }
    cJSON_Delete(json);
    *control = next;
    return ESP_OK;
}

/* De danh mot cho trong 100 ban ghi truoc khi them lich su moi. */
static esp_err_t make_room_in_history(void)
{
    while (true) {
        history_response_t response = {0};
        esp_http_client_config_t config = {
            .url = FIREBASE_HISTORY_URL "?orderBy=%22timestamp%22&limitToFirst=100",
            .method = HTTP_METHOD_GET,
            .timeout_ms = 10000,
            .event_handler = history_response_handler,
            .user_data = &response,
#if !FIREBASE_SKIP_CERT_VERIFY
            .crt_bundle_attach = esp_crt_bundle_attach,
#endif
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) return ESP_ERR_NO_MEM;
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        if (err != ESP_OK || status < 200 || status >= 300 || response.failed) {
            ESP_LOGE(TAG, "Khong the doc lich su Firebase (HTTP %d)", status);
            free(response.data);
            return err != ESP_OK ? err : ESP_FAIL;
        }
        cJSON *history = response.data ? cJSON_Parse(response.data) : NULL;
        free(response.data);
        if (history == NULL || (!cJSON_IsObject(history) && !cJSON_IsNull(history))) {
            cJSON_Delete(history);
            return ESP_FAIL;
        }
        if (cJSON_GetArraySize(history) < MAX_HISTORY_COUNT) {
            cJSON_Delete(history);
            return ESP_OK;
        }
        // Firebase sap xep null truoc so; cung timestamp thi sap xep theo key.
        cJSON *oldest = NULL;
        double oldest_time = 0;
        bool oldest_has_time = false;
        cJSON *item;
        cJSON_ArrayForEach(item, history) {
            cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(item, "timestamp");
            bool has_time = cJSON_IsNumber(timestamp);
            double time = has_time ? timestamp->valuedouble : 0;
            if (oldest == NULL || (oldest_has_time && !has_time) ||
                (oldest_has_time == has_time &&
                 ((!has_time || time == oldest_time) ? strcmp(item->string, oldest->string) < 0 : time < oldest_time))) {
                oldest = item;
                oldest_time = time;
                oldest_has_time = has_time;
            }
        }
        if (oldest == NULL || oldest->string == NULL) {
            cJSON_Delete(history);
            return ESP_FAIL;
        }
        // Ma hoa key de co the dung an toan trong URL.
        size_t key_len = strlen(oldest->string);
        size_t url_size = strlen(FIREBASE_BASE_URL "/node1/history/.json") + key_len * 3 + 1;
        char *url = malloc(url_size);
        if (url == NULL) {
            cJSON_Delete(history);
            return ESP_ERR_NO_MEM;
        }
        size_t offset = (size_t)snprintf(url, url_size, FIREBASE_BASE_URL "/node1/history/");
        for (size_t i = 0; i < key_len; i++) {
            unsigned char ch = (unsigned char)oldest->string[i];
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '~') {
                url[offset++] = (char)ch;
            } else {
                offset += (size_t)snprintf(url + offset, url_size - offset, "%%%02X", ch);
            }
        }
        snprintf(url + offset, url_size - offset, ".json");
        cJSON_Delete(history);
        err = send_to_firebase(url, HTTP_METHOD_DELETE, NULL, 0, "node1/history/cu");
        free(url);
        if (err != ESP_OK) return err;
    }
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
    if (!has_sensor_data_changed(&s_last_sent, light_percent, soil_moisture_percent,
                                 water_level_percent, mq2_signal_percent,
                                 temperature_c, humidity_percent,
                                 dht11_valid, pump_is_on)) {
        ESP_LOGI(TAG, "Du lieu cam bien khong thay doi, bo qua khong luu len Firebase.");
        return ESP_OK;
    }

    // Khong gui null: Firebase PATCH se xoa truong neu nhan gia tri null.
    // Neu chua tung doc duoc cam bien, giu nguyen truong hien co tren Firebase.
    char payload[384];
    char *cursor = payload;
    size_t remaining = sizeof(payload);
    int written = snprintf(cursor, remaining, "{");
    if (written < 0 || (size_t)written >= remaining) return ESP_ERR_INVALID_SIZE;
    cursor += written;
    remaining -= (size_t)written;
#define APPEND_FIELD(...) do { \
        written = snprintf(cursor, remaining, __VA_ARGS__); \
        if (written < 0 || (size_t)written >= remaining) return ESP_ERR_INVALID_SIZE; \
        cursor += written; \
        remaining -= (size_t)written; \
    } while (0)
    if (dht11_valid) APPEND_FIELD("\"temperature\":%.1f,\"humidity\":%.1f,", temperature_c, humidity_percent);
    if (soil_moisture_percent >= 0) APPEND_FIELD("\"soil_moisture\":%d,", soil_moisture_percent);
    if (light_percent >= 0) APPEND_FIELD("\"light\":%d,", light_percent);
    if (water_level_percent >= 0) APPEND_FIELD("\"water_level\":%d,", water_level_percent);
    if (mq2_signal_percent >= 0) APPEND_FIELD("\"gas_mq2\":%d,", mq2_signal_percent);
    APPEND_FIELD("\"pump_on\":%s,\"timestamp\":{\".sv\":\"timestamp\"}}", pump_is_on ? "true" : "false");
#undef APPEND_FIELD
    int payload_length = (int)(cursor - payload);

    if (payload_length < 0 || (size_t)payload_length >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    // 1. Cap nhat trang thai hien tai (PATCH len node1/sensor.json)
    esp_err_t err_sensor = send_to_firebase(FIREBASE_SENSOR_URL, HTTP_METHOD_PATCH, payload, payload_length, "node1/sensor");

    // Chi tao lich su sau khi cap nhat trang thai hien tai thanh cong.
    if (err_sensor != ESP_OK) {
        return err_sensor;
    }

    esp_err_t room_err = make_room_in_history();
    if (room_err != ESP_OK) return room_err;

    // POST tao mot ban ghi moi voi ID do Firebase sinh ra.
    esp_err_t err_history = send_to_firebase(FIREBASE_HISTORY_URL, HTTP_METHOD_POST, payload, payload_length, "node1/history");

    if (err_sensor == ESP_OK && err_history == ESP_OK) {
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

        if (s_upload_count < INT_MAX) s_upload_count++;
        ESP_LOGI(TAG, "==> Da gui du lieu len Firebase thanh cong (lan %d)", s_upload_count);

        return ESP_OK;
    }

    return err_sensor != ESP_OK ? err_sensor : err_history;
}
