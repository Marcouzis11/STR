#ifndef HCSR04_H
#define HCSR04_H

#include <stdint.h>
#include <stdbool.h>

int hcsr04_init(int trig_gpio, int echo_gpio);
uint8_t hcsr04_read_distance(int handle, int trig_gpio, int echo_gpio);

#endif