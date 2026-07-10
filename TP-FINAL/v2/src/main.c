#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include "config.h"
#include "thread_manager.h"
#include "logger.h"
#include "alarm.h"
#include "dht11.h"
#include "bmp280.h"
#include "hcsr04.h"
#include "laser_barrier.h"
#include "hall_sensor.h"
#include "ttp223b.h"
#include "morse_auth.h"

static volatile bool running = true;

static void signal_handler(int signum) {
    (void)signum;
    running = false;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("=== Rack Monitor System v1.0 ===\n");
    printf("Initializing...\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    system_context_t ctx;
    if (init_system_context(&ctx) != 0) {
        fprintf(stderr, "Failed to initialize system context\n");
        return EXIT_FAILURE;
    }

    if (alarm_leds_init() < 0) {
        fprintf(stderr, "Failed to initialize alarm LEDs\n");
        cleanup_system_context(&ctx);
        return EXIT_FAILURE;
    }

    alarm_set_state(ALARM_DISARMED);
    printf("System ready. Press Ctrl+C to exit.\n");

    pthread_t env_thread, sec_thread, morse_thread, alarm_th, logger_th;

    pthread_create(&env_thread, NULL, env_monitor_thread, &ctx);
    pthread_create(&sec_thread, NULL, security_thread, &ctx);
    pthread_create(&morse_thread, NULL, morse_auth_thread, &ctx);
    pthread_create(&alarm_th, NULL, alarm_thread, &ctx);
    pthread_create(&logger_th, NULL, logger_thread, &ctx);

    /* El hilo de seguridad recibe la maxima prioridad de tiempo real: su
     * latencia debe mantenerse muy por debajo de 500 ms. SCHED_FIFO requiere
     * privilegios (sudo); si falla, el sistema sigue con planificacion normal. */
    struct sched_param param;
    param.sched_priority = 80;
    if (pthread_setschedparam(sec_thread, SCHED_FIFO, &param) != 0) {
        fprintf(stderr, "[WARN] No se pudo fijar SCHED_FIFO en security_thread "
                        "(ejecutar con sudo para prioridad RT).\n");
    }
    param.sched_priority = 50;
    pthread_setschedparam(env_thread, SCHED_FIFO, &param);

    while (running) {
        usleep(100000);
    }

    printf("\nShutting down...\n");

    pthread_cancel(env_thread);
    pthread_cancel(sec_thread);
    pthread_cancel(morse_thread);
    pthread_cancel(alarm_th);
    pthread_cancel(logger_th);

    pthread_join(env_thread, NULL);
    pthread_join(sec_thread, NULL);
    pthread_join(morse_thread, NULL);
    pthread_join(alarm_th, NULL);
    pthread_join(logger_th, NULL);

    cleanup_system_context(&ctx);
    printf("System stopped.\n");
    return EXIT_SUCCESS;
}