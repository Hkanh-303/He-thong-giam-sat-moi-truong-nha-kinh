#include "maybom.h"

void maybom_init(gpio_num_t pin) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    
    // Tắt bơm khi vừa khởi động mạch
    // Lưu ý: Đa số relay 5V màu xanh đen trên thị trường là loại "kích mức thấp" (Low-level trigger).
    // Nếu khi nạp code xong mà bơm tự chạy liên tục, hãy đổi số 0 ở đây thành 1.
    gpio_set_level(pin, 1); 
}

void maybom_on(gpio_num_t pin) {
    gpio_set_level(pin, 0); // Đổi thành 1 nếu relay của bạn kích mức thấp
}

void maybom_off(gpio_num_t pin) {
    gpio_set_level(pin, 1); // Đổi thành 0 nếu relay của bạn kích mức thấp
}