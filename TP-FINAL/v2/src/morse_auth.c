#include "morse_auth.h"
#include <stdio.h>
#include <string.h>

/*
 * Maquina de estados Morse dirigida por tiempo (v2).
 *
 * Diferencias clave frente al v1:
 *   - El v1 recibia una "duracion" que el hilo siempre pasaba como 0, por lo
 *     que en hardware real jamas se podia generar una raya. Ademas, tras el
 *     primer simbolo saltaba a SEQUENCE_COMPLETE sin retorno, por lo que
 *     codigos multi-simbolo como "..--" eran inalcanzables (el test unitario
 *     lo demostraba fallando).
 *   - Aqui la funcion recibe el instante monotono actual (now_ms) y detecta
 *     flancos por si misma. Mide la duracion real de cada pulsacion y acumula
 *     simbolos hasta que un silencio > INTER_SYMBOL_TIMEOUT_MS cierra la
 *     secuencia. Asi se implementan de verdad los timeouts del plan.
 */

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

static void append_symbol(morse_context_t* ctx, char sym) {
    if (ctx->buffer_pos < MORSE_BUFFER_SIZE - 1) {
        ctx->buffer[ctx->buffer_pos++] = sym;
        ctx->buffer[ctx->buffer_pos] = '\0';
    }
}

void morse_auth_init(morse_context_t* ctx) {
    if (!ctx) return;
    ctx->state = MORSE_STATE_IDLE;
    ctx->buffer_pos = 0;
    ctx->buffer[0] = '\0';
    ctx->button_pressed = false;
    ctx->press_start_ms = 0;
    ctx->last_release_ms = 0;
}

void morse_auth_reset(morse_context_t* ctx) {
    morse_auth_init(ctx);
}

void morse_auth_update(morse_context_t* ctx, bool pressed, uint32_t now_ms) {
    if (!ctx) return;

    bool rising  = (pressed && !ctx->button_pressed);
    bool falling = (!pressed && ctx->button_pressed);

    switch (ctx->state) {
        case MORSE_STATE_IDLE:
        case MORSE_STATE_GAP:
            if (rising) {
                ctx->press_start_ms = now_ms;
                ctx->state = MORSE_STATE_PRESSED;
            } else if (ctx->state == MORSE_STATE_GAP && ctx->buffer_pos > 0) {
                /* Silencio prolongado tras al menos un simbolo -> fin de secuencia. */
                if (now_ms - ctx->last_release_ms >= INTER_SYMBOL_TIMEOUT_MS) {
                    ctx->state = MORSE_STATE_SEQUENCE_COMPLETE;
                }
            }
            break;

        case MORSE_STATE_PRESSED: {
            uint32_t held = now_ms - ctx->press_start_ms;
            if (held > STUCK_FINGER_TIMEOUT_MS) {
                /* Dedo trabado: descartar la secuencia por seguridad. */
                ctx->state = MORSE_STATE_STUCK_FINGER;
            } else if (falling) {
                if (held >= MORSE_DASH_MIN_MS) {
                    append_symbol(ctx, '-');
                    printf("[MORSE] Symbol: - (held=%ums)\n", held);
                } else if (held >= MORSE_MIN_TOUCH_MS && held <= MORSE_DOT_MAX_MS) {
                    append_symbol(ctx, '.');
                    printf("[MORSE] Symbol: . (held=%ums)\n", held);
                } else if (held > 0) {
                    printf("[MORSE] Ignored (held=%ums < min)\n", held);
                }
                ctx->last_release_ms = now_ms;
                ctx->state = MORSE_STATE_GAP;
            }
            break;
        }

        case MORSE_STATE_STUCK_FINGER:
            if (falling) {
                morse_auth_reset(ctx);
            }
            break;

        case MORSE_STATE_SEQUENCE_COMPLETE:
            /* El consumidor debe leer la secuencia y llamar a morse_auth_reset(). */
            break;
    }

    ctx->button_pressed = pressed;
}

const char* morse_auth_get_sequence(morse_context_t* ctx) {
    if (!ctx) return "";
    return ctx->buffer;
}

bool morse_auth_validate(const char* sequence, const char* user_id) {
    if (!sequence || !user_id) return false;

    for (int i = 0; i < num_users; i++) {
        if (strcmp(sequence, users[i].code) == 0 &&
            strcmp(user_id, users[i].user_id) == 0) {
            return true;
        }
    }
    return false;
}

const char* morse_auth_lookup_user(const char* sequence) {
    if (!sequence) return NULL;
    for (int i = 0; i < num_users; i++) {
        if (strcmp(sequence, users[i].code) == 0) {
            return users[i].user_id;
        }
    }
    return NULL;
}
