#include "dht11.h"

#include <math.h>
#include <stdbool.h>

#include "dht.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DHT11_SENSOR";
static gpio_num_t dht_pin;
static bool initialized;
static int64_t last_read_us;

esp_err_t dht11_init(gpio_num_t pin)
{
    initialized = false;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = gpio_reset_pin(pin);
    if (err != ESP_OK) return err;
    err = gpio_set_direction(pin, GPIO_MODE_INPUT);
    if (err != ESP_OK) return err;
    err = gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    if (err != ESP_OK) return err;

    dht_pin = pin;
    last_read_us = 0;
    vTaskDelay(pdMS_TO_TICKS(2000));
    initialized = true;
    ESP_LOGI(TAG, "GPIO%d: driver esp-idf-lib/dht 1.2.0; cho khung du lieu hop le", pin);
    return ESP_OK;
}

esp_err_t dht11_read(float *temperature, float *humidity)
{
    if (temperature == NULL || humidity == NULL) return ESP_ERR_INVALID_ARG;
    if (!initialized) return ESP_ERR_INVALID_STATE;
    int64_t now = esp_timer_get_time();
    if (last_read_us != 0 && now - last_read_us < 2000000) {
        ESP_LOGW(TAG, "DHT11 can cach hai lan doc it nhat 2 giay");
        return ESP_ERR_INVALID_STATE;
    }
    last_read_us = now;

    float measured_temperature;
    float measured_humidity;
    // Thu vien tu phat START open-drain, chuyen input va kiem tra checksum.
    // Chu y API nay nhan humidity truoc temperature.
    esp_err_t err = dht_read_float_data(DHT_TYPE_DHT11, dht_pin,
                                       &measured_humidity, &measured_temperature);
    if (err != ESP_OK) return err;
    if (!isfinite(measured_temperature) || !isfinite(measured_humidity) ||
        measured_humidity < 0 || measured_humidity > 100) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *temperature = measured_temperature;
    *humidity = measured_humidity;
    return ESP_OK;
}
