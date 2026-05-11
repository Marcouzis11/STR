#include "morse_auth.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char* code;
    const char* user_id;
} morse_user_t;

static const morse_user_t users[] = {
    {"..--", "admin"},
    {".-..", "operator"},
    {"...-", "guest"}
};

static const int num_users = sizeof(users) / sizeof(users[0]);

void morse_auth_init(morse_context_t* ctx) {
    if (!ctx) return;
    ctx->state = MORSE_STATE_IDLE;
    ctx->buffer_pos = 0;
    ctx->buffer[0] = '\0';
    ctx->button_pressed = false;
    ctx->last_press_time = 0;
}

void morse_auth_reset(morse_context_t* ctx) {
    if (!ctx) return;
    ctx->state = MORSE_STATE_IDLE;
    ctx->buffer_pos = 0;
    ctx->buffer[0] = '\0';
    ctx->button_pressed = false;
}

void morse_auth_update(morse_context_t* ctx, bool pressed, uint32_t press_duration_ms) {
    if (!ctx) return;

    switch (ctx->state) {
        case MORSE_STATE_IDLE:
            if (pressed) {
                ctx->last_press_time = press_duration_ms;
                ctx->state = MORSE_STATE_DOT_DETECTED;
            }
            break;

        case MORSE_STATE_DOT_DETECTED:
            if (pressed) {
                if (press_duration_ms > MORSE_DASH_MIN_MS) {
                    if (ctx->buffer_pos < MORSE_BUFFER_SIZE - 1) {
                        ctx->buffer[ctx->buffer_pos++] = '-';
                    }
                    ctx->state = MORSE_STATE_DASH_DETECTED;
                }
            } else {
                if (press_duration_ms <= MORSE_DOT_MAX_MS) {
                    if (ctx->buffer_pos < MORSE_BUFFER_SIZE - 1) {
                        ctx->buffer[ctx->buffer_pos++] = '.';
                    }
                }
                ctx->state = MORSE_STATE_SEQUENCE_COMPLETE;
            }
            break;

        case MORSE_STATE_DASH_DETECTED:
            if (!pressed) {
                ctx->state = MORSE_STATE_SEQUENCE_COMPLETE;
            } else if (press_duration_ms > STUCK_FINGER_TIMEOUT_MS) {
                ctx->state = MORSE_STATE_STUCK_FINGER;
            }
            break;

        case MORSE_STATE_STUCK_FINGER:
            if (!pressed) {
                morse_auth_reset(ctx);
            }
            break;

        case MORSE_STATE_SEQUENCE_COMPLETE:
            if (!pressed && ctx->buffer_pos > 0) {
                ctx->buffer[ctx->buffer_pos] = '\0';
            }
            break;
    }
}

const char* morse_auth_get_sequence(morse_context_t* ctx) {
    if (!ctx) return "";
    return ctx->buffer;
}

bool morse_auth_validate(const char* sequence, const char* user_id) {
    if (!sequence || !user_id) return false;

    for (int i = 0; i < num_users; i++) {
        if (strcmp(sequence, users[i].code) == 0) {
            if (strcmp(user_id, users[i].user_id) == 0) {
                return true;
            }
        }
    }
    return false;
}