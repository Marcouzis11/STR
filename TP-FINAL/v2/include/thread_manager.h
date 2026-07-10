#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include "config.h"

#define NUM_THREADS 5

typedef struct {
    pthread_t thread;
    char name[32];
    bool running;
    void* (*start_routine)(void*);
} thread_info_t;

typedef struct {
    pthread_mutex_t mutex_env;
    pthread_mutex_t mutex_security;
    pthread_mutex_t mutex_alarm;
    pthread_mutex_t mutex_morse;
    pthread_mutex_t mutex_log_queue;

    sem_t sem_log;
    sem_t sem_alarm;
    sem_t sem_morse_complete;

    environmental_data_t env_data;
    security_status_t security_status;
    alarm_state_t alarm_state;
    morse_context_t morse_ctx;
    log_entry_t log_queue[64];
    uint8_t log_queue_head;
    uint8_t log_queue_tail;
    bool log_queue_full;
} system_context_t;

int init_system_context(system_context_t* ctx);
void cleanup_system_context(system_context_t* ctx);

/* Productor thread-safe del pipeline de logging: encola un mensaje y avisa al
 * logger_thread via sem_log. Devuelve 0 si se encolo, -1 si la cola esta llena
 * (se descarta el mensaje mas nuevo para no bloquear a los productores). */
int log_enqueue(system_context_t* ctx, log_channel_t channel, const char* fmt, ...);

void* env_monitor_thread(void* arg);
void* security_thread(void* arg);
void* morse_auth_thread(void* arg);
void* alarm_thread(void* arg);
void* logger_thread(void* arg);

#endif