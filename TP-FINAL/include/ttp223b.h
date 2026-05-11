#ifndef TTP223B_H
#define TTP223B_H

#include <stdbool.h>

int ttp223b_init(int gpio);
bool ttp223b_is_pressed(int handle, int gpio);

#endif