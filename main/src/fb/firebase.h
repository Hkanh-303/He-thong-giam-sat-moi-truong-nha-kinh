#ifndef SMARTPLAN_FIREBASE_H
#define SMARTPLAN_FIREBASE_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t firebase_send_sensor_data(int light_percent,
                                    int soil_moisture_percent,
                                    int water_level_percent,
                                    int mq2_signal_percent,
                                    float temperature_c,
                                    float humidity_percent,
                                    bool dht11_valid,
                                    bool pump_is_on);

#endif // SMARTPLAN_FIREBASE_H
