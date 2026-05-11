#include "laser_barrier.h"
#include <lgpio.h>

int laser_barrier_init(int gpio) {
    int handle = lgpio_open();
    if (handle < 0) return -1;
    lgpio_claim_input(handle, 0, gpio);
    return handle;
}

bool laser_barrier_is_broken(int handle, int gpio) {
    int level = lgpio_read(handle, 0, gpio);
    return (level == 0);
}