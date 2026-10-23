/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Linux ELF binding only; standalone store.c has no loader dependency.
 */
#include "internal.h"
#include "doe_labels_native_private.h"

#include <stdlib.h>

_Static_assert(_Alignof(doe_label_entry) <= _Alignof(max_align_t),
               "native malloc must satisfy table alignment");

static void *native_allocate(void *state, const void *vtable, size_t size,
                             size_t alignment) {
    (void)state;
    (void)vtable;
    (void)alignment;
    return malloc(size);
}

static void native_deallocate(void *state, const void *vtable, void *memory,
                              size_t size, size_t alignment) {
    (void)state;
    (void)vtable;
    (void)size;
    (void)alignment;
    free(memory);
}

static doe_label_store native_store = {
    .allocator = {NULL, NULL, native_allocate, native_deallocate},
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

doe_label_status doe_labels_native_set(const void *handle,
                                       const unsigned char *data, size_t length) {
    return doe_label_store_set(&native_store, handle, data, length);
}

doe_label_status doe_labels_native_get(const void *handle, doe_label_view *out) {
    return doe_label_store_get(&native_store, handle, out);
}

doe_label_status doe_labels_native_remove(const void *handle) {
    return doe_label_store_remove(&native_store, handle);
}

/* The embedder must quiesce API/object/borrowed-view use before dlclose. Failure
 * at this irreversible boundary is fatal, not recoverable store destruction. */
__attribute__((destructor)) static void native_shutdown(void) {
    doe_label_status status = doe_labels_destroy_contents(&native_store);
    if (status != DOE_LABEL_OK) doe_labels_fatal("native shutdown", (int)status);
}
