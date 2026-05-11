#include "dht11.h"
#include <lgpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DHT11_MAX_TRANSITIONS 85

static int64_t get_current_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

int dht11_init(int gpio_chip, int gpio_offset) {
    (void)gpio_chip;
    int handle = lgpio_open();
    if (handle < 0) return -1;
    lgpio_claim_output(handle, 0, gpio_offset, 0);
    return handle;
}

dht11_reading_t dht11_read(int handle) {
    dht11_reading_t result = {.valid = false, .temperature = 0.0f, .humidity = 0.0f};

    int gpio = DHT11_GPIO;
    uint8_t data[5] = {0};

    lgpio_write(handle, 0, gpio, 1);
    usleep(50000);
    lgpio_write(handle, 0, gpio, 0);
    usleep(20000);

    lgpio_change_mode(handle, 0, gpio, LG_SET_BIAS_PULL_DOWN);
    usleep(20);

    int64_t start = get_current_time_us();
    int64_t timeout = start + 100000;

    while (lgpio_read(handle, 0, gpio) == 1) {
        if (get_current_time_us() > timeout) return result;
    }

    start = get_current_time_us();
    timeout = start + 100000;
    while (lgpio_read(handle, 0, gpio) == 0) {
        if (get_current_time_us() > timeout) return result;
    }

    for (int i = 0; i < 40; i++) {
        start = get_current_time_us();
        timeout = start + 100000;
        while (lgpio_read(handle, 0, gpio) == 0) {
            if (get_current_time_us() > timeout) return result;
        }

        int64_t bit_start = get_current_time_us();
        timeout = bit_start + 100000;
        while (lgpio_read(handle, 0, gpio) == 1) {
            if (get_current_time_us() > timeout) return result;
        }

        int64_t bit_end = get_current_time_us();
        int bit_time = (int)(bit_end - bit_start);

        if (bit_time > 70) data[i / 8] |= (1 << (7 - (i % 8)));
    }

    lgpio_change_mode(handle, 0, gpio, LG_SET_BIAS_PULL_UP);

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum == data[4]) {
        result.humidity = (float)data[0];
        result.temperature = (float)data[2];
        result.valid = true;
    }

    return result;
}