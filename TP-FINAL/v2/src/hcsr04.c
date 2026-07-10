#include "hcsr04.h"
#include "gpio_hal.h"
#include <unistd.h>

/* Tiempo maximo de espera de eco: ~200cm ida y vuelta ~= 12ms. Usamos 30ms. */
#define HCSR04_ECHO_TIMEOUT_US 30000

int hcsr04_init(int trig_gpio, int echo_gpio) {
    int handle = gpio_open();
    if (handle < 0) return -1;

    gpio_claim_output(handle, trig_gpio, 0);
    gpio_claim_input(handle, echo_gpio);

    return handle;
}

uint8_t hcsr04_read_distance(int handle, int trig_gpio, int echo_gpio) {
    /* Pulso de disparo de 10us. */
    gpio_write(handle, trig_gpio, 1);
    usleep(10);
    gpio_write(handle, trig_gpio, 0);

    /* Espera a que el eco suba, CON timeout (v1 podia colgarse aqui). */
    int64_t wait_start = gpio_now_us();
    while (gpio_read(handle, echo_gpio) == 0) {
        if (gpio_now_us() - wait_start > HCSR04_ECHO_TIMEOUT_US) return 255;
    }

    int64_t echo_start = gpio_now_us();
    while (gpio_read(handle, echo_gpio) == 1) {
        if (gpio_now_us() - echo_start > HCSR04_ECHO_TIMEOUT_US) return 255;
    }
    int64_t echo_end = gpio_now_us();

    /* distancia_cm = duracion_us * 0.0343 / 2 = duracion_us * 343 / 20000. */
    int64_t duration_us = echo_end - echo_start;
    int64_t distance = (duration_us * 343) / 20000;

    if (distance > 200) distance = 200;
    if (distance < 0)   distance = 0;

    return (uint8_t)distance;
}
