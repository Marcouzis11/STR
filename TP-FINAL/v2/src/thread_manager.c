#include "thread_manager.h"
#include "gpio_hal.h"
#include "dht11.h"
#include "bmp280.h"
#include "hcsr04.h"
#include "laser_barrier.h"
#include "hall_sensor.h"
#include "ttp223b.h"
#include "morse_auth.h"
#include "alarm.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define LOG_QUEUE_SIZE 64

/* ------------------------------------------------------------------ */
/* Inicializacion / limpieza del contexto compartido                  */
/* ------------------------------------------------------------------ */
int init_system_context(system_context_t* ctx) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(system_context_t));

    pthread_mutex_init(&ctx->mutex_env, NULL);
    pthread_mutex_init(&ctx->mutex_security, NULL);
    pthread_mutex_init(&ctx->mutex_alarm, NULL);
    pthread_mutex_init(&ctx->mutex_morse, NULL);
    pthread_mutex_init(&ctx->mutex_log_queue, NULL);

    /* sem_log arranca en 0: el logger se bloquea hasta que haya trabajo. */
    sem_init(&ctx->sem_log, 0, 0);
    sem_init(&ctx->sem_alarm, 0, 0);
    sem_init(&ctx->sem_morse_complete, 0, 0);

    ctx->alarm_state = ALARM_DISARMED;
    morse_auth_init(&ctx->morse_ctx);
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

/* ------------------------------------------------------------------ */
/* Pipeline de logging (productor / consumidor)                       */
/* ------------------------------------------------------------------ */
int log_enqueue(system_context_t* ctx, log_channel_t channel, const char* fmt, ...) {
    if (!ctx || !fmt) return -1;

    log_entry_t entry;
    entry.channel = channel;
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    bool queued = false;
    pthread_mutex_lock(&ctx->mutex_log_queue);
    uint8_t next = (ctx->log_queue_head + 1) % LOG_QUEUE_SIZE;
    if (next != ctx->log_queue_tail) { /* hay hueco (una ranura reservada) */
        ctx->log_queue[ctx->log_queue_head] = entry;
        ctx->log_queue_head = next;
        queued = true;
    }
    pthread_mutex_unlock(&ctx->mutex_log_queue);

    if (queued) sem_post(&ctx->sem_log); /* despierta al logger_thread */
    return queued ? 0 : -1;
}

void* logger_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[LOGGER] Thread started\n");
    logger_init();

    while (1) {
        sem_wait(&ctx->sem_log); /* bloquea sin consumir CPU hasta que haya datos */

        log_entry_t entry;
        bool have = false;
        pthread_mutex_lock(&ctx->mutex_log_queue);
        if (ctx->log_queue_head != ctx->log_queue_tail) {
            entry = ctx->log_queue[ctx->log_queue_tail];
            ctx->log_queue_tail = (ctx->log_queue_tail + 1) % LOG_QUEUE_SIZE;
            have = true;
        }
        pthread_mutex_unlock(&ctx->mutex_log_queue);

        /* La E/S a disco se hace FUERA del mutex: seccion critica minima. */
        if (have) logger_log(entry.channel, "%s", entry.message);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Hilo de monitoreo ambiental (periodico, 5 s)                       */
/* ------------------------------------------------------------------ */
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

            log_enqueue(ctx, LOG_ENV,
                "T_dht=%.1fC H=%.0f%% T_bmp=%.1fC P=%.1fhPa",
                dht.temperature, dht.humidity, bmp.temperature, bmp.pressure);

            float diff = dht.temperature - bmp.temperature;
            if (diff < 0) diff = -diff;
            if (diff > TEMP_DIFF_THRESHOLD) {
                log_enqueue(ctx, LOG_ALERT,
                    "TEMP_DIFF_EXCEEDED diff=%.1fC (umbral=%.1f)",
                    diff, (float)TEMP_DIFF_THRESHOLD);
            }
            if (bmp.pressure < PRESSURE_MIN || bmp.pressure > PRESSURE_MAX) {
                log_enqueue(ctx, LOG_ALERT,
                    "PRESSURE_OUT_OF_RANGE p=%.1fhPa", bmp.pressure);
            }
        }

        sleep(ENV_READ_INTERVAL_SEC);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Hilo de seguridad (polling 50 ms)                                  */
