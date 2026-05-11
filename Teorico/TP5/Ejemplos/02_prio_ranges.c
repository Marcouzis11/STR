// gcc -o app 02_prio_ranges.c

#include <sched.h>
#include <stdio.h>

int main()
{
    printf("\nPriority ranges for each policy:\n");
    printf("SCHED_OTHER: %d a %d\n", sched_get_priority_min(SCHED_OTHER), sched_get_priority_max(SCHED_OTHER));
    printf("SCHED_FIFO: %d a %d\n", sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(SCHED_FIFO));
    printf("SCHED_RR: %d a %d\n", sched_get_priority_min(SCHED_RR), sched_get_priority_max(SCHED_RR));

    return 0;
}
