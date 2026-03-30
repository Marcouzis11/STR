#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <pigpio.h> // Usamos pigpio en lugar de wiringPi

// Definición de pines (pigpio usa la numeración BCM del procesador)
#define LED1_PIN 17 
#define LED2_PIN 27 
#define LED3_PIN 22 

// Función para sumar milisegundos a la estructura timespec de forma segura
void sumar_ms(struct timespec *ts, int ms) {
    ts->tv_nsec += ms * 1000000L;
    while (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

// --- TAREA 1: 100 ms (Prioridad Alta) ---
void* tarea_1(void* arg) {
    struct timespec proxima_activacion;
    clock_gettime(CLOCK_MONOTONIC, &proxima_activacion);

    while(1) {
        // Leer el estado actual del pin, invertirlo y escribirlo
        int estado = gpioRead(LED1_PIN);
        gpioWrite(LED1_PIN, !estado);
        
        // Calcular próxima activación y dormir el hilo
        sumar_ms(&proxima_activacion, 100);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &proxima_activacion, NULL);
    }
    return NULL;
}

// --- TAREA 2: 300 ms (Prioridad Media) ---
void* tarea_2(void* arg) {
    struct timespec proxima_activacion;
    clock_gettime(CLOCK_MONOTONIC, &proxima_activacion);

    while(1) {
        int estado = gpioRead(LED2_PIN);
        gpioWrite(LED2_PIN, !estado);
        
        sumar_ms(&proxima_activacion, 300);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &proxima_activacion, NULL);
    }
    return NULL;
}

// --- TAREA 3: 500 ms (Prioridad Baja) ---
void* tarea_3(void* arg) {
    struct timespec proxima_activacion;
    clock_gettime(CLOCK_MONOTONIC, &proxima_activacion);

    while(1) {
        int estado = gpioRead(LED3_PIN);
        gpioWrite(LED3_PIN, !estado);
        
        sumar_ms(&proxima_activacion, 500);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &proxima_activacion, NULL);
    }
    return NULL;
}

// --- FUNCIÓN PARA CONFIGURAR HILOS DE TIEMPO REAL ---
void crear_hilo_rt(pthread_t *hilo, void *(*funcion_tarea)(void *), int prioridad) {
    pthread_attr_t attr;
    struct sched_param param;

    pthread_attr_init(&attr);
    
    // Indicar que queremos definir nuestra propia política
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    
    // Configurar política FIFO (Tiempo Real)
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    
    // Asignar la prioridad
    param.sched_priority = prioridad;
    pthread_attr_setschedparam(&attr, &param);

    // Crear el hilo
    if (pthread_create(hilo, &attr, funcion_tarea, NULL) != 0) {
        perror("Error al crear el hilo. Verifica los permisos de superusuario (sudo).");
        exit(EXIT_FAILURE);
    }
    
    pthread_attr_destroy(&attr);
}

int main() {
    pthread_t hilo1, hilo2, hilo3;

    // 1. Inicializar la biblioteca pigpio
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Error al inicializar pigpio. Recuerda ejecutar con sudo.\n");
        return 1;
    }

    // 2. Configurar pines como salidas y apagarlos inicialmente
    gpioSetMode(LED1_PIN, PI_OUTPUT); gpioWrite(LED1_PIN, 0);
    gpioSetMode(LED2_PIN, PI_OUTPUT); gpioWrite(LED2_PIN, 0);
    gpioSetMode(LED3_PIN, PI_OUTPUT); gpioWrite(LED3_PIN, 0);

    printf("Iniciando sistema RT con prioridades usando pigpio...\n");

    // 3. Crear los hilos con sus respectivas prioridades (RMS)
    crear_hilo_rt(&hilo1, tarea_1, 90); // Tarea 100ms -> Máxima prioridad
    crear_hilo_rt(&hilo2, tarea_2, 80); // Tarea 300ms -> Prioridad media
    crear_hilo_rt(&hilo3, tarea_3, 70); // Tarea 500ms -> Mínima prioridad

    // 4. Esperar a que los hilos terminen
    pthread_join(hilo1, NULL);
    pthread_join(hilo2, NULL);
    pthread_join(hilo3, NULL);

    // Finalizar pigpio (nunca se alcanzará en este bucle infinito, pero es buena práctica)
    gpioTerminate();

    return 0;
}