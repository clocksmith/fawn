/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_INDIRECT_H
#define DOE_NATIVE_METAL_INDIRECT_H
#include "metal_binding_internal.h"
#include "doe_metal_indirect.h"

/* Caller holds ordinary device and originating pass leases across preparation,
 * admission and any error delivery. Compilation is outside the device mutex;
 * publication occurs once under it. No native pointer is published on failure. */
native_failure native_metal_guard_initialize(native_device *device);
/* Called with device mutex and the existing recording/device lease. Any failure
 * discards the whole unsubmitted command; unknown uses the existing pending owner. */
native_failure native_metal_dispatch_indirect(doe_mtl_commands *commands,
    const doe_resource_command *record);
#endif
