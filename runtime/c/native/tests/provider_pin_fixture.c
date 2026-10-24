/* SPDX-License-Identifier: MIT */
#include "provider_pin_fixture.h"
#include "../src/provider_pin.h"
#include <stdlib.h>

static provider_pin_control *control;

static void progress(void *identity)
{
    provider_pin_control *state = identity;
    /* Thread control publication completes before the worker can retire itself. */
    if (doe_mutex_lock(&state->published_mutex).status != DOE_HOST_OK) abort();
    if (doe_mutex_unlock(&state->published_mutex).status != DOE_HOST_OK) abort();
    while (!atomic_load(&state->finish))
        if (doe_sleep_for(UINT64_C(1000000)).status != DOE_HOST_OK) abort();
    atomic_fetch_add(&state->terminal, 1);
}

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
bool provider_pin_fixture_start(provider_pin_control *state, bool background)
{
    control = state;
    if (!background) return true;
    if (!native_provider_pin() || !native_provider_pin()) return false;
    if (doe_mutex_lock(&state->published_mutex).status != DOE_HOST_OK) abort();
    doe_host_result created = doe_thread_create(&state->worker, progress, state);
    if (doe_mutex_unlock(&state->published_mutex).status != DOE_HOST_OK) abort();
    return created.status == DOE_HOST_OK;
}

#if defined(_WIN32)
BOOL WINAPI DllMain(HINSTANCE image, DWORD reason, LPVOID reserved)
{
    (void)image;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH && control) atomic_fetch_add(&control->unloaded, 1);
    return TRUE;
}
#else
__attribute__((destructor))
static void unloaded(void)
{
    if (control) atomic_fetch_add(&control->unloaded, 1);
}
#endif
