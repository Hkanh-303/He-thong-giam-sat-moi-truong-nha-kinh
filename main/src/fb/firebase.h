#ifndef SMARTPLAN_FIREBASE_H
#define SMARTPLAN_FIREBASE_H

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool automatic;
    bool manual_on;
} firebase_pump_control_t;

/* Doc lenh cua app tu node1/pump_control. */
esp_err_t firebase_get_pump_control(firebase_pump_control_t *control);

esp_err_t firebase_send_sensor_data(int light_percent,
                                    int soil_moisture_percent,
                                    int water_level_percent,
                                    int mq2_signal_percent,
                                    float temperature_c,
                                    float humidity_percent,
                                    bool dht11_valid,
                                    bool pump_is_on);

/* Khoi phuc du lieu do gan nhat va bo dem tu Flash NVS sau khi mat nguon */
esp_err_t firebase_restore_last_sensor_data(void);

/* Lay cac gia tri hop le cuoi cung da khoi phuc tu NVS. */
bool firebase_get_last_sensor_data(int *light_percent, int *soil_moisture_percent,
                                   int *water_level_percent, int *mq2_signal_percent,
                                   float *temperature_c, float *humidity_percent,
                                   bool *dht11_valid);

/* Luu du lieu do moi nhat vao Flash NVS khi co thay doi (an toan khi cup dien) */
esp_err_t firebase_save_last_sensor_data(int light_percent,
                                         int soil_moisture_percent,
                                         int water_level_percent,
                                         int mq2_signal_percent,
                                         float temperature_c,
                                         float humidity_percent,
                                         bool dht11_valid,
                                         bool pump_is_on);

#endif // SMARTPLAN_FIREBASE_H
