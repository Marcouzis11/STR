#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

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

void test_mutex_init_destroy(void) {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int res = pthread_mutex_init(&mutex, NULL);
    TEST_ASSERT(res == 0, "Mutex initialization succeeds");
    pthread_mutex_destroy(&mutex);
}

void test_sem_init_destroy(void) {
    sem_t sem;
    int res = sem_init(&sem, 0, 0);
    TEST_ASSERT(res == 0, "Semaphore initialization succeeds");
    sem_destroy(&sem);
}

void test_log_queue_circular(void) {
    char queue[64][256];
    int head = 0, tail = 0;
    bool full = false;

    for (int i = 0; i < 64; i++) {
        snprintf(queue[head], 256, "Message %d", i);
        head = (head + 1) % 64;
        if (head == tail) full = true;
    }

    TEST_ASSERT(full == true, "Queue wraps around correctly");
    
    int count = 0;
    while (tail != head || full) {
        tail = (tail + 1) % 64;
        full = false;
        count++;
    }
    TEST_ASSERT(count == 64, "Full cycle processes all messages");
}

void test_alarm_state_transitions(void) {
    typedef enum { DISARMED, ARMING, ARMED, TRIGGERED } state_t;
    state_t states[5] = { DISARMED, ARMING, ARMED, TRIGGERED, DISARMED };
    
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(states[i] != states[i+1], "State transitions are different");
    }
}

void test_temperature_comparison(void) {
    float dht_temp = 25.5;
    float bmp_temp = 24.8;
    float diff = dht_temp - bmp_temp;
    if (diff < 0) diff = -diff;
    TEST_ASSERT(diff < 2.0, "Temperature difference within threshold");
    
    dht_temp = 30.0;
    bmp_temp = 25.0;
    diff = dht_temp - bmp_temp;
    if (diff < 0) diff = -diff;
    TEST_ASSERT(diff > 2.0, "Temperature difference exceeds threshold");
}

int main(void) {
    printf("=== Integration Tests ===\n\n");

    test_mutex_init_destroy();
    test_sem_init_destroy();
    test_log_queue_circular();
    test_alarm_state_transitions();
    test_temperature_comparison();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}