#ifndef GPIO_HAL_H
#define GPIO_HAL_H

/*
 * Capa de abstraccion de hardware (HAL) para GPIO.
 *
 * Motivacion (ver RESUMEN.md): el codigo v1 llamaba a funciones tipo
 * `lgpio_open()`, `lgpio_write()`, `lgpio_read()`, que NO existen en la
 * libreria real `lgpio`. La API real usa nombres como lgGpiochipOpen(),
 * lgGpioClaimOutput(), lgGpioWrite(), lgGpioRead(). Ademas, ese acoplamiento
 * directo impedia compilar y probar el sistema fuera de la Raspberry Pi.
 *
 * Esta HAL centraliza TODO el acceso a hardware detras de una interfaz unica
 * con dos backends seleccionables en tiempo de compilacion:
 *
 *   - -DUSE_LGPIO  -> mapea a la API real de lgpio (para la Raspberry Pi 5).
 *   - (por defecto) -> backend de SIMULACION en memoria, que permite compilar,
 *                      ejecutar y verificar toda la logica de hilos, mutex,
 *                      semaforos, la maquina Morse y el logging en cualquier PC.
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Niveles de pull para entradas con bias configurable. */
typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_DOWN,
    GPIO_PULL_UP
} gpio_pull_t;

/* Reloj monotono en microsegundos, comun a ambos backends. */
static inline int64_t gpio_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

int  gpio_open(void);
void gpio_close(int handle);
int  gpio_claim_output(int handle, int gpio, int initial_level);
int  gpio_claim_input(int handle, int gpio);
int  gpio_set_pull(int handle, int gpio, gpio_pull_t pull);
int  gpio_write(int handle, int gpio, int level);
int  gpio_read(int handle, int gpio);

#endif /* GPIO_HAL_H */
