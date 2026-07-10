#include "laser_barrier.h"
#include "gpio_hal.h"

int laser_barrier_init(int gpio) {
    int handle = gpio_open();
    if (handle < 0) return -1;
    gpio_claim_input(handle, gpio);
    return handle;
}

bool laser_barrier_is_broken(int handle, int gpio) {
    /* Salida del LM393: nivel bajo cuando el haz laser esta interrumpido. */
    int level = gpio_read(handle, gpio);
    return (level == 0);
}
