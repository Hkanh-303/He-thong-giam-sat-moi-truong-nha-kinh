#include "dht11.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "DHT11_SENSOR";
static gpio_num_t dht_pin;

void dht11_init(gpio_num_t pin) {
    dht_pin = pin;
    gpio_reset_pin(dht_pin);
    
    // Cấu hình ban đầu là Input để chân ở trạng thái nổi
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << dht_pin),
        .pull_down_en = 0,
        .pull_up_en = 1 // Bật pull-up nội bộ
    };
    gpio_config(&io_conf);
    
    // Đợi cảm biến ổn định nguồn
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Khoi tao DHT11 tai GPIO %d", dht_pin);
}

// Hàm hỗ trợ đo độ dài xung
static int wait_for_state(int state, int timeout_us) {
    int time_waited = 0;
    while (gpio_get_level(dht_pin) != state) {
        if (time_waited > timeout_us) return -1;
        esp_rom_delay_us(1);
        time_waited++;
    }
    return time_waited;
}

esp_err_t dht11_read(float *temperature, float *humidity) {
    uint8_t data[5] = {0};
    
    // 1. Gửi tín hiệu Start
    gpio_set_direction(dht_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht_pin, 0);
    esp_rom_delay_us(20000); // Kéo xuống ít nhất 18ms
    gpio_set_level(dht_pin, 1);
    esp_rom_delay_us(40);    // Kéo lên 20-40us
    
    // 2. Chuyển sang đọc tín hiệu phản hồi
    gpio_set_direction(dht_pin, GPIO_MODE_INPUT);
    
    // Chờ DHT11 kéo xuống
    if (wait_for_state(0, 80) == -1) return ESP_ERR_TIMEOUT;
    // Chờ DHT11 kéo lên
    if (wait_for_state(1, 80) == -1) return ESP_ERR_TIMEOUT;
    // Chờ bắt đầu truyền dữ liệu
    if (wait_for_state(0, 80) == -1) return ESP_ERR_TIMEOUT;
    
    // 3. Đọc 40 bits (5 bytes)
    for (int i = 0; i < 40; i++) {
        // Chờ hết mức LOW
        if (wait_for_state(1, 50) == -1) return ESP_ERR_TIMEOUT;
        
        // Đo thời gian mức HIGH để xác định bit 0 hay 1
        int high_time = wait_for_state(0, 70);
        if (high_time == -1) return ESP_ERR_TIMEOUT;
        
        if (high_time > 40) {
            // Bit 1
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
    
    // 4. Kiểm tra Checksum
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        return ESP_ERR_INVALID_CRC;
    }
    
    // 5. Gán dữ liệu cho DHT11 (định dạng số nguyên)
    *humidity = data[0];
    *temperature = data[2];
    
    return ESP_OK;
}