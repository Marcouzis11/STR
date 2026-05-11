#include "hcsr04.h"
#include <lgpio.h>
#include <stdio.h>
#include <unistd.h>

int hcsr04_init(int trig_gpio, int echo_gpio) {
    int handle = lgpio_open();
    if (handle < 0) return -1;

    lgpio_claim_output(handle, 0, trig_gpio, 0);
    lgpio_claim_input(handle, 0, echo_gpio);

    return handle;
}

uint8_t hcsr04_read_distance(int handle, int trig_gpio, int echo_gpio) {
    lgpio_write(handle, 0, trig_gpio, 1);
    usleep(10);
    lgpio_write(handle, 0, trig_gpio, 0);

    int64_t start = 0;
    int64_t end = 0;
    int timeout = 1000000;

    while (lgpio_read(handle, 0, echo_gpio) == 0) {
        start = (int64_t)lgpio_get_current_time();
        if (start < 0) return 255;
    }

    while (lgpio_read(handle, 0, echo_gpio) == 1) {
        end = (int64_t)lgpio_get_current_time();
        if (end < 0) return 255;
        if (end - start > timeout) break;
    }

    int64_t duration_us = end - start;
    uint8_t distance = (uint8_t)((duration_us * 343) / 20000);

    if (distance > 200) distance = 200;

    return distance;
}