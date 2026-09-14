#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"
#include "hal/adc_types.h"

#define QUANGTRO_CHANNEL       ADC_CHANNEL_4 // Tương ứng GPIO 32
#define DOAMDAT_CHANNEL        ADC_CHANNEL_5 // Tương ứng GPIO 33
#define MUCNUOC_CHANNEL        ADC_CHANNEL_6 // Tương ứng GPIO 34 
#define MQ2_CHANNEL            ADC_CHANNEL_7 // Tương ứng GPIO 35 (Chân cho MQ-2)
#define DHT11_PIN              GPIO_NUM_4    // Chân tín hiệu cho DHT11
#define CAMBIENMUA_CHANNEL     ADC_CHANNEL_3 // Tương ứng chân VN (GPIO 39)


#define NGUONG_CANH_BAO_3V3  1000  
#define NGUONG_MUA             3000     
#define MAYBOM_PIN GPIO_NUM_18 
#define NGUONG_DAT_KHO 3000 
#endif