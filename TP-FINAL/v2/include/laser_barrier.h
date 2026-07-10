#ifndef LASER_BARRIER_H
#define LASER_BARRIER_H

#include <stdbool.h>

int laser_barrier_init(int gpio);
bool laser_barrier_is_broken(int handle, int gpio);

#endif