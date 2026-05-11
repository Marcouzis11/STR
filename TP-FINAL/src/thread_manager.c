#include "thread_manager.h"
#include "dht11.h"
#include "bmp280.h"
#include "hcsr04.h"
#include "laser_barrier.h"
#include "hall_sensor.h"
#include "ttp223b.h"
#include "morse_auth.h"
#include "alarm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lgpio.h>

int init_system_context(system_context_t* ctx) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(system_context_t));

    pthread_mutex_init(&ctx->mutex_env, NULL);
    pthread_mutex_init(&ctx->mutex_security, NULL);
    pthread_mutex_init(&ctx->mutex_alarm, NULL);
    pthread_mutex_init(&ctx->mutex_morse, NULL);
    pthread_mutex_init(&ctx->mutex_log_queue, NULL);

    sem_init(&ctx->sem_log, 0, 0);
    sem_init(&ctx->sem_alarm, 0, 0);
    sem_init(&ctx->sem_morse_complete, 0, 0);

    ctx->env_data.timestamp = 0;
    ctx->alarm_state = ALARM_DISARMED;
    ctx->morse_ctx.state = MORSE_STATE_IDLE;
    ctx->morse_ctx.buffer_pos = 0;
    ctx->log_queue_head = 0;
    ctx->log_queue_tail = 0;
    ctx->log_queue_full = false;

    return 0;
}

void cleanup_system_context(system_context_t* ctx) {
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->mutex_env);
    pthread_mutex_destroy(&ctx->mutex_security);
    pthread_mutex_destroy(&ctx->mutex_alarm);
    pthread_mutex_destroy(&ctx->mutex_morse);
    pthread_mutex_destroy(&ctx->mutex_log_queue);
    sem_destroy(&ctx->sem_log);
    sem_destroy(&ctx->sem_alarm);
    sem_destroy(&ctx->sem_morse_complete);
}

void* env_monitor_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[ENV_MONITOR] Thread started (5s interval)\n");

    int dht_handle = dht11_init(0, DHT11_GPIO);
    int bmp_handle = bmp280_init(BMP280_I2C_BUS, BMP280_I2C_ADDR);

    while (1) {
        dht11_reading_t dht = dht11_read(dht_handle);
        bmp280_reading_t bmp = bmp280_read(bmp_handle);

        if (dht.valid && bmp.valid) {
            pthread_mutex_lock(&ctx->mutex_env);
            ctx->env_data.dht11_temp = dht.temperature;
            ctx->env_data.dht11_humidity = dht.humidity;
            ctx->env_data.bmp280_temp = bmp.temperature;
            ctx->env_data.bmp280_pressure = bmp.pressure;
            ctx->env_data.timestamp = time(NULL);
            pthread_mutex_unlock(&ctx->mutex_env);

            float diff = dht.temperature - bmp.temperature;
            if (diff < 0) diff = -diff;

            pthread_mutex_lock(&ctx->mutex_log_queue);
            if (diff > TEMP_DIFF_THRESHOLD) {
                printf("[ENV] ALERT: Temperature diff %.1f°C exceeds threshold\n", diff);
            }
            pthread_mutex_unlock(&ctx->mutex_log_queue);
        }

        sleep(ENV_READ_INTERVAL_SEC);
    }

    (void)dht_handle;
    (void)bmp_handle;
    return NULL;
}

