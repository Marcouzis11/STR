// gcc -o app 08_set_get_affinity.c

#define _GNU_SOURCE
#include<sched.h>
#include<stdio.h>
#include<stdlib.h>

#define PROCESS 0
#define MASK 0x7

int main()
{
    pid_t pid;
    cpu_set_t set;
    int cpu;
    size_t s;
    unsigned long mask;
    
    // current affinity
    s = sched_getaffinity(PROCESS, sizeof(cpu_set_t), &set);
    if (s == -1)
        printf("error sched_getaffinity\n");
    printf("current affinity is\n");
    printf("CPUs:");
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
        if (CPU_ISSET(cpu, &set))
            printf(" %d", cpu);
    printf("\n");
    
    // change affinity
    mask = MASK;

    CPU_ZERO(&set);

    for (cpu = 0; mask > 0; cpu++, mask >>= 1)
        if (mask & 1)
            CPU_SET(cpu, &set);

    if (sched_setaffinity(PROCESS, sizeof(set), &set) == -1)
        printf("error sched_setaffinity\n");

    // new affinity 
    s = sched_getaffinity(PROCESS, sizeof(cpu_set_t), &set);
    if (s == -1)
        printf("error sched_getaffinity\n");
    printf("new affinity is\n");
    printf("CPUs:");
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
        if (CPU_ISSET(cpu, &set))
            printf(" %d", cpu);
    printf("\n");

    while(1);

    return 0;
}
