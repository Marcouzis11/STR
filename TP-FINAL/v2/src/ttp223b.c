#include "ttp223b.h"
#include "gpio_hal.h"

int ttp223b_init(int gpio) {
    int handle = gpio_open();
    if (handle < 0) return -1;
    gpio_claim_input(handle, gpio);
    return handle;
}

bool ttp223b_is_pressed(int handle, int gpio) {
    /* TTP223B en modo activo-alto: nivel 1 mientras se toca el sensor. */
    int level = gpio_read(handle, gpio);
    return (level == 1);
}
