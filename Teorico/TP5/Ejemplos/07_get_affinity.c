// gcc -o app 07_get_affinity.c

#define _GNU_SOURCE
#include<stdio.h>
#include<sched.h>
//#include<stdlib.h>

#define PROCESS 0

int main()
{
    pid_t pid;
    cpu_set_t set;
    size_t s;
    int cpu;

    s = sched_getaffinity(PROCESS, sizeof(cpu_set_t), &set);
    if (s == -1)
        printf("error sched_getaffinity\n");

    printf("CPUs:");
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
        if (CPU_ISSET(cpu, &set))
            printf(" %d", cpu);
    printf("\n");

    return 0;
}
