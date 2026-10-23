/* SPDX-License-Identifier: MIT
 * Real loader control, no GPU: an unpinned image really unloads, then the same
 * image helper retains code for a worker after its public loader handle closes.
 * Pending native resource retirement is checked separately by pending_progress. */
#include "provider_pin_fixture.h"
#include "../src/provider_pin.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static provider_pin_start start(void *image)
{
    void *symbol = dlsym(image, "provider_pin_fixture_start");
    assert(symbol);
    provider_pin_start function;
    _Static_assert(sizeof(function) == sizeof(symbol), "loader function pointer ABI");
    memcpy(&function, &symbol, sizeof(function));
    return function;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    /* Linking the helper into a normal executable needs no dlopen of its PIE. */
    assert(native_provider_pin() && native_provider_pin());
    provider_pin_control *state = calloc(1, sizeof(*state));
    assert(state);
    atomic_init(&state->finish, false);
    atomic_init(&state->terminal, 0);
    atomic_init(&state->unloaded, 0);
    void *image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(image && start(image)(state, false));
    assert(dlclose(image) == 0);
    assert(atomic_load(&state->unloaded) == 1);
    atomic_store(&state->unloaded, 0);

    image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(image && start(image)(state, true));
    assert(dlclose(image) == 0);
    assert(!atomic_load(&state->unloaded) && !atomic_load(&state->terminal));
    atomic_store(&state->finish, true);
    assert(pthread_join(state->worker, NULL) == 0);
    assert(atomic_load(&state->terminal) == 1 && !atomic_load(&state->unloaded));
    /* Stop borrowing the observer before freeing it. Only the image reference,
     * not the worker or its host control storage, is retained until exit. */
    image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(image && start(image)(NULL, false));
    assert(dlclose(image) == 0);
    free(state);
    puts("PASS provider residency: main-image admission, actual unpinned unload, pinned worker survives dlclose and joins after terminal retirement");
}
