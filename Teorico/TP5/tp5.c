#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sched.h>
#include <unistd.h>

#define PROCESO 0

void ejercicio1(void);
void ejercicio2(void);
void ejercicio3(void);
void ejercicio4(void);

void ejercicio1(void)
{
    int which, nice, errno;

    printf("\n=== Ejercicio 1: Nice Value ===\n");

    printf("a) Rango de nice values: -20 a 19\n");

    which = PRIO_PROCESS;
    nice = -5;

    printf("b) Cambiando nice value a %d...\n", nice);
    if (setpriority(which, PROCESO, nice) == -1) {
        perror("error setpriority()");
        return;
    }

    errno = 0;
    nice = getpriority(which, PROCESO);
    if (nice == -1 && errno != 0) {
        perror("error getpriority()");
        return;
    }

    printf("c) Nice value actual: %d\n", nice);
}

void ejercicio2(void)
{
    int pol;
    struct sched_param sp;

    printf("\n=== Ejercicio 2: Políticas de Scheduling ===\n");

    printf("a) Rangos de prioridad:\n");
    printf("   SCHED_OTHER: %d a %d\n", 
           sched_get_priority_min(SCHED_OTHER), 
           sched_get_priority_max(SCHED_OTHER));
    printf("   SCHED_FIFO:  %d a %d\n", 
           sched_get_priority_min(SCHED_FIFO), 
           sched_get_priority_max(SCHED_FIFO));
    printf("   SCHED_RR:   %d a %d\n", 
           sched_get_priority_min(SCHED_RR), 
           sched_get_priority_max(SCHED_RR));

    pol = SCHED_RR;
    sp.sched_priority = 99;

    printf("b) Cambiando a SCHED_RR con prioridad %d...\n", sp.sched_priority);
    if (sched_setscheduler(PROCESO, pol, &sp) == -1) {
        perror("error sched_setscheduler()");
        return;
    }

    pol = sched_getscheduler(PROCESO);
    sched_getparam(PROCESO, &sp);

    printf("c) Política actual: ");
    switch(pol) {
        case SCHED_OTHER: printf("SCHED_OTHER"); break;
        case SCHED_FIFO:  printf("SCHED_FIFO");  break;
        case SCHED_RR:   printf("SCHED_RR");    break;
        default:         printf("desconocida");  break;
    }
    printf("\n");
    printf("   Prioridad: %d\n", sp.sched_priority);
}

void ejercicio3(void)
{
    struct timespec ts;
    int ret;

    printf("\n=== Ejercicio 3: Time Slice ===\n");

    ret = sched_rr_get_interval(PROCESO, &ts);
    if (ret == -1) {
        perror("error sched_rr_get_interval()");
        return;
    }

    printf("Time slice actual: %ld.%09ld segundos\n", ts.tv_sec, ts.tv_nsec);
}

void ejercicio4(void)
{
    cpu_set_t set;
    int cpu;
    unsigned long mask;

    printf("\n=== Ejercicio 4: CPU Affinity ===\n");

    CPU_ZERO(&set);
    mask = 0x1;

    for (cpu = 0; mask > 0; cpu++, mask >>= 1)
        if (mask & 1)
            CPU_SET(cpu, &set);

    printf("a) Fijando CPU 0...\n");
    if (sched_setaffinity(PROCESO, sizeof(cpu_set_t), &set) == -1) {
        perror("error sched_setaffinity()");
        return;
    }

    CPU_ZERO(&set);
    if (sched_getaffinity(PROCESO, sizeof(cpu_set_t), &set) == -1) {
        perror("error sched_getaffinity()");
        return;
    }

    printf("b) CPUs disponibles: ");
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
        if (CPU_ISSET(cpu, &set))
            printf("%d ", cpu);
    printf("\n");
}

int main(int argc, char *argv[])
{
    int opcion;

    if (argc < 2) {
        printf("Uso: %s <ejercicio>\n", argv[0]);
        printf("  1: Ejercicio 1 - Nice Value\n");
        printf("  2: Ejercicio 2 - Políticas de Scheduling\n");
        printf("  3: Ejercicio 3 - Time Slice\n");
        printf("  4: Ejercicio 4 - CPU Affinity\n");
        printf("  0: Todos los ejercicios\n");
        return 1;
    }

    opcion = atoi(argv[1]);

    switch(opcion) {
        case 1: ejercicio1(); break;
        case 2: ejercicio2(); break;
        case 3: ejercicio3(); break;
        case 4: ejercicio4(); break;
        case 0:
            ejercicio1();
            ejercicio2();
            ejercicio3();
            ejercicio4();
            break;
        default:
            printf("Opción inválida\n");
            return 1;
    }

    printf("\n");

    while(opcion != 0 && (opcion == 1 || opcion == 2 || opcion == 4))
        pause();

    return 0;
}