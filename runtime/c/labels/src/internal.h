/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Private storage shared by independently allocated and native-static owners.
 */
#ifndef DOE_LABELS_INTERNAL_H
#define DOE_LABELS_INTERNAL_H

#include "doe_labels.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct doe_label_entry {
    uintptr_t key;
    unsigned char *data;
    size_t length;
} doe_label_entry;

struct doe_label_store {
    doe_label_allocator allocator;
    pthread_mutex_t mutex;
    doe_label_entry *entries;
    size_t capacity;
    size_t count;
    bool closing;
};

/* Exclusive destruction of contents, not control storage. Failure retains all
 * storage and leaves only closing-state rejection probes and retry available. */
doe_label_status doe_labels_destroy_contents(doe_label_store *store);
_Noreturn void doe_labels_fatal(const char *operation, int code);

#endif
