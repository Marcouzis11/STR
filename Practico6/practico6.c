#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <lgpio.h> // [NUEVO] Librería moderna para la Raspberry Pi 5

// Definición de Pines físicos (Nomenclatura BCM)
#define PIN_DHT11 4      
#define PIN_VENTILADOR 17 

// Estados de la Máquina
typedef enum { REPOSO, ALERTA, VENTILACION } EstadoSistema;

// Variables Globales Compartidas
float temperatura_compartida = 0.0;
EstadoSistema estado_actual = REPOSO;
pthread_mutex_t mutex_temp; 

// Handle (Manejador) del chip de GPIO de la Pi 5
int gpio_handle;
uint64_t tiempo_inicio_sistema;

// =========================================================================
// FUNCIÓN AUXILIAR DE TIEMPO (Reemplaza a gpioTick)
// =========================================================================
uint64_t obtener_microsegundos() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000000ULL) + (ts.tv_nsec / 1000);
}

// =========================================================================
// DRIVER DEL DHT11 (Adaptado para lgpio y RP1)
// =========================================================================
float leer_dht11() {
    uint8_t datos[5] = {0, 0, 0, 0, 0};
    uint64_t tick_inicial;

    // Liberamos el pin por si quedó trabado en la lectura anterior
    lgGpioFree(gpio_handle, PIN_DHT11);

    // 1. MCU envía señal de inicio (Pull low por 18ms)
    lgGpioClaimOutput(gpio_handle, 0, PIN_DHT11, 0); 
    usleep(18000); 
    lgGpioWrite(gpio_handle, PIN_DHT11, 1);
    
    // 2. MCU pasa a escuchar
    lgGpioFree(gpio_handle, PIN_DHT11);
    lgGpioClaimInput(gpio_handle, 0, PIN_DHT11);

    // Esperar respuesta del sensor (Low 80us, High 80us)
    int intentos = 0;
    while(lgGpioRead(gpio_handle, PIN_DHT11) == 1 && intentos++ < 1000) usleep(1);
    intentos = 0;
    while(lgGpioRead(gpio_handle, PIN_DHT11) == 0 && intentos++ < 1000) usleep(1);
    intentos = 0;
    while(lgGpioRead(gpio_handle, PIN_DHT11) == 1 && intentos++ < 1000) usleep(1);

    // 3. Leer los 40 bits
    for(int i = 0; i < 40; i++) {
        intentos = 0;
        // Esperar a que el pulso suba
        while(lgGpioRead(gpio_handle, PIN_DHT11) == 0 && intentos++ < 1000) usleep(1);
        
        tick_inicial = obtener_microsegundos(); 
        
        // Esperar a que el pulso baje
        intentos = 0;
        while(lgGpioRead(gpio_handle, PIN_DHT11) == 1 && intentos++ < 1000) usleep(1);
        
        uint64_t duracion = obtener_microsegundos() - tick_inicial;

        // Deserialización: Si el pulso alto dura más de 50us, es un '1'. Si no, es un '0'.
        if(duracion > 50) {
            datos[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    // 4. Validar Checksum
    if(datos[4] == ((datos[0] + datos[1] + datos[2] + datos[3]) & 0xFF)) {
        return (float)datos[2] + (float)datos[3] / 10.0; // Devolver Temperatura
    } else {
        return -255.0; // Error de lectura / Ruido en el cable
    }
}

// =========================================================================
// TAREA A: ADQUISICIÓN DE DATOS (Alta Prioridad - SCHED_FIFO)
// =========================================================================
void* tarea_adquisicion(void* arg) {
    while(1) {
        float temp_local = leer_dht11(); // Lectura SIN bloquear el mutex

        if(temp_local != -255.0) {
            pthread_mutex_lock(&mutex_temp);
            temperatura_compartida = temp_local; 
            pthread_mutex_unlock(&mutex_temp);
        }
        
        sleep(1); 
    }
    return NULL;
}

// =========================================================================
// TAREA B: CONTROL LÓGICO (Prioridad Media - SCHED_FIFO)
// =========================================================================
void* tarea_control(void* arg) {
    uint64_t tick_alerta = 0;
    uint64_t tick_ventilacion = 0;

    while(1) {
        // Obtener temperatura seguro
        pthread_mutex_lock(&mutex_temp);
        float temp = temperatura_compartida;
        pthread_mutex_unlock(&mutex_temp);

        uint64_t tick_actual = obtener_microsegundos();

        // Máquina de Estados
        switch(estado_actual) {
            case REPOSO:
                if (temp > 30.0) {
                    estado_actual = ALERTA;
                    tick_alerta = tick_actual; 
                }
                break;

            case ALERTA:
                if (temp <= 30.0) {
                    estado_actual = REPOSO; 
                } 
                // 60 segundos = 60,000,000 microsegundos
                else if ((tick_actual - tick_alerta) >= 60000000) { 
                    estado_actual = VENTILACION;
                    lgGpioWrite(gpio_handle, PIN_VENTILADOR, 1); // Prender LED/Ventilador
                    tick_ventilacion = tick_actual;
                }
                break;

            case VENTILACION:
                // 120 segundos = 120,000,000 microsegundos
                if (temp < 25.0 || (tick_actual - tick_ventilacion) >= 120000000) {
                    estado_actual = REPOSO;
                    lgGpioWrite(gpio_handle, PIN_VENTILADOR, 0); // Apagar LED/Ventilador
                }
                break;
        }

        usleep(100000); 
    }
    return NULL;
}

// =========================================================================
// TAREA C: INTERFAZ Y DIAGNÓSTICO (Prioridad Baja - SCHED_OTHER)
// =========================================================================
void* tarea_interfaz(void* arg) {
    const char* nombres_estados[] = {"REPOSO", "ALERTA", "VENTILACIÓN"};

    while(1) {
        pthread_mutex_lock(&mutex_temp);
        float temp = temperatura_compartida;
        EstadoSistema estado_local = estado_actual;
        pthread_mutex_unlock(&mutex_temp);

        uint64_t uptime_segundos = (obtener_microsegundos() - tiempo_inicio_sistema) / 1000000;

        printf("[DIAGNÓSTICO] Uptime: %llus | Temp: %.1f°C | Estado: %s | Actuador: %s\n", 
               uptime_segundos, 
               temp, 
               nombres_estados[estado_local],
               (estado_local == VENTILACION) ? "ON" : "OFF");

        sleep(5); 
    }
    return NULL;
}

// =========================================================================
// CONFIGURAR HILOS
// =========================================================================
void configurar_hilo(pthread_t* hilo, void* (*funcion)(void*), int politica, int prioridad) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (politica == SCHED_FIFO) {
        struct sched_param param;
        param.sched_priority = prioridad;
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        pthread_attr_setschedparam(&attr, &param);
    }

    pthread_create(hilo, &attr, funcion, NULL);
    pthread_attr_destroy(&attr);
}

// =========================================================================
// MAIN
// =========================================================================
int main() {
    // Abrir el chip GPIO 4 (El que maneja los 40 pines en la Pi 5)
    gpio_handle = lgGpiochipOpen(4);
    if (gpio_handle < 0) {
        printf("Error al iniciar lgpio. ¿Ejecutaste con sudo?\n");
        return 1;
    }

    printf("Iniciando Sistema de Control de Clima (Optimizando para Raspberry Pi 5)...\n");

    // Configurar pin del ventilador/LED como salida y apagarlo
    lgGpioClaimOutput(gpio_handle, 0, PIN_VENTILADOR, 0);

    pthread_mutex_init(&mutex_temp, NULL);
    tiempo_inicio_sistema = obtener_microsegundos();

    pthread_t hilo_a, hilo_b, hilo_c;
    configurar_hilo(&hilo_a, tarea_adquisicion, SCHED_FIFO, 90);
    configurar_hilo(&hilo_b, tarea_control, SCHED_FIFO, 50);
    configurar_hilo(&hilo_c, tarea_interfaz, SCHED_OTHER, 0);

    pthread_join(hilo_c, NULL);

    lgGpiochipClose(gpio_handle);
    pthread_mutex_destroy(&mutex_temp);
    return 0;
}