void* security_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[SECURITY] Thread started (50ms polling)\n");

    int hcsr_handle = hcsr04_init(HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO);
    int laser_handle = laser_barrier_init(LASER_GPIO);
    int hall_handle = hall_sensor_init(HALL_GPIO);

    while (1) {
        bool event_triggered = false;
        char event_msg[128] = "";

        uint8_t distance = hcsr04_read_distance(hcsr_handle, HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO);
        bool laser_broken = laser_barrier_is_broken(laser_handle, LASER_GPIO);
        bool hall_triggered = hall_sensor_detected(hall_handle, HALL_GPIO);

        pthread_mutex_lock(&ctx->mutex_security);
        ctx->security_status.ultrasonic_distance_cm = distance;
        ctx->security_status.ultrasonic_triggered = (distance < 10);
        ctx->security_status.laser_triggered = laser_broken;
        ctx->security_status.hall_triggered = hall_triggered;
        ctx->security_status.last_event = time(NULL);
        pthread_mutex_unlock(&ctx->mutex_security);

        if (laser_broken) {
            event_triggered = true;
            snprintf(event_msg, sizeof(event_msg), "LASER_BARRIER_TRIGGERED dist=%dcm", distance);
        }
        if (hall_triggered) {
            event_triggered = true;
            snprintf(event_msg, sizeof(event_msg), "HALL_DOOR_OPEN");
        }
        if (distance < 10) {
            event_triggered = true;
            snprintf(event_msg, sizeof(event_msg), "ULTRASONIC_INTRUSION dist=%dcm", distance);
        }

        if (event_triggered) {
            pthread_mutex_lock(&ctx->mutex_alarm);
            if (ctx->alarm_state == ALARM_ARMED || ctx->alarm_state == ALARM_TRIGGERED) {
                alarm_trigger_event(event_msg);
                ctx->alarm_state = ALARM_TRIGGERED;
            }
            pthread_mutex_unlock(&ctx->mutex_alarm);
        }

        usleep(SECURITY_POLL_US);
    }

    (void)hcsr_handle;
    (void)laser_handle;
    (void)hall_handle;
    return NULL;
}

void* morse_auth_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[MORSE_AUTH] Thread started\n");

    int ttp_handle = ttp223b_init(TTP223B_GPIO);

    while (1) {
        bool pressed = ttp223b_is_pressed(ttp_handle, TTP223B_GPIO);

        pthread_mutex_lock(&ctx->mutex_morse);
        morse_auth_update(&ctx->morse_ctx, pressed, 0);

        if (ctx->morse_ctx.state == MORSE_STATE_SEQUENCE_COMPLETE) {
            const char* seq = morse_auth_get_sequence(&ctx->morse_ctx);
            printf("[MORSE] Sequence received: %s\n", seq);

            if (morse_auth_validate(seq, "admin")) {
                pthread_mutex_lock(&ctx->mutex_alarm);
                if (ctx->alarm_state == ALARM_DISARMED) {
                    ctx->alarm_state = ALARM_ARMED;
                    alarm_set_state(ALARM_ARMED);
                    printf("[MORSE] Alarm ARMED by admin\n");
                } else if (ctx->alarm_state == ALARM_ARMED) {
                    ctx->alarm_state = ALARM_DISARMED;
                    alarm_set_state(ALARM_DISARMED);
                    printf("[MORSE] Alarm DISARMED by admin\n");
                }
                pthread_mutex_unlock(&ctx->mutex_alarm);
            }

            morse_auth_reset(&ctx->morse_ctx);
        }
        pthread_mutex_unlock(&ctx->mutex_morse);

        usleep(10000);
    }

    (void)ttp_handle;
    return NULL;
}

void* alarm_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[ALARM] Thread started\n");

    while (1) {
        pthread_mutex_lock(&ctx->mutex_alarm);
        alarm_state_t state = ctx->alarm_state;

        if (state == ALARM_TRIGGERED) {
            alarm_blink_led(LED_RED_GPIO, 3, 200);
        } else if (state == ALARM_ARMING) {
            alarm_blink_led(LED_YELLOW_GPIO, 5, 100);
        }
        pthread_mutex_unlock(&ctx->mutex_alarm);

        usleep(50000);
    }

    return NULL;
}

void* logger_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[LOGGER] Thread started\n");

    logger_init();

    while (1) {
        sem_wait(&ctx->sem_log);

        pthread_mutex_lock(&ctx->mutex_log_queue);
        if (ctx->log_queue_full || ctx->log_queue_head != ctx->log_queue_tail) {
            log_entry_t entry = ctx->log_queue[ctx->log_queue_tail];
            logger_log(entry.channel, "%s", entry.message);
            ctx->log_queue_tail = (ctx->log_queue_tail + 1) % 64;
            ctx->log_queue_full = false;
        }
        pthread_mutex_unlock(&ctx->mutex_log_queue);

        usleep(10000);
    }

    logger_close();
    return NULL;
}