/* SPDX-License-Identifier: MIT
 * Developer checks, separate from independently sealed acceptance cases.
 */
#include "doe_labels.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation {
    void *pointer;
    size_t size;
    size_t alignment;
} allocation;

typedef struct ledger {
    allocation live[4096];
    size_t requests;
    size_t fail_at;
    size_t count;
} ledger;

static void *tracked_allocate(void *state, const void *vtable, size_t size,
                              size_t alignment) {
    ledger *tracking = state;
    assert(vtable == tracking);
    ++tracking->requests;
    if (tracking->requests == tracking->fail_at) return NULL;
    void *pointer = malloc(size);
    assert(pointer != NULL);
    assert((uintptr_t)pointer % alignment == 0);
    assert(tracking->count < 4096);
    tracking->live[tracking->count++] = (allocation){pointer, size, alignment};
    return pointer;
}

static void tracked_deallocate(void *state, const void *vtable, void *pointer,
                               size_t size, size_t alignment) {
    ledger *tracking = state;
    assert(vtable == tracking);
    for (size_t i = 0; i < tracking->count; ++i) {
        if (tracking->live[i].pointer == pointer) {
            assert(tracking->live[i].size == size);
            assert(tracking->live[i].alignment == alignment);
            tracking->live[i] = tracking->live[--tracking->count];
            free(pointer);
            return;
        }
    }
    assert(!"free of unknown allocation");
}

static doe_label_store *create_store(ledger *tracking) {
    doe_label_config config = {DOE_LABELS_ABI_VERSION,
        {tracking, tracking, tracked_allocate, tracked_deallocate}};
    doe_label_store *store = NULL;
    assert(doe_label_store_create(&config, &store) == DOE_LABEL_OK);
    memset(&config, 0, sizeof(config));
    return store;
}

static doe_label_view view_of(doe_label_store *store, uintptr_t key) {
    doe_label_view view = {NULL, 0};
    assert(doe_label_store_get(store, (const void *)key, &view) == DOE_LABEL_OK);
    return view;
}

static void semantics(void) {
    ledger tracking = {0};
    doe_label_store *store = create_store(&tracking);
    unsigned char bytes[] = {0, 255, 128, 13, 0, 17};
    assert(doe_label_store_set(store, (void *)1, bytes, sizeof(bytes)) == DOE_LABEL_OK);
    doe_label_view first = view_of(store, 1);
    assert(first.data != bytes && first.length == sizeof(bytes));
    assert(memcmp(first.data, bytes, sizeof(bytes)) == 0);
    memset(bytes, 42, sizeof(bytes));
    assert(first.data[0] == 0 && first.data[1] == 255);
    size_t requests = tracking.requests;
    assert(doe_label_store_set(store, NULL, bytes, sizeof(bytes)) == DOE_LABEL_OK);
    assert(doe_label_store_set(store, (void *)1, NULL, SIZE_MAX) == DOE_LABEL_OK);
    assert(doe_label_store_set(store, (void *)1, bytes, 0) == DOE_LABEL_OK);
    assert(view_of(store, 1).data == first.data);
    assert(tracking.requests == requests);
    tracking.fail_at = requests + 1;
    assert(doe_label_store_set(store, (void *)1, bytes, sizeof(bytes)) == DOE_LABEL_OUT_OF_MEMORY);
    assert(view_of(store, 1).data == first.data);
    tracking.fail_at = 0;
    assert(doe_label_store_set(store, (void *)1, first.data + 1, 3) == DOE_LABEL_OK);
    doe_label_view aliased = view_of(store, 1);
    assert(aliased.length == 3 && aliased.data[0] == 255 && aliased.data[2] == 13);
    assert(doe_label_store_remove(store, (void *)1) == DOE_LABEL_OK);
    assert(view_of(store, 1).data == NULL);
    assert(doe_label_store_remove(store, (void *)1) == DOE_LABEL_OK);
    assert(doe_label_store_set(store, (void *)1, bytes, sizeof(bytes)) == DOE_LABEL_OK);
    assert(doe_label_store_destroy(store) == DOE_LABEL_OK);
    assert(tracking.count == 0);
}

