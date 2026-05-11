#include "alarm.h"
#include <lgpio.h>
#include <stdio.h>
#include <unistd.h>

static int alarm_handle = -1;
static alarm_state_t current_state = ALARM_DISARMED;

int alarm_leds_init(void) {
    alarm_handle = lgpio_open();
    if (alarm_handle < 0) return -1;

    lgpio_claim_output(alarm_handle, 0, LED_YELLOW_GPIO, 0);
    lgpio_claim_output(alarm_handle, 0, LED_GREEN_GPIO, 0);
    lgpio_claim_output(alarm_handle, 0, LED_RED_GPIO, 0);

    lgpio_write(alarm_handle, 0, LED_YELLOW_GPIO, 0);
    lgpio_write(alarm_handle, 0, LED_GREEN_GPIO, 1);
    lgpio_write(alarm_handle, 0, LED_RED_GPIO, 0);

    return alarm_handle;
}

void alarm_set_state(alarm_state_t state) {
    current_state = state;

    if (alarm_handle < 0) return;

    switch (state) {
        case ALARM_DISARMED:
            lgpio_write(alarm_handle, 0, LED_YELLOW_GPIO, 0);
            lgpio_write(alarm_handle, 0, LED_GREEN_GPIO, 1);
            lgpio_write(alarm_handle, 0, LED_RED_GPIO, 0);
            break;

        case ALARM_ARMED:
            lgpio_write(alarm_handle, 0, LED_YELLOW_GPIO, 0);
            lgpio_write(alarm_handle, 0, LED_GREEN_GPIO, 0);
            lgpio_write(alarm_handle, 0, LED_RED_GPIO, 1);
            break;

        case ALARM_TRIGGERED:
        case ALARM_SOUNDING:
            lgpio_write(alarm_handle, 0, LED_YELLOW_GPIO, 0);
            lgpio_write(alarm_handle, 0, LED_GREEN_GPIO, 0);
            lgpio_write(alarm_handle, 0, LED_RED_GPIO, 1);
            break;

        case ALARM_ARMING:
            lgpio_write(alarm_handle, 0, LED_YELLOW_GPIO, 1);
            lgpio_write(alarm_handle, 0, LED_GREEN_GPIO, 0);
            lgpio_write(alarm_handle, 0, LED_RED_GPIO, 0);
            break;
    }
}

alarm_state_t alarm_get_state(void) {
    return current_state;
}

void alarm_blink_led(int gpio, uint8_t times, uint32_t delay_ms) {
    if (alarm_handle < 0) return;

    for (uint8_t i = 0; i < times; i++) {
        lgpio_write(alarm_handle, 0, gpio, 1);
        usleep(delay_ms * 1000);
        lgpio_write(alarm_handle, 0, gpio, 0);
        usleep(delay_ms * 1000);
    }
}

void alarm_trigger_event(const char* event_name) {
    if (!event_name) return;

    printf("[ALARM] Event triggered: %s\n", event_name);

    if (alarm_handle >= 0) {
        alarm_blink_led(LED_RED_GPIO, 5, 100);
    }
}