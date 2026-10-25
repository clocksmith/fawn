/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Called only before resources/commands escape the new per-device context.
 * Unsupported optional events retain ordinary command-buffer completion; a real
 * allocation/native failure is not converted to successful fallback. */
doe_mtl_status doe_mtl_context_prefer_timeline(doe_mtl_context *context,
    doe_mtl_error *error) {
    if (!context || context->references != 1)
        return doe_mtl_fail(DOE_MTL_INVALID_STATE, "timeline configuration", error);
    if (context->timeline || !context->native.timeline_create ||
        !context->native.timeline_signal || !context->native.timeline_value)
        return doe_mtl_fail(DOE_MTL_OK, "timeline configuration", error);
    void *event = NULL;
    doe_mtl_status status = context->native.timeline_create(context->native.userdata,
        context->device, &event, error);
    if (status == DOE_MTL_UNSUPPORTED)
        return doe_mtl_fail(DOE_MTL_OK, "timeline unavailable; command completion", error);
    if (status != DOE_MTL_OK) return status;
    if (!event) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "timeline allocation", error);
    context->timeline = event;
    context->caps.timeline_enabled = true;
    return doe_mtl_fail(DOE_MTL_OK, "timeline configuration", error);
}
