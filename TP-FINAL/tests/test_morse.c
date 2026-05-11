#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "morse_auth.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("[PASS] %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("[FAIL] %s\n", msg); \
        tests_failed++; \
    } \
} while(0)

void test_morse_idle_state(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    TEST_ASSERT(ctx.state == MORSE_STATE_IDLE, "Initial state is IDLE");
}

void test_morse_dot_detection(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    morse_auth_update(&ctx, true, 0);
    TEST_ASSERT(ctx.state == MORSE_STATE_DOT_DETECTED, "Press transitions to DOT_DETECTED");
    morse_auth_update(&ctx, false, 200);
    TEST_ASSERT(ctx.state == MORSE_STATE_SEQUENCE_COMPLETE, "Short release creates DOT");
}

void test_morse_dash_detection(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    morse_auth_update(&ctx, true, 0);
    TEST_ASSERT(ctx.state == MORSE_STATE_DOT_DETECTED, "Press starts as DOT");
    morse_auth_update(&ctx, false, 400);
    TEST_ASSERT(ctx.state == MORSE_STATE_SEQUENCE_COMPLETE, "Long release creates DASH");
}

void test_morse_sequence_buffer(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    
    morse_auth_update(&ctx, true, 0);
    morse_auth_update(&ctx, false, 150);
    
    morse_auth_update(&ctx, true, 0);
    morse_auth_update(&ctx, false, 150);
    
    morse_auth_update(&ctx, true, 0);
    morse_auth_update(&ctx, false, 400);
    
    const char* seq = morse_auth_get_sequence(&ctx);
    TEST_ASSERT(strcmp(seq, "..-") == 0, "Sequence buffer holds ..-");
}

void test_morse_admin_code(void) {
    TEST_ASSERT(morse_auth_validate("..--", "admin") == true, "Admin code ..-- is valid");
    TEST_ASSERT(morse_auth_validate(".-..", "operator") == true, "Operator code .-.. is valid");
    TEST_ASSERT(morse_auth_validate("...-", "guest") == true, "Guest code ...- is valid");
}

void test_morse_invalid_codes(void) {
    TEST_ASSERT(morse_auth_validate("....", "admin") == false, "Invalid code rejected");
    TEST_ASSERT(morse_auth_validate("..--", "guest") == false, "Wrong user rejected");
}

void test_morse_reset(void) {
    morse_context_t ctx;
    morse_auth_init(&ctx);
    
    morse_auth_update(&ctx, true, 0);
    morse_auth_update(&ctx, false, 150);
    
    morse_auth_reset(&ctx);
    TEST_ASSERT(ctx.state == MORSE_STATE_IDLE, "Reset returns to IDLE");
    TEST_ASSERT(ctx.buffer_pos == 0, "Reset clears buffer");
}

int main(void) {
    printf("=== Morse Authentication Tests ===\n\n");

    test_morse_idle_state();
    test_morse_dot_detection();
    test_morse_dash_detection();
    test_morse_sequence_buffer();
    test_morse_admin_code();
    test_morse_invalid_codes();
    test_morse_reset();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}