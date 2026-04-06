#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sched.h>

volatile bool sistema_activo = true;
volatile unsigned long long iteraciones_navegacion = 0;

// El recurso conflictivo
pthread_mutex_t recurso_compartido;

void simular_carga_cpu() {
    volatile double calculo = 1.0;
    for(int i = 0; i < 50000; i++) { calculo = calculo * 1.00001; }
}

// ---------------------------------------------------------
// HILO TELEMETRÍA (Baja - Prio 10)
// ---------------------------------------------------------
void* hilo_telemetria(void* arg) {
    printf("[TELEMETRÍA - Prio 10] Iniciando. Intentando bloquear el recurso...\n");
    pthread_mutex_lock(&recurso_compartido); 
    printf("[TELEMETRÍA - Prio 10] ¡Recurso bloqueado! Trabajando lentamente...\n");

    // Simula estar usando el recurso durante un tiempo
    for(int i = 0; i < 15; i++) {
        if(!sistema_activo) break;
        simular_carga_cpu();
        printf("[TELEMETRÍA - Prio 10] Usando el recurso...\n");
        usleep(100000); 
    }

    printf("[TELEMETRÍA - Prio 10] Trabajo terminado. Liberando recurso...\n");
    pthread_mutex_unlock(&recurso_compartido); 
    return NULL;
}

// ---------------------------------------------------------
// HILO ESTABILIDAD (Crítica - Prio 80)
// ---------------------------------------------------------
void* hilo_estabilidad(void* arg) {
    printf("[ESTABILIDAD - Prio 80] Iniciando. Necesito el recurso crítico...\n");
    
    // Intenta tomar la llave. Se quedará bloqueado, PERO le prestará su prioridad a Telemetría
    pthread_mutex_lock(&recurso_compartido); 
    
    printf("[ESTABILIDAD - Prio 80] ¡Recurso obtenido! Ejecutando tarea crítica...\n");
    pthread_mutex_unlock(&recurso_compartido);
    printf("[ESTABILIDAD - Prio 80] Tarea crítica completada.\n");
    return NULL;
}

// ---------------------------------------------------------
// HILO NAVEGACIÓN (Media - Prio 40)
// ---------------------------------------------------------
void* hilo_navegacion(void* arg) {
    printf("[NAVEGACIÓN - Prio 40] Iniciando bucle infinito. Consumiendo CPU al 100%%...\n");
    while(sistema_activo) {
        simular_carga_cpu();
        iteraciones_navegacion++;
    }
    printf("[NAVEGACIÓN - Prio 40] Detenido.\n");
    return NULL;
}

// ---------------------------------------------------------
// CONFIGURACIÓN DE AFINIDAD Y PRIORIDADES
// ---------------------------------------------------------
void configurar_hilo(pthread_t* hilo, void* (*funcion)(void*), int prioridad) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // Forzamos todo al Núcleo 0 para que compitan
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    struct sched_param param;
    param.sched_priority = prioridad;
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO); // SCHED_FIFO activado
    pthread_attr_setschedparam(&attr, &param);

    pthread_create(hilo, &attr, funcion, NULL);
    pthread_attr_destroy(&attr);
}

int main() {
    printf("--- Ensayo Caso C: Inversión de Prioridad (SOLUCIONADO) ---\n\n");
    
    // --- LA SOLUCIÓN: HERENCIA DE PRIORIDADES ---
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&recurso_compartido, &mutex_attr);
    // --------------------------------------------

    pthread_t t_estabilidad, t_navegacion, t_telemetria;

    // SECUENCIA AJUSTADA PARA FORZAR EL CHOQUE:
    
    // 1. Inicia Telemetría y se apropia del mutex
    configurar_hilo(&t_telemetria, hilo_telemetria, 10);
    usleep(500000); // Esperamos medio segundo

    // 2. Inicia Estabilidad. Al ver el mutex ocupado, se bloqueará temporalmente.
    // Al hacerlo, le transfiere instantáneamente su prioridad 80 a Telemetría.
    configurar_hilo(&t_estabilidad, hilo_estabilidad, 80);
    usleep(500000); // Esperamos medio segundo

    // 3. Inicia Navegación. 
    // Intentará acaparar la CPU, pero el Sistema Operativo no la dejará porque
    // Telemetría ahora está corriendo temporalmente con prioridad 80.
    configurar_hilo(&t_navegacion, hilo_navegacion, 40);

    // Observamos cómo el sistema se cura a sí mismo durante 5 segundos
    sleep(5);
    sistema_activo = false;

    // Forzamos el cierre limpiamente
    pthread_cancel(t_navegacion); // Cancelamos el bucle infinito de navegación
    
    pthread_join(t_telemetria, NULL);
    pthread_join(t_estabilidad, NULL); // Estabilidad ahora terminará exitosamente
    
    pthread_mutex_destroy(&recurso_compartido);
    pthread_mutexattr_destroy(&mutex_attr);

    printf("\n================================================\n");
    printf("Iteraciones de Navegación (Prio 40): %llu\n", iteraciones_navegacion);
    printf("¡El hilo Estabilidad (Prio 80) logró ejecutar su tarea exitosamente!\n");
    printf("================================================\n");

    return 0;
}