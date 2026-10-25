/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

/* Reuses the timestamp owner's exact Metal event ABI; this event instead belongs
 * to the device queue and is retained until its context has no command leases. */
doe_mtl_status doe_mtl_objc_timeline_create(void *userdata, void *device,
    void **out, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    if (!r->responds_call(device, r->responds, r->queries.new_event))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "newSharedEvent", error);
    void *pool = doe_mtl_objc_pool(r);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    void *event = doe_mtl_objc_object(r, device, r->queries.new_event);
    doe_mtl_objc_drain(r, pool);
    if (!event) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newSharedEvent", error);
    *out = event;
    return doe_mtl_fail(DOE_MTL_OK, "newSharedEvent", error);
}

doe_mtl_status doe_mtl_objc_timeline_signal(void *userdata, void *command,
    void *event, uint64_t value, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    if (!r->responds_call(command, r->responds, r->queries.signal_event))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "encodeSignalEvent:value:", error);
    r->queries.event(command, r->queries.signal_event, event, value);
    return doe_mtl_fail(DOE_MTL_OK, "encodeSignalEvent:value:", error);
}

uint64_t doe_mtl_objc_timeline_value(void *userdata, void *event) {
    doe_mtl_objc *r = userdata;
    return r->queries.event_value(event, r->queries.signaled_value);
}
