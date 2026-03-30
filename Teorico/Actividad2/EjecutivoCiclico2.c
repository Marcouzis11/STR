#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* Definiciones de constantes */
#define PERIODO_BASE_MS 100          /* Período base en milisegundos */
#define PERIODO_BASE_NS (PERIODO_BASE_MS * 1000000)  /* Período base en nanosegundos */

#define PERIODO_TAREA1_MS 100        /* Período de Tarea 1 */
#define PERIODO_TAREA2_MS 300        /* Período de Tarea 2 */
#define PERIODO_TAREA3_MS 500        /* Período de Tarea 3 */

/* Número de ciclos para cada tarea */
#define CICLOS_TAREA1 (PERIODO_TAREA1_MS / PERIODO_BASE_MS)
#define CICLOS_TAREA2 (PERIODO_TAREA2_MS / PERIODO_BASE_MS)
#define CICLOS_TAREA3 (PERIODO_TAREA3_MS / PERIODO_BASE_MS)

#define DURACION_TOTAL_MS 2000       /* Duración total de la aplicación en ms */
#define NUM_CICLOS (DURACION_TOTAL_MS / PERIODO_BASE_MS)

/* Variables globales para medir el tiempo */
static struct timespec tiempo_inicio;

/**
 * Función para obtener el tiempo transcurrido desde el inicio en milisegundos
 */
long obtener_tiempo_transcurrido(void)
{
    struct timespec tiempo_actual;
    long ms_transcurridos;
    
    clock_gettime(CLOCK_MONOTONIC, &tiempo_actual);
    
    /* Calcular diferencia en milisegundos */
    ms_transcurridos = (tiempo_actual.tv_sec - tiempo_inicio.tv_sec) * 1000;
    ms_transcurridos += (tiempo_actual.tv_nsec - tiempo_inicio.tv_nsec) / 1000000;
    
    return ms_transcurridos;
}

/**
 * Tarea 1: Se ejecuta cada 100 ms
 */
void tarea1(void)
{
    long tiempo = obtener_tiempo_transcurrido();
    printf("Tarea 1 ejecutada - Tiempo: %ld ms\n", tiempo);
}

/**
 * Tarea 2: Se ejecuta cada 300 ms
 */
void tarea2(void)
{
    long tiempo = obtener_tiempo_transcurrido();
    printf("  Tarea 2 ejecutada - Tiempo: %ld ms\n", tiempo);
}

/**
 * Tarea 3: Se ejecuta cada 500 ms
 */
void tarea3(void)
{
    long tiempo = obtener_tiempo_transcurrido();
    printf("    Tarea 3 ejecutada - Tiempo: %ld ms\n", tiempo);
}

/**
 * Función principal que implementa el ejecutivo cíclico
 */
int main(void)
{
    struct timespec periodo_base;
    int contador_ciclos = 0;
    
    /* Inicializar el tiempo de inicio */
    clock_gettime(CLOCK_MONOTONIC, &tiempo_inicio);
    
    printf("===========================================\n");
    printf("Ejecutivo Cíclico - Sistema de Tiempo Real\n");
    printf("===========================================\n");
    printf("Período base: %d ms\n", PERIODO_BASE_MS);
    printf("Tarea 1: cada %d ms\n", PERIODO_TAREA1_MS);
    printf("Tarea 2: cada %d ms\n", PERIODO_TAREA2_MS);
    printf("Tarea 3: cada %d ms\n", PERIODO_TAREA3_MS);
    printf("Duración total: %d ms\n", DURACION_TOTAL_MS);
    printf("===========================================\n\n");
    
    /* Configurar la estructura de tiempo para nanosleep */
    periodo_base.tv_sec = 0;
    periodo_base.tv_nsec = PERIODO_BASE_NS;
    
    /* Bucle principal del ejecutivo cíclico */
    while (contador_ciclos < NUM_CICLOS) {
        /* Ejecutar Tarea 1 en cada ciclo */
        if (contador_ciclos % CICLOS_TAREA1 == 0) {
            tarea1();
        }
        
        /* Ejecutar Tarea 2 cada 3 ciclos */
        if (contador_ciclos % CICLOS_TAREA2 == 0 && contador_ciclos != 0) {
            tarea2();
        }
        
        /* Ejecutar Tarea 3 cada 5 ciclos */
        if (contador_ciclos % CICLOS_TAREA3 == 0 && contador_ciclos != 0) {
            tarea3();
        }
        
        /* Esperar hasta el siguiente período */
        nanosleep(&periodo_base, NULL);
        
        contador_ciclos++;
    }
    
    printf("\n===========================================\n");
    printf("Ejecución completada\n");
    printf("===========================================\n");
    
    return EXIT_SUCCESS;
}