/* ------------------------------------------------------------------ */
void* security_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[SECURITY] Thread started (50ms polling)\n");

    int hcsr_handle = hcsr04_init(HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO);
    int laser_handle = laser_barrier_init(LASER_GPIO);
    int hall_handle = hall_sensor_init(HALL_GPIO);

    while (1) {
        uint8_t distance = hcsr04_read_distance(hcsr_handle, HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO);
        bool laser_broken = laser_barrier_is_broken(laser_handle, LASER_GPIO);
        bool hall_triggered = hall_sensor_detected(hall_handle, HALL_GPIO);
        bool ultrasonic = (distance < 10);

        pthread_mutex_lock(&ctx->mutex_security);
        ctx->security_status.ultrasonic_distance_cm = distance;
        ctx->security_status.ultrasonic_triggered = ultrasonic;
        ctx->security_status.laser_triggered = laser_broken;
        ctx->security_status.hall_triggered = hall_triggered;
        ctx->security_status.last_event = time(NULL);
        pthread_mutex_unlock(&ctx->mutex_security);

        /* Solo actuamos si la alarma esta armada. Seccion critica cortisima:
         * leemos/actualizamos el estado y salimos; NADA de esperas aqui. */
        if (laser_broken || hall_triggered || ultrasonic) {
            bool fire = false;
            pthread_mutex_lock(&ctx->mutex_alarm);
            if (ctx->alarm_state == ALARM_ARMED || ctx->alarm_state == ALARM_TRIGGERED) {
                ctx->alarm_state = ALARM_TRIGGERED;
                fire = true;
            }
            pthread_mutex_unlock(&ctx->mutex_alarm);

            if (fire) {
                if (laser_broken)
                    log_enqueue(ctx, LOG_ALERT, "LASER_BARRIER_TRIGGERED dist=%dcm", distance);
                if (hall_triggered)
                    log_enqueue(ctx, LOG_ALERT, "HALL_DOOR_OPEN");
                if (ultrasonic)
                    log_enqueue(ctx, LOG_ALERT, "ULTRASONIC_INTRUSION dist=%dcm", distance);
            }
        }

        usleep(SECURITY_POLL_US); /* 50 ms -> latencia << 500 ms */
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Hilo de autenticacion Morse (event-driven, poll 10 ms)             */
/* ------------------------------------------------------------------ */
void* morse_auth_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[MORSE_AUTH] Thread started\n");

    int ttp_handle = ttp223b_init(TTP223B_GPIO);

    while (1) {
        bool pressed = ttp223b_is_pressed(ttp_handle, TTP223B_GPIO);
        uint32_t now_ms = (uint32_t)(gpio_now_us() / 1000);

        char seq[MORSE_BUFFER_SIZE] = {0};
        bool complete = false;

        pthread_mutex_lock(&ctx->mutex_morse);
        morse_auth_update(&ctx->morse_ctx, pressed, now_ms);
        if (ctx->morse_ctx.state == MORSE_STATE_SEQUENCE_COMPLETE) {
            strncpy(seq, morse_auth_get_sequence(&ctx->morse_ctx), sizeof(seq) - 1);
            morse_auth_reset(&ctx->morse_ctx);
            complete = true;
        }
        pthread_mutex_unlock(&ctx->mutex_morse);

        /* Procesamos la secuencia FUERA de mutex_morse para no anidar bloqueos. */
        if (complete) {
            const char* user = morse_auth_lookup_user(seq);
            printf("[MORSE] Sequence received: %s (%s)\n", seq, user ? user : "desconocido");

            if (user && strcmp(user, "admin") == 0) {
                pthread_mutex_lock(&ctx->mutex_alarm);
                if (ctx->alarm_state == ALARM_DISARMED) {
                    ctx->alarm_state = ALARM_ARMED;
                    alarm_set_state(ALARM_ARMED);
                    pthread_mutex_unlock(&ctx->mutex_alarm);
                    log_enqueue(ctx, LOG_USER, "ADMIN_ARMED code=%s", seq);
                } else {
                    ctx->alarm_state = ALARM_DISARMED;
                    alarm_set_state(ALARM_DISARMED);
                    pthread_mutex_unlock(&ctx->mutex_alarm);
                    log_enqueue(ctx, LOG_USER, "ADMIN_DISARMED code=%s", seq);
                }
                sem_post(&ctx->sem_morse_complete);
            } else if (user) {
                log_enqueue(ctx, LOG_USER, "LOGIN user=%s code=%s", user, seq);
            } else {
                log_enqueue(ctx, LOG_USER, "INVALID_CODE code=%s", seq);
            }
        }

        usleep(10000); /* 10 ms: buena resolucion de flancos para punto/raya */
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Hilo de alarma (actuadores). Copia el estado bajo mutex y parpadea */
/* FUERA de la seccion critica (v1 dormia ~1s con mutex_alarm tomado). */
/* ------------------------------------------------------------------ */
void* alarm_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    printf("[ALARM] Thread started\n");

    while (1) {
        pthread_mutex_lock(&ctx->mutex_alarm);
        alarm_state_t state = ctx->alarm_state;
        pthread_mutex_unlock(&ctx->mutex_alarm);

        if (state == ALARM_TRIGGERED) {
            alarm_blink_led(LED_RED_GPIO, 3, 200);
        } else if (state == ALARM_ARMING) {
            alarm_blink_led(LED_YELLOW_GPIO, 5, 100);
        }

        usleep(50000);
    }
    return NULL;
}
