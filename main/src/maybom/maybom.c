#include "maybom.h"

void maybom_init(gpio_num_t pin) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    // Tắt bơm khi vừa khởi động mạch (mức 0 / LOW)
    gpio_set_level(pin, 0); 
}

void maybom_on(gpio_num_t pin) {
    gpio_set_level(pin, 1); // Bật bơm (mức 1 / HIGH)
}

void maybom_off(gpio_num_t pin) {
    gpio_set_level(pin, 0); // Tắt bơm (mức 0 / LOW)
}
