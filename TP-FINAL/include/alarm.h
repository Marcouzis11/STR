#ifndef ALARM_H
#define ALARM_H

#include "config.h"

int alarm_leds_init(void);
void alarm_set_state(alarm_state_t state);
alarm_state_t alarm_get_state(void);
void alarm_blink_led(int gpio, uint8_t times, uint32_t delay_ms);
void alarm_trigger_event(const char* event_name);

#endif