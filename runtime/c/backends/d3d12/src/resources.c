/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_buffer_create(doe_dx_context *context, uint64_t size,
    doe_dx_memory memory, doe_dx_buffer **out, doe_dx_error *error) {
    if (!context || !out || !size || memory < DOE_DX_DEFAULT || memory > DOE_DX_READBACK)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "buffer.create", error);
    if (size > SIZE_MAX || context->objects == SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE, 0, "buffer.create", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "buffer.create", error);
    doe_dx_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "buffer.allocate", error);
    doe_dx_status status = doe_dx_native_buffer_create(context->native, size, memory,
        &buffer->native, error);
    if (status != DOE_DX_OK) {
        free(buffer);
        return doe_dx_context_result(context, status, error);
    }
    buffer->context = context;
    buffer->references = 1;
    buffer->size = size;
    buffer->memory = memory;
    buffer->initialized = memory == DOE_DX_UPLOAD;
    context->objects++;
    *out = buffer;
    return doe_dx_fail(DOE_DX_OK, 0, "buffer.create", error);
}

doe_dx_status doe_dx_buffer_retain(doe_dx_buffer *buffer) {
    if (!buffer) return DOE_DX_INVALID_ARGUMENT;
    if (buffer->references == UINT32_MAX) return DOE_DX_RANGE;
    buffer->references++;
    return DOE_DX_OK;
}

void doe_dx_buffer_release(doe_dx_buffer *buffer) {
    if (!buffer) return;
    buffer->references--;
    if (buffer->references) return;
    doe_dx_native_buffer_destroy(buffer->native);
    buffer->context->objects--;
    free(buffer);
}

uint64_t doe_dx_buffer_size(const doe_dx_buffer *buffer) {
    return buffer ? buffer->size : 0;
}

static doe_dx_status host_access(doe_dx_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, bool write) {
    if (!buffer || (!bytes && size)) return DOE_DX_INVALID_ARGUMENT;
    if (buffer->context->lost) return DOE_DX_DEVICE_LOST;
    if (buffer->memory != (write ? DOE_DX_UPLOAD : DOE_DX_READBACK)) return DOE_DX_UNSUPPORTED;
    if (!doe_dx_range(buffer->size, offset, size)) return DOE_DX_RANGE;
    if (buffer->owner) return DOE_DX_BUSY;
    if (!write && !buffer->initialized) return DOE_DX_UNINITIALIZED;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_buffer_write(doe_dx_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error) {
    doe_dx_status status = host_access(buffer, offset, bytes, size, true);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "buffer.write", error);
    if (!size) return doe_dx_fail(DOE_DX_OK, 0, "buffer.write", error);
    status = doe_dx_native_write(buffer->native, offset, bytes, size, error);
    return doe_dx_context_result(buffer->context, status, error);
}

doe_dx_status doe_dx_buffer_read(doe_dx_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error) {
    doe_dx_status status = host_access(buffer, offset, bytes, size, false);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "buffer.read", error);
    if (!size) return doe_dx_fail(DOE_DX_OK, 0, "buffer.read", error);
    status = doe_dx_native_read(buffer->native, offset, bytes, size, error);
    return doe_dx_context_result(buffer->context, status, error);
}

doe_dx_status doe_dx_pipeline_create(doe_dx_context *context,
    const doe_dx_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    if (!context || !desc || !out || !desc->bytecode || !desc->bytecode_size ||
        (desc->binding_count && !desc->bindings))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "pipeline.create", error);
    if (desc->binding_count > DOE_DX_MAX_BINDINGS || context->objects == SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE, 0, "pipeline.create", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "pipeline.create", error);
    for (size_t index = 0; index < desc->binding_count; index++) {
        const doe_dx_binding_layout *binding = &desc->bindings[index];
        if (binding->stride < 4 || binding->stride > 2048 || binding->stride % 4 ||
            (binding->access != DOE_DX_READ_ONLY && binding->access != DOE_DX_READ_WRITE))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "pipeline.binding", error);
        for (size_t prior = 0; prior < index; prior++) {
            const doe_dx_binding_layout *other = &desc->bindings[prior];
            if (binding->access == other->access &&
                binding->shader_register == other->shader_register &&
                binding->register_space == other->register_space)
                return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "pipeline.duplicate_binding", error);
        }
    }
    doe_dx_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "pipeline.allocate", error);
    doe_dx_status status = doe_dx_native_pipeline_create(context->native, desc,
        &pipeline->native, error);
    if (status != DOE_DX_OK) {
        free(pipeline);
        return doe_dx_context_result(context, status, error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->binding_count = desc->binding_count;
    if (desc->binding_count)
        memcpy(pipeline->bindings, desc->bindings, desc->binding_count * sizeof(*desc->bindings));
    context->objects++;
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "pipeline.create", error);
}

doe_dx_status doe_dx_pipeline_retain(doe_dx_pipeline *pipeline) {
    if (!pipeline) return DOE_DX_INVALID_ARGUMENT;
    if (pipeline->references == UINT32_MAX) return DOE_DX_RANGE;
    pipeline->references++;
    return DOE_DX_OK;
}

void doe_dx_pipeline_release(doe_dx_pipeline *pipeline) {
    if (!pipeline) return;
    pipeline->references--;
    if (pipeline->references) return;
    doe_dx_native_pipeline_destroy(pipeline->native);
    pipeline->context->objects--;
    free(pipeline);
}
