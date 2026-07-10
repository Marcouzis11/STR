#include "hall_sensor.h"
#include "gpio_hal.h"

int hall_sensor_init(int gpio) {
    int handle = gpio_open();
    if (handle < 0) return -1;
    gpio_claim_input(handle, gpio);
    return handle;
}

bool hall_sensor_detected(int handle, int gpio) {
    /* Nivel bajo = iman ausente = puerta abierta. */
    int level = gpio_read(handle, gpio);
    return (level == 0);
}
