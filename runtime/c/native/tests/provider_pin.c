/* SPDX-License-Identifier: MIT
 * Real loader control, no GPU: an unpinned image really unloads, then the same
 * image helper retains code for a worker after its public loader handle closes.
 * Pending native resource retirement is checked separately by pending_progress. */
#include "provider_pin_fixture.h"
#include "../src/provider_pin.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
typedef HMODULE provider_image;
static provider_image open_image(const char *path) { return LoadLibraryA(path); }
static void close_image(provider_image image) { assert(FreeLibrary(image)); }
#else
#include <dlfcn.h>
typedef void *provider_image;
static provider_image open_image(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void close_image(provider_image image) { assert(dlclose(image) == 0); }
#endif

static provider_pin_start start(provider_image image)
{
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(image, "provider_pin_fixture_start");
#else
    void *symbol = dlsym(image, "provider_pin_fixture_start");
#endif
    assert(symbol);
    provider_pin_start function;
    _Static_assert(sizeof(function) == sizeof(symbol), "loader function pointer ABI");
    memcpy(&function, &symbol, sizeof(function));
    return function;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    errno = EDOM;
#if defined(_WIN32)
    SetLastError(ERROR_INVALID_DATA);
#endif
    assert(native_provider_pin() && native_provider_pin());
#if defined(_WIN32)
    DWORD error = GetLastError();
    assert(error == ERROR_INVALID_DATA);
#endif
    assert(errno == EDOM);
    provider_pin_control *state = calloc(1, sizeof(*state));
    assert(state);
    atomic_init(&state->finish, false);
    atomic_init(&state->terminal, 0);
    atomic_init(&state->unloaded, 0);
    assert(doe_mutex_init(&state->published_mutex, false).status == DOE_HOST_OK);
    provider_image image = open_image(argv[1]);
    assert(image && start(image)(state, false));
    close_image(image);
    assert(atomic_load(&state->unloaded) == 1);
    atomic_store(&state->unloaded, 0);

    image = open_image(argv[1]);
    assert(image && start(image)(state, true));
    close_image(image);
    assert(!atomic_load(&state->unloaded) && !atomic_load(&state->terminal));
    atomic_store(&state->finish, true);
    assert(doe_thread_join(&state->worker).status == DOE_HOST_OK);
    assert(atomic_load(&state->terminal) == 1 && !atomic_load(&state->unloaded));
    /* Stop borrowing the observer before freeing it. Code residency alone,
     * not the worker or its host control storage, survives until process exit. */
    image = open_image(argv[1]);
    assert(image && start(image)(NULL, false));
    close_image(image);
    assert(doe_mutex_destroy(&state->published_mutex).status == DOE_HOST_OK);
    free(state);
    puts("PASS provider residency: main image, actual unpinned unload, pinned worker survives loader close, terminal join and host-storage cleanup");
}
