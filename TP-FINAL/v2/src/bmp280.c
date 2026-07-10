#include "bmp280.h"

#ifndef USE_LGPIO
/* ---------------------------------------------------------------------------
 * Backend de SIMULACION: no hay bus I2C fuera de la Raspberry Pi.
 * Devuelve una lectura fija coherente para poder ejercitar el sistema.
 * ------------------------------------------------------------------------- */
int bmp280_init(int bus, uint8_t addr) { (void)bus; (void)addr; return 0; }

bmp280_reading_t bmp280_read(int handle) {
    (void)handle;
    bmp280_reading_t r = {.temperature = 24.3f, .pressure = 1013.0f, .valid = true};
    return r;
}

#else
/* ---------------------------------------------------------------------------
 * Backend REAL: BMP280 por I2C (linux/i2c-dev).
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define BMP280_REG_TEMP      0xFA
#define BMP280_REG_PRESS     0xF7
#define BMP280_REG_CTRL_MEAS 0xF4

typedef struct {
    uint16_t dig_T1; int16_t dig_T2, dig_T3;
    uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5,
                             dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

static bmp280_calib_t calibration;
static int i2c_fd = -1;
static int32_t t_fine; /* enlaza la compensacion de temperatura con la de presion */

int bmp280_init(int bus, uint8_t addr) {
    char filename[32];
    snprintf(filename, sizeof(filename), "/dev/i2c-%d", bus);

    i2c_fd = open(filename, O_RDWR);
    if (i2c_fd < 0) return -1;
    if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) { close(i2c_fd); i2c_fd = -1; return -1; }

    uint8_t config_data[2] = {BMP280_REG_CTRL_MEAS, 0x27}; /* oversampling x1, normal */
    if (write(i2c_fd, config_data, 2) != 2) { close(i2c_fd); i2c_fd = -1; return -1; }

    uint8_t calib_addr = 0x88, calib_data[24];
    if (write(i2c_fd, &calib_addr, 1) != 1)   { close(i2c_fd); i2c_fd = -1; return -1; }
    if (read(i2c_fd, calib_data, 24) != 24)   { close(i2c_fd); i2c_fd = -1; return -1; }

    calibration.dig_T1 = (uint16_t)(calib_data[0]  | (calib_data[1]  << 8));
    calibration.dig_T2 = (int16_t) (calib_data[2]  | (calib_data[3]  << 8));
    calibration.dig_T3 = (int16_t) (calib_data[4]  | (calib_data[5]  << 8));
    calibration.dig_P1 = (uint16_t)(calib_data[6]  | (calib_data[7]  << 8));
    calibration.dig_P2 = (int16_t) (calib_data[8]  | (calib_data[9]  << 8));
    calibration.dig_P3 = (int16_t) (calib_data[10] | (calib_data[11] << 8));
    calibration.dig_P4 = (int16_t) (calib_data[12] | (calib_data[13] << 8));
    calibration.dig_P5 = (int16_t) (calib_data[14] | (calib_data[15] << 8));
    calibration.dig_P6 = (int16_t) (calib_data[16] | (calib_data[17] << 8));
    calibration.dig_P7 = (int16_t) (calib_data[18] | (calib_data[19] << 8));
    calibration.dig_P8 = (int16_t) (calib_data[20] | (calib_data[21] << 8));
    calibration.dig_P9 = (int16_t) (calib_data[22] | (calib_data[23] << 8));

    return i2c_fd;
}

/* Temperatura en 0.01 C; ademas fija t_fine (variable de modulo). */
static int32_t bmp280_compensate_t(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calibration.dig_T1 << 1))) *
                    ((int32_t)calibration.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)calibration.dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)calibration.dig_T1))) >> 12) *
                    ((int32_t)calibration.dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

/* Presion en Pa (formato Q24.8: dividir por 256 para obtener Pa). Usa t_fine. */
static uint32_t bmp280_compensate_p(int32_t adc_P) {
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)calibration.dig_P6;
    var2 += (var1 * (int64_t)calibration.dig_P5) << 17;
    var2 += ((int64_t)calibration.dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)calibration.dig_P3) >> 8) +
           ((var1 * (int64_t)calibration.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calibration.dig_P1) >> 33;
    if (var1 == 0) return 0; /* evita division por cero */

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calibration.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calibration.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calibration.dig_P7) << 4);
    return (uint32_t)p;
}

bmp280_reading_t bmp280_read(int handle) {
    (void)handle;
    bmp280_reading_t result = {.valid = false, .temperature = 0.0f, .pressure = 0.0f};
    if (i2c_fd < 0) return result;

    uint8_t reg = BMP280_REG_TEMP, data[3];
    if (write(i2c_fd, &reg, 1) != 1) return result;
    if (read(i2c_fd, data, 3) != 3)  return result;
    int32_t adc_T = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    result.temperature = (float)bmp280_compensate_t(adc_T) / 100.0f;

    reg = BMP280_REG_PRESS;
    if (write(i2c_fd, &reg, 1) != 1) return result;
    if (read(i2c_fd, data, 3) != 3)  return result;
    int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    /* Pa -> hPa: /256 (Q24.8) y /100. */
    result.pressure = (float)bmp280_compensate_p(adc_P) / 25600.0f;

    result.valid = true;
    return result;
}
#endif /* USE_LGPIO */
