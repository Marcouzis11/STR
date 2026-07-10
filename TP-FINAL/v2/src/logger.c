#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define LOGS_DIR "logs"

typedef struct {
    log_channel_t channel;
    const char* file_path;
} log_channel_info_t;

/* Indexado por log_channel_t (LOG_ENV=0, LOG_USER=1, LOG_ALERT=2). */
static const log_channel_info_t channels[3] = {
    {LOG_ENV,   LOGS_DIR "/environmental.log"},
    {LOG_USER,  LOGS_DIR "/user_events.log"},
    {LOG_ALERT, LOGS_DIR "/alerts.log"}
};

static FILE* log_files[3] = {NULL, NULL, NULL};

int logger_init(void) {
    mkdir(LOGS_DIR, 0755);

    for (int i = 0; i < 3; i++) {
        log_files[i] = fopen(channels[i].file_path, "a");
        if (!log_files[i]) {
            perror("Failed to open log file");
            return -1;
        }
    }

    return 0;
}

int logger_log(log_channel_t channel, const char* format, ...) {
    if (channel < 0 || channel > 2) return -1;

    FILE* f = log_files[channel];
    if (!f) return -1;

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(f, "[%s] ", timestamp);

    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);

    fprintf(f, "\n");
    fflush(f);

    return 0;
}

void logger_close(void) {
    for (int i = 0; i < 3; i++) {
        if (log_files[i]) {
            fclose(log_files[i]);
            log_files[i] = NULL;
        }
    }
}

const char* logger_get_pipe_path(log_channel_t channel) {
    if (channel < 0 || channel > 2) return NULL;
    return channels[channel].file_path;
}