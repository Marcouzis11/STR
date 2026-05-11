// gcc -o app 05_time_slice.c

#include <stdio.h>
#include <sched.h>

#define PROCESS 0

int main()
{
    struct timespec ts;
    int ret;

    ret = sched_rr_get_interval(PROCESS, &ts);

    printf("timeslice is %lu.%lu\n", ts.tv_sec, ts.tv_nsec);

    return 0;
}

