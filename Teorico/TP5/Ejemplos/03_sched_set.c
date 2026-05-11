// gcc -o app 03_sched_set.c

#include <sched.h>
#include <stdio.h>

#define PROCESS 0
#define PRIO 99

int main()
{
    int j, pol;
    struct sched_param sp;

    pol = SCHED_RR; // SCHED_OTHER, SCHED_FIFO, SCHED_RR

    sp.sched_priority = PRIO;

    if (sched_setscheduler(PROCESS, pol, &sp) == -1)
        printf("error sched_setscheduler\n");

    while(1);

    return 0;
}
