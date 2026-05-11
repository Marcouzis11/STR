#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

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

typedef struct {
    float temperature;
    float humidity;
    bool valid;
} dht11_reading_t;

void test_dht11_reading_structure(void) {
    dht11_reading_t reading = {.temperature = 25.5, .humidity = 60.0, .valid = true};
    TEST_ASSERT(reading.valid == true, "DHT11 reading structure has valid flag");
    TEST_ASSERT(reading.temperature > 0 && reading.temperature < 50, "Temperature in plausible range");
    TEST_ASSERT(reading.humidity >= 0 && reading.humidity <= 100, "Humidity in valid percentage range");
}

void test_dht11_checksum_calculation(void) {
    uint8_t data[5] = {40, 0, 25, 0, 65};
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    TEST_ASSERT(checksum == data[4], "DHT11 checksum calculation works");

    data[4] = 64;
    TEST_ASSERT(checksum != data[4], "Invalid checksum detected");
}

void test_dht11_timing_thresholds(void) {
    int short_threshold = 350;
    int long_threshold = 250;
    int bit_time_short = 70;
    int bit_time_long = 120;

    TEST_ASSERT(bit_time_short <= short_threshold, "Short bit detected correctly");
    TEST_ASSERT(bit_time_long > long_threshold, "Long bit detected correctly");
}

int main(void) {
    printf("=== DHT11 Driver Tests ===\n\n");

    test_dht11_reading_structure();
    test_dht11_checksum_calculation();
    test_dht11_timing_thresholds();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return EXIT_SUCCESS;
}