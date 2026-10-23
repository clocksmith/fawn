/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Private build-bound storage. CONTRACT.md owns behavior and lifetime. */
#ifndef DOE_ERROR_SCOPES_INTERNAL_H
#define DOE_ERROR_SCOPES_INTERNAL_H

#include "doe_error_scopes.h"
#include <pthread.h>

enum doe_error_scope_state {
    DOE_ERROR_SCOPES_DEAD,
    DOE_ERROR_SCOPES_LIVE,
    DOE_ERROR_SCOPES_CLEANUP
};

typedef struct doe_error_scope_entry {
    uint32_t filter;
    uint32_t error_type;
    size_t length;
    unsigned char bytes[DOE_ERROR_SCOPE_MESSAGE_CAPACITY];
} doe_error_scope_entry;

struct doe_error_scope_stack {
    pthread_mutex_t mutex;
    doe_error_scope_entry scopes[DOE_ERROR_SCOPE_MAX_DEPTH];
    size_t depth;
    doe_error_scope_callback callback;
    void *userdata1;
    void *userdata2;
    enum doe_error_scope_state state;
};

#endif
