#include "maybom.h"

void maybom_init(gpio_num_t pin) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    
    // Tắt bơm khi vừa khởi động mạch
    // Lưu ý: Đa số relay 5V màu xanh đen trên thị trường là loại "kích mức thấp" (Low-level trigger).
    // Voi relay kich muc cao, dao ca ba muc GPIO trong module nay.
    gpio_set_level(pin, 1); 
}

void maybom_on(gpio_num_t pin) {
    gpio_set_level(pin, 0); // Relay kich muc thap
}

void maybom_off(gpio_num_t pin) {
    gpio_set_level(pin, 1); // Relay kich muc thap
}
