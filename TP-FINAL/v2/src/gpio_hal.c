#include "gpio_hal.h"

#ifdef USE_LGPIO
/* ---------------------------------------------------------------------------
 * Backend REAL: libreria lgpio (Raspberry Pi 5 / chip RP1).
 * API real de lgpio (no la inventada del v1).
 * ------------------------------------------------------------------------- */
#include <lgpio.h>

int gpio_open(void) {
    /* gpiochip 4 es el RP1 en la Raspberry Pi 5; 0 en modelos anteriores. */
    int h = lgGpiochipOpen(4);
    if (h < 0) h = lgGpiochipOpen(0);
    return h;
}

void gpio_close(int handle) {
    if (handle >= 0) lgGpiochipClose(handle);
}

int gpio_claim_output(int handle, int gpio, int initial_level) {
    return lgGpioClaimOutput(handle, 0, gpio, initial_level);
}

int gpio_claim_input(int handle, int gpio) {
    return lgGpioClaimInput(handle, 0, gpio);
}

int gpio_set_pull(int handle, int gpio, gpio_pull_t pull) {
    int flags = 0;
    if (pull == GPIO_PULL_UP)   flags = LG_SET_PULL_UP;
    if (pull == GPIO_PULL_DOWN) flags = LG_SET_PULL_DOWN;
    /* Re-reclamar como entrada con el bias deseado. */
    return lgGpioClaimInput(handle, flags, gpio);
}

int gpio_write(int handle, int gpio, int level) {
    return lgGpioWrite(handle, gpio, level);
}

int gpio_read(int handle, int gpio) {
    return lgGpioRead(handle, gpio);
}

#else
/* ---------------------------------------------------------------------------
 * Backend de SIMULACION (por defecto).
 * Mantiene el estado de los pines en memoria para poder ejecutar y verificar
 * toda la logica del sistema (hilos, mutex, semaforos, Morse, logging) sin
 * hardware. Las entradas devuelven su nivel "de reposo" (sin intrusion) para
 * no disparar falsas alarmas durante una demo.
 * ------------------------------------------------------------------------- */
#include <string.h>

#define SIM_MAX_GPIO 64

static int  sim_level[SIM_MAX_GPIO];
static bool sim_is_output[SIM_MAX_GPIO];
static bool sim_initialized = false;

static void sim_init_once(void) {
    if (sim_initialized) return;
    for (int i = 0; i < SIM_MAX_GPIO; i++) {
        /* Reposo de sensores digitales: linea en alto (no disparado).
         * laser/hall consideran "disparado" = nivel 0, asi que 1 = tranquilo. */
        sim_level[i] = 1;
        sim_is_output[i] = false;
    }
    sim_initialized = true;
}

int gpio_open(void) {
    sim_init_once();
    return 1; /* handle ficticio valido */
}

void gpio_close(int handle) { (void)handle; }

int gpio_claim_output(int handle, int gpio, int initial_level) {
    (void)handle;
    if (gpio < 0 || gpio >= SIM_MAX_GPIO) return -1;
    sim_is_output[gpio] = true;
    sim_level[gpio] = initial_level;
    return 0;
}

int gpio_claim_input(int handle, int gpio) {
    (void)handle;
    if (gpio < 0 || gpio >= SIM_MAX_GPIO) return -1;
    sim_is_output[gpio] = false;
    return 0;
}

int gpio_set_pull(int handle, int gpio, gpio_pull_t pull) {
    (void)handle; (void)gpio; (void)pull;
    return 0;
}

int gpio_write(int handle, int gpio, int level) {
    (void)handle;
    if (gpio < 0 || gpio >= SIM_MAX_GPIO) return -1;
    sim_level[gpio] = level ? 1 : 0;
    return 0;
}

int gpio_read(int handle, int gpio) {
    (void)handle;
    if (gpio < 0 || gpio >= SIM_MAX_GPIO) return -1;
    return sim_level[gpio];
}

#endif /* USE_LGPIO */
