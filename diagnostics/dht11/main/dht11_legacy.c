#include "dht11_legacy.h"

#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#if CONFIG_IDF_TARGET_ESP32
#include "soc/gpio_struct.h"
#endif

static const char *TAG = "DHT11_SENSOR";
static gpio_num_t dht_pin;
static portMUX_TYPE dht_lock = portMUX_INITIALIZER_UNLOCKED;
static bool start_dump_reported;

esp_err_t legacy_dht11_init(gpio_num_t pin)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;

    esp_err_t err = gpio_reset_pin(pin);
    if (err != ESP_OK) return err;
    // Đặt latch ở mức nhả trước khi bật đầu ra open-drain.
    err = gpio_set_level(pin, 1);
    if (err != ESP_OK) return err;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pin_bit_mask = (1ULL << pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    dht_pin = pin;
    // Đợi cảm biến ổn định sau khi cấp nguồn.
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Da cau hinh DATA tai GPIO %d", dht_pin);
    return ESP_OK;
}

// Đợi mức tiếp theo; trả về thời gian đã đợi hoặc -1 nếu timeout.
static int wait_for_state(int state, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(dht_pin) != state) {
        if (esp_timer_get_time() - start >= timeout_us) return -1;
    }
    return (int)(esp_timer_get_time() - start);
}

esp_err_t legacy_dht11_read(float *temperature, float *humidity)
{
    if (temperature == NULL || humidity == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t data[5] = {0};
    const char *timeout_stage = "duong DATA len HIGH sau khi nha";
    int failed_bit = -1;
    int idle_level = gpio_get_level(dht_pin);
    int rise_time_us = -1;
    int final_level;

    // Kéo DATA xuống ít nhất 18 ms để yêu cầu DHT11 gửi dữ liệu.
    gpio_set_level(dht_pin, 0);
    esp_rom_delay_us(20000);
    int start_level = gpio_get_level(dht_pin);
    if (start_level != 0) {
#if CONFIG_IDF_TARGET_ESP32
        // Chụp thanh ghi khi vẫn đang yêu cầu LOW, trước khi nhả DATA.
        unsigned shift = (unsigned)dht_pin % 32;
        unsigned latch = ((dht_pin < 32 ? GPIO.out : GPIO.out1.val) >> shift) & 1U;
        unsigned output_enable = ((dht_pin < 32 ? GPIO.enable : GPIO.enable1.val) >> shift) & 1U;
        unsigned open_drain = GPIO.pin[dht_pin].pad_driver;
        uint32_t matrix = GPIO.func_out_sel_cfg[dht_pin].val;
#endif
        gpio_set_level(dht_pin, 1);
        ESP_LOGE(TAG, "GPIO %d khong xuong LOW khi gui START (idle=%d, start=%d)",
                 dht_pin, idle_level, start_level);
        if (!start_dump_reported) {
            start_dump_reported = true;
#if CONFIG_IDF_TARGET_ESP32
            ESP_LOGE(TAG, "START_SNAPSHOT: latch=%u oe=%u pad=%d od=%u matrix=0x%08lx",
                     latch, output_enable, start_level, open_drain, (unsigned long)matrix);
#endif
            // In sau khi nhả DATA; không giữ mức LOW trong lúc ghi log.
            gpio_dump_io_configuration(stdout, 1ULL << dht_pin);
        }
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&dht_lock);
    // Mức 1 ở chế độ open-drain nhả DATA; bắt đầu theo dõi ngay.
    gpio_set_level(dht_pin, 1);
    rise_time_us = wait_for_state(1, 100);
    if (rise_time_us < 0) goto timeout;

    timeout_stage = "DHT11 keo LOW de phan hoi";
    if (wait_for_state(0, 120) < 0) goto timeout;
    timeout_stage = "DHT11 ket thuc muc LOW phan hoi";
    if (wait_for_state(1, 120) < 0) goto timeout;
    timeout_stage = "DHT11 ket thuc muc HIGH phan hoi";
    if (wait_for_state(0, 120) < 0) goto timeout;

    for (int i = 0; i < 40; i++) {
        failed_bit = i;
        timeout_stage = "bit du lieu muc LOW";
        if (wait_for_state(1, 100) < 0) goto timeout;

        timeout_stage = "bit du lieu muc HIGH";
        int high_time = wait_for_state(0, 120);
        if (high_time < 0) goto timeout;
        if (high_time > 40) data[i / 8] |= (1 << (7 - (i % 8)));
    }
    taskEXIT_CRITICAL(&dht_lock);

    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        ESP_LOGE(TAG, "Checksum sai: du lieu %02x %02x %02x %02x %02x",
                 (unsigned)data[0], (unsigned)data[1], (unsigned)data[2],
                 (unsigned)data[3], (unsigned)data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    *humidity = data[0];
    *temperature = data[2];
    return ESP_OK;

timeout:
    final_level = gpio_get_level(dht_pin);
    taskEXIT_CRITICAL(&dht_lock);
    ESP_LOGE(TAG, "Timeout: %s; bit=%d idle=%d start=%d rise_us=%d final=%d",
             timeout_stage, failed_bit, idle_level, start_level, rise_time_us, final_level);
    return ESP_ERR_TIMEOUT;
}
