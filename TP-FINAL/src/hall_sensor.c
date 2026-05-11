#include "hall_sensor.h"
#include <lgpio.h>

int hall_sensor_init(int gpio) {
    int handle = lgpio_open();
    if (handle < 0) return -1;
    lgpio_claim_input(handle, 0, gpio);
    return handle;
}

bool hall_sensor_detected(int handle, int gpio) {
    int level = lgpio_read(handle, 0, gpio);
    return (level == 0);
}