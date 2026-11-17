#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "config.h"
#include "dht11_legacy.h"
#include "dht.h"

static const char *TAG = "DHT_DIAG";

void app_main(void)
{
    // Giữ bơm tắt trong phiên chẩn đoán (mức 0).
    ESP_ERROR_CHECK(gpio_reset_pin(MAYBOM_PIN));
    ESP_ERROR_CHECK(gpio_set_level(MAYBOM_PIN, 0));
    ESP_ERROR_CHECK(gpio_set_direction(MAYBOM_PIN, GPIO_MODE_OUTPUT));

    ESP_LOGI(TAG, "DHT11 DIAGNOSTIC: DATA=GPIO%d; relay GPIO%d OFF", DHT11_PIN, MAYBOM_PIN);
    ESP_LOGI(TAG, "CUSTOM / REFERENCE doc luan phien; khong khoi tao ADC, Wi-Fi, Firebase");

    while (true) {
        float temperature = 0;
        float humidity = 0;

        ESP_LOGI(TAG, "CUSTOM: driver cu, luu de doi chieu");
        esp_err_t err = legacy_dht11_init(DHT11_PIN);
        if (err == ESP_OK) err = legacy_dht11_read(&temperature, &humidity);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "CUSTOM OK: %.1f C, %.1f %%RH", temperature, humidity);
        } else {
            ESP_LOGW(TAG, "CUSTOM FAIL: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "REFERENCE: esp-idf-lib/dht 1.2.0");
        ESP_ERROR_CHECK(gpio_reset_pin(DHT11_PIN));
        ESP_ERROR_CHECK(gpio_set_pull_mode(DHT11_PIN, GPIO_PULLUP_ONLY));
        // API tham chiếu nhận humidity trước temperature.
        err = dht_read_float_data(DHT_TYPE_DHT11, DHT11_PIN, &humidity, &temperature);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "REFERENCE OK: %.1f C, %.1f %%RH", temperature, humidity);
        } else {
            ESP_LOGW(TAG, "REFERENCE FAIL: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
