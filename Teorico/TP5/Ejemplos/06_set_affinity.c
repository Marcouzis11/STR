// gcc -o app 06_set_affinity.c

#define _GNU_SOURCE
#include<sched.h>
#include<stdio.h>
#include<stdlib.h>

#define PROCESS 0

int main()
{
    pid_t pid;
    cpu_set_t set;
    int cpu;
    unsigned long mask;

    mask = 0x1;

    CPU_ZERO(&set);

    for (cpu = 0; mask > 0; cpu++, mask >>= 1)
        if (mask & 1)
            CPU_SET(cpu, &set);

    if (sched_setaffinity(PROCESS, sizeof(set), &set) == -1)
        printf("error sched_setaffinity\n");

    while(1);

    return 0;
}