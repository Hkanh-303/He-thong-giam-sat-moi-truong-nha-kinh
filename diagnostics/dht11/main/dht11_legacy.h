#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t legacy_dht11_init(gpio_num_t pin);
esp_err_t legacy_dht11_read(float *temperature, float *humidity);
