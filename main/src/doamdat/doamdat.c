#include "doamdat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define SOIL_SAMPLE_COUNT 16
#define SOIL_ADC_MAX 4095
#define SOIL_SAMPLE_MAX_AGE_US (10LL * 1000 * 1000)
#define SOIL_NVS_NAMESPACE "soil_cal"
#define SOIL_NVS_KEY "cal"
#define SOIL_NVS_MAGIC 0x534F494CUL
#define SOIL_NVS_VERSION 1

typedef struct {
    int dry;
    int wet;
} soil_calibration_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t dry;
    int32_t wet;
} soil_calibration_record_t;

static const char *TAG = "DOAMDAT_CAMBIEN";
static portMUX_TYPE s_soil_lock = portMUX_INITIALIZER_UNLOCKED;
static soil_calibration_t s_calibration = {.dry = -1, .wet = -1};
static int s_latest_raw = -1;
static int64_t s_latest_sample_us;

static bool calibration_ready(soil_calibration_t calibration)
{
    return calibration.dry >= 0 && calibration.dry <= SOIL_ADC_MAX &&
           calibration.wet >= 0 && calibration.wet <= SOIL_ADC_MAX &&
           abs(calibration.dry - calibration.wet) >= DOAMDAT_CALIBRATION_MIN_SPAN;
}

static soil_calibration_t calibration_snapshot(void)
{
    taskENTER_CRITICAL(&s_soil_lock);
    soil_calibration_t calibration = s_calibration;
    taskEXIT_CRITICAL(&s_soil_lock);
    return calibration;
}

static void remember_sample(int raw)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_soil_lock);
    s_latest_raw = raw;
    s_latest_sample_us = raw >= 0 ? now : 0;
    taskEXIT_CRITICAL(&s_soil_lock);
}

static void restore_calibration(void)
{
    soil_calibration_t calibration = {.dry = -1, .wet = -1};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SOIL_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        soil_calibration_record_t record = {0};
        size_t size = sizeof(record);
        err = nvs_get_blob(handle, SOIL_NVS_KEY, &record, &size);
        nvs_close(handle);
        if (err == ESP_OK) {
            if (size == sizeof(record) && record.magic == SOIL_NVS_MAGIC &&
                record.version == SOIL_NVS_VERSION &&
                record.dry >= -1 && record.dry <= SOIL_ADC_MAX &&
                record.wet >= -1 && record.wet <= SOIL_ADC_MAX) {
                soil_calibration_t restored = {.dry = record.dry, .wet = record.wet};
                if (restored.dry < 0 || restored.wet < 0 || calibration_ready(restored)) {
                    calibration = restored;
                } else {
                    ESP_LOGW(TAG, "Bo qua hieu chinh NVS: hai moc qua gan nhau");
                }
            } else {
                ESP_LOGW(TAG, "Bo qua ban ghi hieu chinh NVS khong hop le");
            }
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Khong khoi phuc duoc hieu chinh dat: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Khong mo duoc NVS hieu chinh dat: %s", esp_err_to_name(err));
    }
    if (calibration_ready(calibration)) {
        ESP_LOGI(TAG, "Da nap moc dat tu NVS: kho=%d, uot=%d", calibration.dry, calibration.wet);
    } else {
        // Tu dong nap moc mac dinh: kho=3200, uot=1400 de luon co so do %
        calibration.dry = 3200;
        calibration.wet = 1400;
        ESP_LOGI(TAG, "Chua co moc tu NVS, su dung moc mac dinh: kho=%d, uot=%d", calibration.dry, calibration.wet);
    }
    taskENTER_CRITICAL(&s_soil_lock);
    s_calibration = calibration;
    taskEXIT_CRITICAL(&s_soil_lock);
}

static esp_err_t save_calibration(soil_calibration_t calibration)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SOIL_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    soil_calibration_record_t record = {
        .magic = SOIL_NVS_MAGIC, .version = SOIL_NVS_VERSION,
        .dry = calibration.dry, .wet = calibration.wet,
    };
    err = nvs_set_blob(handle, SOIL_NVS_KEY, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_soil_lock);
        s_calibration = calibration;
        taskEXIT_CRITICAL(&s_soil_lock);
    }
    return err;
}

