#ifndef HALL_SENSOR_H
#define HALL_SENSOR_H

#include <stdbool.h>

int hall_sensor_init(int gpio);
bool hall_sensor_detected(int handle, int gpio);

#endif