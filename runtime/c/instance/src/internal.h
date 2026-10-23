/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_INSTANCE_INTERNAL_H
#define DOE_INSTANCE_INTERNAL_H

#include "doe_instance.h"
#include <stdatomic.h>

#define DOE_INSTANCE_MAGIC UINT32_C(0xD0E10001)

#if defined(DOE_INSTANCE_TESTING) && defined(DOE_INSTANCE_TEST_REFERENCE_LIMIT)
#define DI_REFERENCE_LIMIT DOE_INSTANCE_TEST_REFERENCE_LIMIT
#else
#define DI_REFERENCE_LIMIT UINT32_MAX
#endif

struct doe_instance {
    uint32_t magic;
    _Atomic uint32_t references;
    doe_future_queue *queue;
    doe_instance_config config;
    size_t allocation_size;
    size_t allocation_alignment;
};

_Noreturn void di_fatal(const char *operation, unsigned status);
#endif
