#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float temperature;
    float humidity;
    bool valid;
} dht11_reading_t;

int dht11_init(int gpio_chip, int gpio_offset);
dht11_reading_t dht11_read(int handle);

#endif