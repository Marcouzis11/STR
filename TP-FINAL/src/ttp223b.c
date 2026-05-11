#include "ttp223b.h"
#include <lgpio.h>

int ttp223b_init(int gpio) {
    int handle = lgpio_open();
    if (handle < 0) return -1;
    lgpio_claim_input(handle, 0, gpio);
    return handle;
}

bool ttp223b_is_pressed(int handle, int gpio) {
    int level = lgpio_read(handle, 0, gpio);
    return (level == 1);
}