static void growth_and_deletion(void) {
    ledger tracking = {0};
    doe_label_store *store = create_store(&tracking);
    const unsigned char *pointers[512];
    for (uintptr_t i = 1; i <= 512; ++i) {
        assert(doe_label_store_set(store, (void *)(i * 4096),
               (const unsigned char *)&i, sizeof(i)) == DOE_LABEL_OK);
        pointers[i - 1] = view_of(store, i * 4096).data;
    }
    for (uintptr_t i = 1; i <= 512; i += 2) {
        assert(doe_label_store_remove(store, (void *)(i * 4096)) == DOE_LABEL_OK);
    }
    for (uintptr_t i = 2; i <= 512; i += 2) {
        doe_label_view view = view_of(store, i * 4096);
        assert(view.data == pointers[i - 1]);
        assert(view.length == sizeof(i));
        assert(memcmp(view.data, &i, sizeof(i)) == 0);
    }
    for (uintptr_t i = 1; i <= 512; i += 2) {
        assert(view_of(store, i * 4096).data == NULL);
        assert(doe_label_store_set(store, (void *)(i * 4096),
               (const unsigned char *)&i, sizeof(i)) == DOE_LABEL_OK);
    }
    assert(doe_label_store_destroy(store) == DOE_LABEL_OK);
    assert(tracking.count == 0);
}

static void allocation_failures(void) {
    /* First insertion and the fifth insertion exercise label/table rollback.
     * The observed allocation count, rather than guessed ordinals, bounds each
     * failure sweep; every attempt starts from the same independent prefix. */
    for (size_t prefix = 0; prefix <= 4; prefix += 4) {
        size_t operation_requests = 0;
        for (size_t failure = 0; failure <= operation_requests; ++failure) {
            ledger tracking = {0};
            doe_label_store *store = create_store(&tracking);
            unsigned char value = 77;
            for (uintptr_t i = 1; i <= prefix; ++i) {
                assert(doe_label_store_set(store, (void *)i, &value, 1) == DOE_LABEL_OK);
            }
            doe_label_view before = view_of(store, 1);
            size_t count = tracking.count;
            size_t requests = tracking.requests;
            tracking.fail_at = failure == 0 ? 0 : requests + failure;
            doe_label_status status = doe_label_store_set(store, (void *)99, &value, 1);
            if (failure == 0) {
                assert(status == DOE_LABEL_OK);
                operation_requests = tracking.requests - requests;
            } else {
                assert(status == DOE_LABEL_OUT_OF_MEMORY);
                assert(tracking.count == count);
                assert(view_of(store, 99).data == NULL);
                assert(view_of(store, 1).data == before.data);
            }
            assert(doe_label_store_destroy(store) == DOE_LABEL_OK);
            assert(tracking.count == 0);
        }
    }
}

typedef struct worker {
    doe_label_store *store;
    uintptr_t first;
} worker;

static void *mutate(void *argument) {
    worker *work = argument;
    for (uintptr_t round = 0; round < 50; ++round) {
        for (uintptr_t i = work->first; i < work->first + 8; ++i) {
            assert(doe_label_store_set(work->store, (void *)i,
                   (const unsigned char *)&round, sizeof(round)) == DOE_LABEL_OK);
            if (round % 2 == 0) {
                assert(doe_label_store_remove(work->store, (void *)i) == DOE_LABEL_OK);
            }
        }
    }
    return NULL;
}

static void concurrency(void) {
    ledger tracking = {0};
    doe_label_store *store = create_store(&tracking);
    pthread_t threads[4];
    worker workers[4];
    for (size_t i = 0; i < 4; ++i) {
        workers[i] = (worker){store, i * 8 + 1};
        assert(pthread_create(&threads[i], NULL, mutate, &workers[i]) == 0);
    }
    for (size_t i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    /* Borrowed bytes are dereferenced only after every writer has joined. */
    for (uintptr_t i = 1; i <= 32; ++i) {
        uintptr_t expected = 49;
        doe_label_view view = view_of(store, i);
        assert(view.length == sizeof(expected));
        assert(memcmp(view.data, &expected, sizeof(expected)) == 0);
    }
    assert(doe_label_store_destroy(store) == DOE_LABEL_OK);
    assert(tracking.count == 0);
}

int main(void) {
    assert(doe_label_store_destroy(NULL) == DOE_LABEL_OK);
    assert(doe_label_store_create(NULL, NULL) == DOE_LABEL_INVALID_ARGUMENT);
    doe_label_store *store = (doe_label_store *)(uintptr_t)1;
    assert(doe_label_store_create(NULL, &store) == DOE_LABEL_INVALID_ARGUMENT);
    assert(store == NULL);
    assert(doe_label_store_get((doe_label_store *)(uintptr_t)1, NULL, NULL) == DOE_LABEL_INVALID_ARGUMENT);
    semantics();
    growth_and_deletion();
    allocation_failures();
    concurrency();
    return 0;
}
