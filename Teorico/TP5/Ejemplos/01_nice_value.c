
#include <sys/resource.h>
#include <sched.h>
#include <stdio.h>

#define PROCESS 0
#define NICE_VALUE -5

int main()
{
    int which, nice, errno;
    id_t who;


    which = PRIO_PROCESS;
    who = PROCESS;
    nice = NICE_VALUE;

    if (setpriority(which, who, nice) == -1)
        printf("error setpriority()\n");

    errno = 0;
    nice = getpriority(which, who);
    if (nice == -1 && errno != 0)
        printf("error getpriority()\n");

    printf("nice value is %d\n", nice);
    
    while(1);

    return 0;
}
