/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Reusable copied-byte owner. Retained semantics and original provenance:
 * runtime/zig/src/native/support/doe_label_store.zig; see SOURCE_INPUTS.json.
 */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(DOE_LABELS_TEST_CAPACITY_LIMIT) && !defined(DOE_LABELS_TESTING)
#error "A reduced capacity is permitted only in an explicit test profile"
#endif

_Noreturn void doe_labels_fatal(const char *operation, int code) {
    fprintf(stderr, "doe labels: %s failed (code %d)\n", operation, code);
    abort();
}

static void lock_store(doe_label_store *store) {
    doe_host_result result = doe_mutex_lock(&store->mutex);
    if (result.status != DOE_HOST_OK) doe_labels_fatal("mutex lock", result.native_error);
}

static void unlock_store(doe_label_store *store) {
    doe_host_result result = doe_mutex_unlock(&store->mutex);
    if (result.status != DOE_HOST_OK) doe_labels_fatal("mutex unlock", result.native_error);
}

static void *allocate(const doe_label_allocator *allocator, size_t size,
                      size_t alignment) {
    void *memory = allocator->allocate(allocator->state, allocator->vtable,
                                       size, alignment);
    if (memory != NULL && (uintptr_t)memory % alignment != 0) {
        doe_labels_fatal("allocator alignment", 0);
    }
    return memory;
}

static void deallocate(const doe_label_allocator *allocator, void *memory,
                       size_t size, size_t alignment) {
    allocator->deallocate(allocator->state, allocator->vtable, memory,
                          size, alignment);
}

/* Capacity is a power of two, always with at least one empty slot. Mixing high
 * address bits avoids clustering aligned object tokens. Unsigned wrap is hash
 * arithmetic only; allocation and capacity arithmetic below never wrap. */
