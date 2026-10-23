/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PROVIDER_PIN_FIXTURE_H
#define DOE_NATIVE_PROVIDER_PIN_FIXTURE_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct provider_pin_control {
    atomic_bool finish;
    atomic_uint terminal;
    atomic_uint unloaded;
    pthread_t worker;
} provider_pin_control;

typedef bool (*provider_pin_start)(provider_pin_control *, bool);
#endif