esp_err_t doamdat_init(adc_oneshot_unit_handle_t *adc_handle, adc_channel_t channel)
{
    if (adc_handle == NULL) return ESP_ERR_INVALID_ARG;
    int gpio_num = -1;
    esp_err_t err = adc_oneshot_channel_to_io(ADC_UNIT_1, channel, &gpio_num);
    if (err != ESP_OK) return err;
    if (*adc_handle == NULL) {
        adc_oneshot_unit_init_cfg_t init_config = {.unit_id = ADC_UNIT_1};
        err = adc_oneshot_new_unit(&init_config, adc_handle);
        if (err != ESP_OK) return err;
    }
    ESP_LOGI(TAG, "Do am dat: GPIO%d / ADC1_CH%d, 12-bit, suy hao 12dB", gpio_num, channel);
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(*adc_handle, channel, &config);
    if (err != ESP_OK) return err;
    remember_sample(-1);
    restore_calibration();
    return ESP_OK;
}

int doamdat_read(adc_oneshot_unit_handle_t adc_handle, adc_channel_t channel)
{
    if (adc_handle == NULL) {
        remember_sample(-1);
        return -1;
    }
    int samples[SOIL_SAMPLE_COUNT];
    int valid_count = 0;
    for (int i = 0; i < SOIL_SAMPLE_COUNT; ++i) {
        int raw_val = 0;
        esp_err_t err = adc_oneshot_read(adc_handle, channel, &raw_val);
        if (err == ESP_OK) {
            samples[valid_count++] = raw_val;
        }
        if (i + 1 < SOIL_SAMPLE_COUNT) esp_rom_delay_us(200);
    }
    if (valid_count == 0) {
        remember_sample(-1);
        ESP_LOGW(TAG, "Doc ADC dat that bai tren channel %d", channel);
        return -1;
    }
    // Sắp xếp các mẫu hợp lệ để lấy trung vị (median)
    for (int i = 0; i < valid_count; ++i) {
        for (int j = i + 1; j < valid_count; ++j) {
            if (samples[i] > samples[j]) {
                int swap = samples[i];
                samples[i] = samples[j];
                samples[j] = swap;
            }
        }
    }
    int raw = samples[valid_count / 2];
    remember_sample(raw);
    return raw;
}

bool doamdat_is_calibrated(void)
{
    return calibration_ready(calibration_snapshot());
}

int doamdat_percent(int raw)
{
    if (raw < 0 || raw > SOIL_ADC_MAX) return -1;
    soil_calibration_t calibration = calibration_snapshot();
    if (!calibration_ready(calibration)) {
        // Fallback thang do ADC dao nguoc: 4095 la kho (0%), 0 la uot (100%)
        int p = (4095 - raw) * 100 / 4095;
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        return p;
    }
    int span = calibration.dry - calibration.wet;
    int delta = calibration.dry - raw;
    if (span < 0) {
        span = -span;
        delta = -delta;
    }
    if (delta <= 0) return 0;
    if (delta >= span) return 100;
    return (delta * 100 + span / 2) / span;
}

static void print_status(void)
{
    taskENTER_CRITICAL(&s_soil_lock);
    soil_calibration_t calibration = s_calibration;
    int raw = s_latest_raw;
    int64_t sample_us = s_latest_sample_us;
    taskEXIT_CRITICAL(&s_soil_lock);
    printf("SOIL: dry=%d wet=%d calibrated=%s\n", calibration.dry, calibration.wet,
           calibration_ready(calibration) ? "yes" : "no");
    if (raw >= 0) {
        printf("SOIL: raw=%d/4095 age=%lld ms\n", raw,
               (long long)((esp_timer_get_time() - sample_us) / 1000));
    } else {
        printf("SOIL: chua co mau ADC hop le\n");
    }
    printf("Dat dau do trong dat kho, cho so do on dinh roi nhap: soil dry\n");
    printf("Dat dau do trong dat uot, cho so do on dinh roi nhap: soil wet\n");
    printf("Hai moc can lech nhau it nhat %d muc ADC. Giu nguyen day AO va nguon khi hieu chinh.\n",
           DOAMDAT_CALIBRATION_MIN_SPAN);
}

