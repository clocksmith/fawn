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
    const doe_dx_limits *limits = &commands->context->caps.limits;
    if (x > limits->max_dispatch_groups[0] || y > limits->max_dispatch_groups[1] ||
        z > limits->max_dispatch_groups[2] || commands->operation_count == 65536)
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

doe_dx_status doe_dx_commands_dispatch_indirect(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_compute_resources *resources,
    doe_dx_buffer *arguments, uint64_t offset, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    if (!pipeline || pipeline->graphics)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.indirect.pipeline", error);
    if (commands->operation_count == 65536)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.operation_budget", error);
    status = doe_dx_buffer_preflight(commands, arguments, true);
    if (status != DOE_DX_OK)
        return doe_dx_fail(status, 0, "commands.indirect.arguments", error);
    if (arguments->memory != DOE_DX_DEFAULT)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.indirect.heap", error);
    if (!doe_dx_range(arguments->size, offset, 3u * sizeof(uint32_t)))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.indirect.range", error);
    if (offset & 3u)
        return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.indirect.alignment", error);
    doe_dx_native_resources native = {0};
    size_t added = 0;
    status = doe_dx_prepare_resources(commands, pipeline, resources, &native, &added, error);
    if (status != DOE_DX_OK) return status;
    for (size_t index = 0; index < resources->buffer_count; index++)
        if (resources->buffers[index].buffer == arguments)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.indirect.alias", error);
    size_t argument_added = doe_dx_find_buffer(commands, arguments) ? 0u : 1u;
    size_t occupied = commands->buffer_count + commands->texture_count + commands->timestamp_count;
    if (added + argument_added > DOE_DX_MAX_COMMAND_RESOURCES - occupied)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resource_budget", error);
    status = doe_dx_native_dispatch_indirect(commands->native, pipeline->native,
        &native, arguments->native, offset, error);
    if (status != DOE_DX_OK) return doe_dx_context_result(commands->context, status, error);
    doe_dx_use_resources(commands, pipeline, resources);
    doe_dx_use_buffer(commands, arguments);
    commands->operation_count++;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.dispatch_indirect", error);
}
