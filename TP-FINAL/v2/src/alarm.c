#include "alarm.h"
#include "gpio_hal.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static int alarm_handle = -1;
static alarm_state_t current_state = ALARM_DISARMED;

/*
 * El modulo de alarma es un actuador compartido: lo tocan el hilo de alarma
 * (parpadeos), el hilo Morse (armar/desarmar) y el hilo de seguridad
 * (disparo). Un mutex interno serializa TODOS los accesos al hardware para
 * evitar carreras sobre el handle/los pines. Es un mutex de grano fino y
 * seccion critica cortisima (solo escrituras GPIO), independiente de los
 * mutex de estado del sistema.
 */
static pthread_mutex_t gpio_mtx = PTHREAD_MUTEX_INITIALIZER;

static void set_leds(int yellow, int green, int red) {
    if (alarm_handle < 0) return;
    pthread_mutex_lock(&gpio_mtx);
    gpio_write(alarm_handle, LED_YELLOW_GPIO, yellow);
    gpio_write(alarm_handle, LED_GREEN_GPIO, green);
    gpio_write(alarm_handle, LED_RED_GPIO, red);
    pthread_mutex_unlock(&gpio_mtx);
}

int alarm_leds_init(void) {
    alarm_handle = gpio_open();
    if (alarm_handle < 0) return -1;

    gpio_claim_output(alarm_handle, LED_YELLOW_GPIO, 0);
    gpio_claim_output(alarm_handle, LED_GREEN_GPIO, 0);
    gpio_claim_output(alarm_handle, LED_RED_GPIO, 0);

    set_leds(0, 1, 0); /* verde = sistema desarmado */
    return alarm_handle;
}

void alarm_set_state(alarm_state_t state) {
    current_state = state;
    switch (state) {
        case ALARM_DISARMED:                 set_leds(0, 1, 0); break;
        case ALARM_ARMED:                    set_leds(0, 0, 1); break;
        case ALARM_TRIGGERED:
        case ALARM_SOUNDING:                 set_leds(0, 0, 1); break;
        case ALARM_ARMING:                   set_leds(1, 0, 0); break;
    }
}

alarm_state_t alarm_get_state(void) {
    return current_state;
}

void alarm_blink_led(int gpio, uint8_t times, uint32_t delay_ms) {
    if (alarm_handle < 0) return;
    for (uint8_t i = 0; i < times; i++) {
        pthread_mutex_lock(&gpio_mtx);
        gpio_write(alarm_handle, gpio, 1);
        pthread_mutex_unlock(&gpio_mtx);
        usleep(delay_ms * 1000);
        pthread_mutex_lock(&gpio_mtx);
        gpio_write(alarm_handle, gpio, 0);
        pthread_mutex_unlock(&gpio_mtx);
        usleep(delay_ms * 1000);
    }
}

void alarm_trigger_event(const char* event_name) {
    if (!event_name) return;
    /* Solo notifica; el parpadeo lo realiza alarm_thread FUERA de toda
     * seccion critica de estado (v1 parpadeaba ~1s reteniendo mutex_alarm). */
    printf("[ALARM] Event triggered: %s\n", event_name);
}