static esp_err_t capture_endpoint(bool dry)
{
    taskENTER_CRITICAL(&s_soil_lock);
    soil_calibration_t calibration = s_calibration;
    int raw = s_latest_raw;
    int64_t sample_us = s_latest_sample_us;
    taskEXIT_CRITICAL(&s_soil_lock);
    if (raw < 0 || raw > SOIL_ADC_MAX || esp_timer_get_time() - sample_us > SOIL_SAMPLE_MAX_AGE_US) {
        printf("SOIL: khong co mau ADC moi (<10 giay); doi chu ky doc tiep theo va thu lai\n");
        return ESP_ERR_INVALID_STATE;
    }
    if (dry) calibration.dry = raw;
    else calibration.wet = raw;
    if (calibration.dry >= 0 && calibration.wet >= 0 && !calibration_ready(calibration)) {
        printf("SOIL: tu choi moc %s=%d; dry=%d wet=%d qua gan nhau (can >=%d).\n",
               dry ? "dry" : "wet", raw, calibration.dry, calibration.wet,
               DOAMDAT_CALIBRATION_MIN_SPAN);
        printf("Cho dau do on dinh o trang thai kho/uot ro rang, roi thu lai. Moc cu van duoc giu.\n");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = save_calibration(calibration);
    if (err == ESP_OK) {
        printf("SOIL: da luu %s=%d vao NVS; dry=%d wet=%d calibrated=%s\n",
               dry ? "dry" : "wet", raw, calibration.dry, calibration.wet,
               calibration_ready(calibration) ? "yes" : "no");
    }
    return err;
}

static esp_err_t reset_calibration(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SOIL_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, SOIL_NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_soil_lock);
        s_calibration = (soil_calibration_t){.dry = -1, .wet = -1};
        taskEXIT_CRITICAL(&s_soil_lock);
        printf("SOIL: da xoa hai moc hieu chinh\n");
    }
    return err;
}

esp_err_t doamdat_calibrate_dry(void)
{
    return capture_endpoint(true);
}

esp_err_t doamdat_calibrate_wet(void)
{
    return capture_endpoint(false);
}

esp_err_t doamdat_reset_calibration(void)
{
    return reset_calibration();
}

void doamdat_get_calibration(int *dry, int *wet, bool *is_calibrated)
{
    soil_calibration_t cal = calibration_snapshot();
    if (dry) *dry = cal.dry;
    if (wet) *wet = cal.wet;
    if (is_calibrated) *is_calibrated = calibration_ready(cal);
}

esp_err_t doamdat_set_default_calibration(void)
{
    soil_calibration_t cal = {.dry = 3200, .wet = 1400};
    return save_calibration(cal);
}

static int soil_command(int argc, char **argv)
{
    if (argc != 2) {
        printf("Dung: soil dry | soil wet | soil status | soil reset\n");
        print_status();
        return 1;
    }
    if (strcmp(argv[1], "status") == 0) {
        print_status();
        return 0;
    }
    esp_err_t err;
    if (strcmp(argv[1], "dry") == 0) err = capture_endpoint(true);
    else if (strcmp(argv[1], "wet") == 0) err = capture_endpoint(false);
    else if (strcmp(argv[1], "reset") == 0) err = reset_calibration();
    else {
        printf("Dung: soil dry | soil wet | soil status | soil reset\n");
        return 1;
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "Lenh soil %s that bai: %s", argv[1], esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

esp_err_t doamdat_register_console_commands(void)
{
    const esp_console_cmd_t command = {
        .command = "soil",
        .help = "Hieu chinh do am dat: dry, wet, status, reset",
        .hint = NULL,
        .func = soil_command,
    };
    return esp_console_cmd_register(&command);
}
