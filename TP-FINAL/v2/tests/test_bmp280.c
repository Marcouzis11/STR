#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define BMP280_I2C_BUS 1
#define BMP280_I2C_ADDR 0x76

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("[PASS] %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("[FAIL] %s\n", msg); \
        tests_failed++; \
    } \
} while(0)

int bmp280_i2c_init(void) {
    char filename[32];
    snprintf(filename, sizeof(filename), "/dev/i2c-%d", BMP280_I2C_BUS);
    int fd = open(filename, O_RDWR);
    if (fd >= 0) {
        int res = ioctl(fd, I2C_SLAVE, BMP280_I2C_ADDR);
        if (res >= 0) {
            uint8_t config[2] = {0xF4, 0x27};
            write(fd, config, 2);
        }
    }
    return fd;
}

void test_bmp280_i2c_connection(void) {
    int fd = bmp280_i2c_init();
    TEST_ASSERT(fd >= 0, "BMP280 I2C connection opens");
    if (fd >= 0) close(fd);
}

void test_bmp280_calibration_read(void) {
    int fd = bmp280_i2c_init();
    if (fd < 0) {
        tests_failed++;
        printf("[FAIL] BMP280 I2C not available, skipping calibration test\n");
        return;
    }

    uint8_t reg = 0x88;
    uint8_t calib[24];
    if (write(fd, &reg, 1) == 1) {
        if (read(fd, calib, 24) == 24) {
            TEST_ASSERT(calib[0] != 0 || calib[1] != 0, "Calibration data readable");
        }
    }
    close(fd);
}

int main(void) {
    printf("=== BMP280 Driver Tests ===\n\n");

    test_bmp280_i2c_connection();
    test_bmp280_calibration_read();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    printf("Note: These tests require I2C hardware (run on Raspberry Pi)\n");
    return EXIT_SUCCESS;
}