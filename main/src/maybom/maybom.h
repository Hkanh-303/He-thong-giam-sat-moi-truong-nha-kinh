#ifndef MAYBOM_H
#define MAYBOM_H

#include "driver/gpio.h"

void maybom_init(gpio_num_t pin);
void maybom_on(gpio_num_t pin);
void maybom_off(gpio_num_t pin);

#endif