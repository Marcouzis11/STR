#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float temperature;
    float pressure;
    bool valid;
} bmp280_reading_t;

int bmp280_init(int bus, uint8_t addr);
bmp280_reading_t bmp280_read(int handle);

#endif