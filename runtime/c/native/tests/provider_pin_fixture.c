/* SPDX-License-Identifier: MIT */
#include "provider_pin_fixture.h"
#include "../src/provider_pin.h"
#include <errno.h>
#include <time.h>

static provider_pin_control *control;

static void *progress(void *identity)
{
    provider_pin_control *state = identity;
    while (!atomic_load(&state->finish)) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
    }
    atomic_fetch_add(&state->terminal, 1);
    return NULL;
}

__attribute__((visibility("default")))
bool provider_pin_fixture_start(provider_pin_control *state, bool background)
{
    control = state;
    if (!background) return true;
    if (!native_provider_pin() || !native_provider_pin()) return false;
    return pthread_create(&state->worker, NULL, progress, state) == 0;
}

__attribute__((destructor))
static void unloaded(void)
{
    if (control) atomic_fetch_add(&control->unloaded, 1);
}
