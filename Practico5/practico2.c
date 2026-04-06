#define _GNU_SOURCE // Obligatorio para usar cpu_set_t
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <sched.h> // Para afinidad de CPU

#define MODO_TIEMPO_REAL false 

volatile unsigned long long iteraciones_estabilidad = 0;
volatile unsigned long long iteraciones_navegacion = 0;
volatile unsigned long long iteraciones_telemetria = 0;
volatile bool sistema_activo = true;

// Función de trabajo pesado
void simular_carga_cpu() {
    volatile double calculo = 1.0;
    for(int i = 0; i < 50000; i++) {
        calculo = calculo * 1.00001; 
    }
}

void* hilo_estabilidad(void* arg) {
    while(sistema_activo) {
        simular_carga_cpu();
        iteraciones_estabilidad++;
    }
    return NULL;
}

void* hilo_navegacion(void* arg) {
    while(sistema_activo) {
        simular_carga_cpu();
        iteraciones_navegacion++;
    }
    return NULL;
}

void* hilo_telemetria(void* arg) {
    sigset_t* set = (sigset_t*)arg;
    int sig;
    while(sistema_activo) {
        sigwait(set, &sig); 
        if (sistema_activo) {
            iteraciones_telemetria++;
            printf("[TELEMETRÍA] Datos enviados a tierra...\n"); 
        }
    }
    return NULL;
}

// ---------------------------------------------------------
// CONFIGURACIÓN DE HILOS Y AFINIDAD
// ---------------------------------------------------------
void configurar_hilo(pthread_t* hilo, void* (*funcion)(void*), int prioridad, void* arg) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // --- SOLUCIÓN PARA PLACAS MULTINÚCLEO ---
    // Creamos un "set" de CPUs y le decimos que SOLO use el CPU 0
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); 
    
    // Forzamos al hilo a correr únicamente en el Núcleo 0
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
    // -----------------------------------------

    if (MODO_TIEMPO_REAL) {
        struct sched_param param;
        param.sched_priority = prioridad;
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO); //
        pthread_attr_setschedparam(&attr, &param);
    }

    pthread_create(hilo, &attr, funcion, arg);
    pthread_attr_destroy(&attr);
}

int main() {
    printf("--- Simulador de Vuelo Iniciado ---\n");
    printf("Modo: %s\n", MODO_TIEMPO_REAL ? "Real-Time (SCHED_FIFO)" : "Normal (SCHED_OTHER)");

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, NULL); 

    timer_t timer_id;
    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    timer_create(CLOCK_REALTIME, &sev, &timer_id);

    struct itimerspec its;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 500000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 500000000;
    timer_settime(timer_id, 0, &its, NULL);

    pthread_t t_estabilidad, t_navegacion, t_telemetria;
    configurar_hilo(&t_estabilidad, hilo_estabilidad, 80, NULL); 
    configurar_hilo(&t_navegacion, hilo_navegacion, 40, NULL);   
    configurar_hilo(&t_telemetria, hilo_telemetria, 10, &set);   

    sleep(10);
    sistema_activo = false; 

    pthread_kill(t_telemetria, SIGALRM);

    pthread_join(t_estabilidad, NULL);
    pthread_join(t_navegacion, NULL);
    pthread_join(t_telemetria, NULL);

    printf("\n=== RESULTADOS DESPUÉS DE 10 SEGUNDOS ===\n");
    printf("%-15s | %-10s | %-15s\n", "TAREA", "PRIORIDAD", "ITERACIONES");
    printf("------------------------------------------------\n");
    printf("%-15s | %-10d | %llu\n", "Estabilidad", 80, iteraciones_estabilidad);
    printf("%-15s | %-10d | %llu\n", "Navegación", 40, iteraciones_navegacion);
    printf("%-15s | %-10d | %llu\n", "Telemetría", 10, iteraciones_telemetria);
    printf("================================================\n");

    return 0;
}