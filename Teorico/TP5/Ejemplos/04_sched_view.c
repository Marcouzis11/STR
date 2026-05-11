// gcc -o app 04_sched_view.c

#include <sched.h>
#include <stdio.h>
#include <sys/resource.h>

#define PROCESS 0

int main()
{
    int j, pol;
    struct sched_param sp;

    pol = sched_getscheduler(PROCESS);
    if (pol == -1)
        printf("error sched_getscheduler\n");
    if (sched_getparam(PROCESS, &sp) == -1)
        printf("error sched_getparam\n");

    printf("policy is ");
    switch(pol){
        case 0: printf("SCHED_OTHER\n");
                break;
        case 1: printf("SCHED_FIFO\n");
                break;
        case 2: printf("SCHED_RR\n");
                break;
    }
    
    printf("priority is %d\n", sp.sched_priority);
    printf("nice value is %d\n", getpriority(PRIO_PROCESS, PROCESS));

    while(1);

    return 0;
}
