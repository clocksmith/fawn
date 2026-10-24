/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_DRAW_GUARD_H
#define DOE_NATIVE_METAL_DRAW_GUARD_H
#include "metal_binding_internal.h"
/* Ordinary device and originating pass/bundle call leases cover compilation,
 * publication and callback delivery. Canonical admission precedes this call. */
native_failure native_metal_draw_guard_initialize(native_device *device);
/* Caller retains exactly one originating pass/bundle and an ordinary device
 * lease. Completes canonical admission with Metal preparation/error delivery. */
void native_metal_draw_guard_finish(native_device *, doe_resource_render_pass *,
    doe_resource_bundle_encoder *, doe_resource_status, bool, const char *);
#endif
