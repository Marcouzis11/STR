#ifndef MORSE_AUTH_H
#define MORSE_AUTH_H

#include "config.h"

void morse_auth_init(morse_context_t* ctx);
void morse_auth_reset(morse_context_t* ctx);
void morse_auth_update(morse_context_t* ctx, bool pressed, uint32_t now_ms);
const char* morse_auth_get_sequence(morse_context_t* ctx);
bool morse_auth_validate(const char* sequence, const char* user_id);
const char* morse_auth_lookup_user(const char* sequence);

#endif