#include "dht11.h"
#include "gpio_hal.h"
#include "config.h"
#include <unistd.h>

/*
 * Driver DHT11 (protocolo 1-wire propietario) mediante bit-banging.
 *
 * Nota importante (ver RESUMEN.md): leer el DHT11 por bit-banging desde
 * espacio de usuario en Linux NO es determinista (el planificador puede
 * interrumpir el muestreo de us). Se conserva por compatibilidad con el v1,
 * pero para un sistema de tiempo real serio conviene el overlay de kernel
 * dht11 (/sys/bus/iio) o un microcontrolador auxiliar. El backend de
 * SIMULACION devuelve una lectura fija valida para poder ejercitar el sistema.
 */

int dht11_init(int gpio_chip, int gpio_offset) {
    (void)gpio_chip;
    int handle = gpio_open();
    if (handle < 0) return -1;
    gpio_claim_output(handle, gpio_offset, 1);
    return handle;
}

#ifndef USE_LGPIO
/* --- Backend de simulacion: lectura ambiental plausible y estable. --- */
dht11_reading_t dht11_read(int handle) {
    (void)handle;
    dht11_reading_t r = {.temperature = 24.0f, .humidity = 55.0f, .valid = true};
    return r;
}
#else
/* --- Backend real (bit-banging con lgpio). --- */
dht11_reading_t dht11_read(int handle) {
    dht11_reading_t result = {.valid = false, .temperature = 0.0f, .humidity = 0.0f};

    int gpio = DHT11_GPIO;
    uint8_t data[5] = {0};

    /* Senal de arranque: bajar la linea >18ms y luego liberar. */
    gpio_claim_output(handle, gpio, 0);
    usleep(20000);
    gpio_write(handle, gpio, 1);
    usleep(30);

    /* Pasar a entrada para leer la respuesta del sensor. */
    gpio_claim_input(handle, gpio);
    gpio_set_pull(handle, gpio, GPIO_PULL_UP);

    int64_t start = gpio_now_us();
    while (gpio_read(handle, gpio) == 1) {
        if (gpio_now_us() - start > 100000) return result;
    }
    start = gpio_now_us();
    while (gpio_read(handle, gpio) == 0) {
        if (gpio_now_us() - start > 100000) return result;
    }
    start = gpio_now_us();
    while (gpio_read(handle, gpio) == 1) {
        if (gpio_now_us() - start > 100000) return result;
    }

    for (int i = 0; i < 40; i++) {
        start = gpio_now_us();
        while (gpio_read(handle, gpio) == 0) {
            if (gpio_now_us() - start > 100000) return result;
        }
        int64_t bit_start = gpio_now_us();
        while (gpio_read(handle, gpio) == 1) {
            if (gpio_now_us() - bit_start > 100000) return result;
        }
        int bit_time = (int)(gpio_now_us() - bit_start);
        if (bit_time > 40) data[i / 8] |= (1 << (7 - (i % 8)));
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum == data[4]) {
        result.humidity = (float)data[0];
        result.temperature = (float)data[2];
        result.valid = true;
    }
    return result;
}
#endif
