#ifndef LOGGER_H
#define LOGGER_H

#include "config.h"

int logger_init(void);
int logger_log(log_channel_t channel, const char* format, ...);
void logger_close(void);
const char* logger_get_pipe_path(log_channel_t channel);

#endif