#ifndef CONFIG_H
#define CONFIG_H

/* Habilita clock_gettime/CLOCK_MONOTONIC de forma portable (no tocar __USE_*). */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#define DHT11_GPIO         4
#define BMP280_I2C_BUS     1
#define BMP280_I2C_ADDR    0x76

#define HCSR04_TRIG_GPIO   17
#define HCSR04_ECHO_GPIO   27

#define LASER_GPIO         22
#define HALL_GPIO          23
#define TTP223B_GPIO       24

#define LED_YELLOW_GPIO    5
#define LED_GREEN_GPIO     6
#define LED_RED_GPIO       13

#define ENV_READ_INTERVAL_SEC  5
#define SECURITY_POLL_US      50000

#define TEMP_DIFF_THRESHOLD    2.0
#define PRESSURE_MIN           800
#define PRESSURE_MAX           1100

#define MORSE_DOT_MAX_MS       350
#define MORSE_DASH_MIN_MS      250
#define INTER_SYMBOL_TIMEOUT_MS    500
#define SEQUENCE_TIMEOUT_MS     1500
#define STUCK_FINGER_TIMEOUT_MS 3000

#define MAX_LOG_LINE_LENGTH    256
#define MORSE_BUFFER_SIZE      32

typedef enum {
    ALARM_DISARMED,
    ALARM_ARMING,
    ALARM_ARMED,
    ALARM_TRIGGERED,
    ALARM_SOUNDING
} alarm_state_t;

typedef enum {
    LOG_ENV,
    LOG_USER,
    LOG_ALERT
} log_channel_t;

typedef struct {
    float dht11_temp;
    float dht11_humidity;
    float bmp280_temp;
    float bmp280_pressure;
    time_t timestamp;
} environmental_data_t;

typedef struct {
    bool laser_triggered;
    bool hall_triggered;
    bool ultrasonic_triggered;
    uint8_t ultrasonic_distance_cm;
    time_t last_event;
} security_status_t;

typedef enum {
    MORSE_STATE_IDLE,             /* sin pulsar y sin simbolos pendientes */
    MORSE_STATE_PRESSED,          /* boton presionado, midiendo duracion */
    MORSE_STATE_GAP,              /* boton liberado, esperando siguiente simbolo */
    MORSE_STATE_SEQUENCE_COMPLETE,/* secuencia lista para validar */
    MORSE_STATE_STUCK_FINGER      /* pulsacion continua sospechosa (>3s) */
} morse_state_t;

typedef struct {
    morse_state_t state;
    char buffer[MORSE_BUFFER_SIZE];
    uint8_t buffer_pos;
    bool button_pressed;      /* nivel anterior, para deteccion de flancos */
    uint32_t press_start_ms;  /* instante del ultimo flanco ascendente */
    uint32_t last_release_ms; /* instante del ultimo flanco descendente */
} morse_context_t;

typedef struct {
    alarm_state_t state;
    time_t last_change;
    uint8_t blink_counter;
} alarm_context_t;

typedef struct {
    char timestamp[32];
    log_channel_t channel;
    char message[MAX_LOG_LINE_LENGTH];
} log_entry_t;

#endif