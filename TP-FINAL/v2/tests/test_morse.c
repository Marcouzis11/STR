#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "morse_auth.h"

/*
 * Tests de la maquina Morse v2 (dirigida por tiempo).
 * A diferencia del v1, aqui se pasa el instante monotono actual (now_ms) y la
 * maquina mide la duracion real de cada pulsacion y acumula simbolos hasta que
 * un silencio > INTER_SYMBOL_TIMEOUT_MS cierra la secuencia.
 */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { printf("[PASS] %s\n", msg); tests_passed++; } \
    else      { printf("[FAIL] %s\n", msg); tests_failed++; } \
} while(0)

/* Simula una pulsacion completa (flanco abajo -> arriba) y devuelve el t final. */
static uint32_t tap(morse_context_t* c, uint32_t t_press, uint32_t hold_ms) {
    morse_auth_update(c, true,  t_press);            /* flanco ascendente */
    morse_auth_update(c, false, t_press + hold_ms);  /* flanco descendente */
    return t_press + hold_ms;
}

void test_morse_idle_state(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    TEST_ASSERT(ctx.state == MORSE_STATE_IDLE, "Estado inicial es IDLE");
}

void test_morse_dot(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    tap(&ctx, 0, 100); /* 100ms <= 350 -> punto */
    TEST_ASSERT(strcmp(morse_auth_get_sequence(&ctx), ".") == 0, "Pulsacion corta genera '.'");
}

void test_morse_dash(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    tap(&ctx, 0, 300); /* 300ms >= 250 -> raya */
    TEST_ASSERT(strcmp(morse_auth_get_sequence(&ctx), "-") == 0, "Pulsacion larga genera '-'");
}

void test_morse_sequence_accumulates(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    uint32_t t = 0;
    t = tap(&ctx, t + 50, 100);   /* . */
    t = tap(&ctx, t + 50, 100);   /* . */
    t = tap(&ctx, t + 50, 300);   /* - */
    /* Silencio >= 500ms cierra la secuencia. */
    morse_auth_update(&ctx, false, t + INTER_SYMBOL_TIMEOUT_MS);
    TEST_ASSERT(ctx.state == MORSE_STATE_SEQUENCE_COMPLETE, "Silencio cierra la secuencia");
    TEST_ASSERT(strcmp(morse_auth_get_sequence(&ctx), "..-") == 0, "Acumula multi-simbolo '..-'");
}

void test_morse_admin_full(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    uint32_t t = 0;
    t = tap(&ctx, t + 50, 100);   /* . */
    t = tap(&ctx, t + 50, 100);   /* . */
    t = tap(&ctx, t + 50, 300);   /* - */
    t = tap(&ctx, t + 50, 300);   /* - */
    morse_auth_update(&ctx, false, t + INTER_SYMBOL_TIMEOUT_MS);
    const char* seq = morse_auth_get_sequence(&ctx);
    TEST_ASSERT(strcmp(seq, "..--") == 0, "Codigo admin '..--' se puede ingresar");
    TEST_ASSERT(morse_auth_validate(seq, "admin") == true, "'..--' valida como admin");
}

void test_morse_stuck_finger(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    morse_auth_update(&ctx, true, 0);
    morse_auth_update(&ctx, true, STUCK_FINGER_TIMEOUT_MS + 1); /* sigue presionado */
    TEST_ASSERT(ctx.state == MORSE_STATE_STUCK_FINGER, "Dedo trabado >3s se detecta");
    morse_auth_update(&ctx, false, STUCK_FINGER_TIMEOUT_MS + 100);
    TEST_ASSERT(ctx.state == MORSE_STATE_IDLE, "Al soltar, se reinicia a IDLE");
}

void test_morse_validate(void) {
    TEST_ASSERT(morse_auth_validate("..--", "admin") == true, "admin ..-- valido");
    TEST_ASSERT(morse_auth_validate(".-..", "operator") == true, "operator .-.. valido");
    TEST_ASSERT(morse_auth_validate("...-", "guest") == true, "guest ...- valido");
    TEST_ASSERT(morse_auth_validate("....", "admin") == false, "codigo inexistente rechazado");
    TEST_ASSERT(morse_auth_validate("..--", "guest") == false, "usuario incorrecto rechazado");
    TEST_ASSERT(strcmp(morse_auth_lookup_user("...-"), "guest") == 0, "lookup_user identifica guest");
}

void test_morse_reset(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    tap(&ctx, 0, 100);
    morse_auth_reset(&ctx);
    TEST_ASSERT(ctx.state == MORSE_STATE_IDLE, "Reset vuelve a IDLE");
    TEST_ASSERT(ctx.buffer_pos == 0, "Reset limpia el buffer");
}

int main(void) {
    printf("=== Morse Authentication Tests (v2) ===\n\n");

    test_morse_idle_state();
    test_morse_dot();
    test_morse_dash();
    test_morse_sequence_accumulates();
    test_morse_admin_full();
    test_morse_stuck_finger();
    test_morse_validate();
    test_morse_reset();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