static size_t find_slot(const doe_label_entry *entries, size_t capacity,
                        uintptr_t key) {
    uintptr_t hash = key;
    hash ^= hash >> 16;
    hash *= (uintptr_t)UINT32_C(0x7feb352d);
    hash ^= hash >> 15;
    size_t slot = (size_t)hash & (capacity - 1);
    while (entries[slot].key != 0 && entries[slot].key != key) {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static doe_label_status next_capacity(size_t current, size_t *next) {
    if (current > SIZE_MAX / 2) return DOE_LABEL_LAYOUT_OVERFLOW;
    size_t capacity = current == 0 ? 8 : current * 2;
    if (capacity > SIZE_MAX / sizeof(doe_label_entry)) {
        return DOE_LABEL_LAYOUT_OVERFLOW;
    }
#if defined(DOE_LABELS_TESTING) && defined(DOE_LABELS_TEST_CAPACITY_LIMIT)
    if (capacity > DOE_LABELS_TEST_CAPACITY_LIMIT) {
        return DOE_LABEL_LAYOUT_OVERFLOW;
    }
#endif
    *next = capacity;
    return DOE_LABEL_OK;
}

doe_label_status doe_label_store_create(const doe_label_config *config,
                                         doe_label_store **out) {
    if (out == NULL) return DOE_LABEL_INVALID_ARGUMENT;
    *out = NULL;
    if (config == NULL) return DOE_LABEL_INVALID_ARGUMENT;
    if (config->abi_version != DOE_LABELS_ABI_VERSION) {
        return DOE_LABEL_UNSUPPORTED_VERSION;
    }
    doe_label_allocator allocator = config->allocator;
    if (allocator.allocate == NULL || allocator.deallocate == NULL) {
        return DOE_LABEL_INVALID_ARGUMENT;
    }
    doe_label_store *store = allocate(&allocator, sizeof(*store),
                                     _Alignof(doe_label_store));
    if (store == NULL) return DOE_LABEL_OUT_OF_MEMORY;
    store->allocator = allocator;
    store->entries = NULL;
    store->capacity = 0;
    store->count = 0;
    store->closing = false;
    doe_host_result result = doe_mutex_init(&store->mutex, false);
    if (result.status != DOE_HOST_OK) {
        deallocate(&allocator, store, sizeof(*store), _Alignof(doe_label_store));
        return DOE_LABEL_SYNCHRONIZATION_INIT;
    }
    *out = store;
    return DOE_LABEL_OK;
}

/* Called under the store mutex. Prepare all fallible resources before changing
 * an entry, so source aliases, borrowed pointers and failed growth are safe. */
static doe_label_status set_locked(doe_label_store *store, uintptr_t key,
                                   const unsigned char *data, size_t length) {
    size_t slot = store->capacity == 0 ? 0 :
        find_slot(store->entries, store->capacity, key);
    bool exists = store->capacity != 0 && store->entries[slot].key == key;
    size_t capacity = store->capacity;
    if (!exists && store->count >= capacity / 2) {
        doe_label_status status = next_capacity(capacity, &capacity);
        if (status != DOE_LABEL_OK) return status;
    }
    unsigned char *copy = allocate(&store->allocator, length,
                                   _Alignof(unsigned char));
    if (copy == NULL) return DOE_LABEL_OUT_OF_MEMORY;
    memcpy(copy, data, length);

    if (capacity != store->capacity) {
        size_t extent = capacity * sizeof(doe_label_entry);
        doe_label_entry *entries = allocate(&store->allocator, extent,
                                            _Alignof(doe_label_entry));
        if (entries == NULL) {
            deallocate(&store->allocator, copy, length, _Alignof(unsigned char));
            return DOE_LABEL_OUT_OF_MEMORY;
        }
        memset(entries, 0, extent);
        for (size_t i = 0; i < store->capacity; ++i) {
            doe_label_entry entry = store->entries[i];
            if (entry.key != 0) {
                entries[find_slot(entries, capacity, entry.key)] = entry;
            }
        }
        if (store->entries != NULL) {
            deallocate(&store->allocator, store->entries,
                       store->capacity * sizeof(doe_label_entry),
                       _Alignof(doe_label_entry));
        }
        store->entries = entries;
        store->capacity = capacity;
        slot = find_slot(entries, capacity, key);
    }
    doe_label_entry previous = store->entries[slot];
    store->entries[slot] = (doe_label_entry){key, copy, length};
    if (exists) {
        deallocate(&store->allocator, previous.data, previous.length,
                   _Alignof(unsigned char));
    } else {
        ++store->count;
    }
    return DOE_LABEL_OK;
}

doe_label_status doe_label_store_set(doe_label_store *store, const void *handle,
                                      const unsigned char *data, size_t length) {
    if (store == NULL) return DOE_LABEL_INVALID_ARGUMENT;
    if (store->closing) return DOE_LABEL_CLEANUP_REQUIRED;
    if (handle == NULL || data == NULL || length == 0) return DOE_LABEL_OK;
    lock_store(store);
    doe_label_status status = set_locked(store, (uintptr_t)handle, data, length);
    unlock_store(store);
    return status;
}

doe_label_status doe_label_store_get(doe_label_store *store, const void *handle,
                                      doe_label_view *out) {
    if (out == NULL || store == NULL) return DOE_LABEL_INVALID_ARGUMENT;
    if (store->closing) return DOE_LABEL_CLEANUP_REQUIRED;
    doe_label_view view = {NULL, 0};
    lock_store(store);
    if (handle != NULL && store->capacity != 0) {
        size_t slot = find_slot(store->entries, store->capacity, (uintptr_t)handle);
        if (store->entries[slot].key != 0) {
            view.data = store->entries[slot].data;
            view.length = store->entries[slot].length;
        }
    }
    unlock_store(store);
    *out = view;
    return DOE_LABEL_OK;
}

doe_label_status doe_label_store_remove(doe_label_store *store,
                                         const void *handle) {
    if (store == NULL) return DOE_LABEL_INVALID_ARGUMENT;
    if (store->closing) return DOE_LABEL_CLEANUP_REQUIRED;
    if (handle == NULL) return DOE_LABEL_OK;
    lock_store(store);
    if (store->capacity != 0) {
        size_t slot = find_slot(store->entries, store->capacity, (uintptr_t)handle);
        doe_label_entry previous = store->entries[slot];
        if (previous.key != 0) {
            deallocate(&store->allocator, previous.data, previous.length,
                       _Alignof(unsigned char));
            store->entries[slot] = (doe_label_entry){0};
            --store->count;
            /* Reinsert the following cluster rather than leaving tombstones.
             * No label allocation moves and no allocation is needed. */
            slot = (slot + 1) & (store->capacity - 1);
            while (store->entries[slot].key != 0) {
                doe_label_entry moved = store->entries[slot];
                store->entries[slot] = (doe_label_entry){0};
                size_t destination = find_slot(store->entries, store->capacity,
                                               moved.key);
                store->entries[destination] = moved;
                slot = (slot + 1) & (store->capacity - 1);
            }
        }
    }
    unlock_store(store);
    return DOE_LABEL_OK;
}

doe_label_status doe_labels_destroy_contents(doe_label_store *store) {
    store->closing = true;
    doe_host_result result = doe_mutex_destroy(&store->mutex);
    if (result.status != DOE_HOST_OK) return DOE_LABEL_CLEANUP_REQUIRED;
    for (size_t i = 0; i < store->capacity; ++i) {
        doe_label_entry entry = store->entries[i];
        if (entry.key != 0) {
            deallocate(&store->allocator, entry.data, entry.length,
                       _Alignof(unsigned char));
        }
    }
    if (store->entries != NULL) {
        deallocate(&store->allocator, store->entries,
                   store->capacity * sizeof(doe_label_entry),
                   _Alignof(doe_label_entry));
    }
    return DOE_LABEL_OK;
}

doe_label_status doe_label_store_destroy(doe_label_store *store) {
    if (store == NULL) return DOE_LABEL_OK;
    doe_label_status status = doe_labels_destroy_contents(store);
    if (status != DOE_LABEL_OK) return status;
    doe_label_allocator allocator = store->allocator;
    deallocate(&allocator, store, sizeof(*store), _Alignof(doe_label_store));
    return DOE_LABEL_OK;
}
