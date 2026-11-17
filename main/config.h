#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"
#include "hal/adc_types.h"

/* Sơ đồ chân cho ESP32 (ADC1 dùng để tránh xung đột với Wi-Fi). */
#define QUANGTRO_PIN            GPIO_NUM_32  // A0, ADC1_CHANNEL_4
#define QUANGTRO_CHANNEL        ADC_CHANNEL_4

#define DOAMDAT_PIN             GPIO_NUM_33  // A0, ADC1_CHANNEL_5
#define DOAMDAT_CHANNEL         ADC_CHANNEL_5

#define MUCNUOC_PIN             GPIO_NUM_34  // A0, ADC1_CHANNEL_6
#define MUCNUOC_CHANNEL         ADC_CHANNEL_6

#define MQ2_PIN                 GPIO_NUM_35  // A0, ADC1_CHANNEL_7
#define MQ2_CHANNEL             ADC_CHANNEL_7

#define CAMBIENMUA_PIN          GPIO_NUM_39  // VN, ADC1_CHANNEL_3; đã nối theo README, chưa có driver trong source
#define CAMBIENMUA_CHANNEL      ADC_CHANNEL_3

#define DHT11_PIN               GPIO_NUM_17  // D17 / GPIO17, Data
#define MAYBOM_PIN              GPIO_NUM_21  // D21 / GPIO21, IN1 của relay

#define NGUONG_CANH_BAO_3V3      1000
#define DOAMDAT_BAT_BOM_PERCENT  30  // Bat tu dong khi thap hon muc nay, sau hieu chinh
#define DOAMDAT_TAT_BOM_PERCENT  45  // Tat khi dat muc nay de tranh relay nhap nhay

// EMA: alpha nho hon thi so do muot hon nhung phan ung cham hon.
#define SENSOR_ANALOG_FILTER_ALPHA 0.35f
#define SENSOR_DHT_FILTER_ALPHA    0.40f

#endif
