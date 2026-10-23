/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_commands_dispatch_resources(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_compute_resources *resources,
    uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    if (!pipeline || !x || !y || !z)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.dispatch", error);
    if (pipeline->graphics) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.compute_pipeline", error);
    if (x > 65535 || y > 65535 || z > 65535 || commands->operation_count == 65536)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.dispatch", error);
    doe_dx_native_resources native = {0};
    size_t added = 0;
    status = doe_dx_prepare_resources(commands, pipeline, resources, &native, &added, error);
    if (status != DOE_DX_OK) return status;
    /* All descriptor allocation precedes native command mutation. */
    status = doe_dx_native_dispatch(commands->native, pipeline->native, &native, x, y, z, error);
    if (status != DOE_DX_OK) return doe_dx_context_result(commands->context, status, error);
    doe_dx_use_resources(commands, pipeline, resources);
    commands->operation_count++;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.dispatch", error);
}

doe_dx_status doe_dx_commands_dispatch(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_buffer_binding *bindings,
    size_t binding_count, uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    doe_dx_compute_resources resources = {bindings, binding_count, NULL, 0};
    return doe_dx_commands_dispatch_resources(commands, pipeline, &resources, x, y, z, error);
}
