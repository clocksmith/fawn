/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_mtl_status doe_mtl_buffer_create(doe_mtl_context *context, uint64_t size,
    doe_mtl_memory memory, doe_mtl_buffer **out, doe_mtl_error *error) {
    doe_mtl_buffer *buffer;
    doe_mtl_status status;
    void *contents;
    if (context == NULL || out == NULL || size == 0 ||
        (memory != DOE_MTL_SHARED && memory != DOE_MTL_PRIVATE))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "buffer create", error);
    if (size > SIZE_MAX || size > context->caps.max_buffer_length)
        return doe_mtl_fail(DOE_MTL_RANGE, "buffer length", error);
    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL)
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "buffer allocation", error);
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(buffer);
        return doe_mtl_fail(status, "buffer context lease", error);
    }
    buffer->context = context;
    buffer->references = 1;
    buffer->size = (size_t)size;
    buffer->memory = memory;
    status = context->native.buffer(context->native.userdata, context->device,
        (size_t)size, memory, &buffer->native, error);
    if (status == DOE_MTL_OK && memory == DOE_MTL_SHARED) {
        contents = context->native.contents(context->native.userdata, buffer->native);
        if (contents == NULL) {
            status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "buffer contents", error);
        } else {
            /* newBufferWithLength:options: returns zero-filled memory, so
             * creation writes nothing; native/tests/buffer_zero_init.c checks
             * fresh buffers after dirty same-size reuse. */
            buffer->initialized = true;
        }
    }
    if (status != DOE_MTL_OK) {
        doe_mtl_buffer_release(buffer);
        return status;
    }
    *out = buffer;
    return doe_mtl_fail(DOE_MTL_OK, "buffer create", error);
}

doe_mtl_status doe_mtl_buffer_retain(doe_mtl_buffer *buffer) {
    if (buffer == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (buffer->references == UINT32_MAX) return DOE_MTL_RANGE;
    buffer->references++;
    return DOE_MTL_OK;
}

/* Removed allocations stay alive in the set until a commit. Past this many
 * bytes a release commits its removals at once rather than waiting for the
 * next submission, bounding what a destroy-only workload keeps alive. */
enum { RESIDENCY_REMOVED_BYTES_LIMIT = 64u << 20 };

static void residency_commit(doe_mtl_context *context) {
    context->native.residency_commit(context->native.userdata, context->residency);
    context->residency_dirty = false;
    context->residency_removed_bytes = 0;
}

void doe_mtl_context_make_resident(doe_mtl_context *context,
    doe_mtl_buffer *const *buffers, size_t count) {
    bool changed = false;
    if (context == NULL || context->residency == NULL || (count != 0 && buffers == NULL)) return;
    for (size_t i = 0; i < count; ++i) {
        doe_mtl_buffer *buffer = buffers[i];
        if (buffer == NULL || buffer->context != context || buffer->native == NULL || buffer->resident) continue;
        context->native.residency_update(context->native.userdata, context->residency, buffer->native, true);
        buffer->resident = true;
        changed = true;
    }
    if (changed || context->residency_dirty) residency_commit(context);
}

void doe_mtl_context_commit_removals(doe_mtl_context *context) {
    if (context->residency != NULL && context->residency_dirty) residency_commit(context);
}

/* A resident allocation leaves the set first; the set keeps it alive until the
 * next commit. Callers release only after the last recorded use retired. */
static void release_native(doe_mtl_buffer *buffer) {
    doe_mtl_context *context = buffer->context;
    if (buffer->native == NULL) return;
    if (buffer->resident) {
        context->native.residency_update(context->native.userdata, context->residency, buffer->native, false);
        buffer->resident = false;
        context->residency_dirty = true;
        context->residency_removed_bytes += buffer->size;
    }
    context->native.release(context->native.userdata, buffer->native);
    buffer->native = NULL;
    if (context->residency_removed_bytes >= RESIDENCY_REMOVED_BYTES_LIMIT) residency_commit(context);
}

void doe_mtl_buffer_release(doe_mtl_buffer *buffer) {
    doe_mtl_context *context;
    if (buffer == NULL || --buffer->references != 0) return;
    context = buffer->context;
    release_native(buffer);
    free(buffer);
    doe_mtl_context_release(context);
}

doe_mtl_status doe_mtl_buffer_evict(doe_mtl_buffer *buffer) {
    if (buffer == NULL) return DOE_MTL_INVALID_ARGUMENT;
    buffer->evict_requested = true;
    if (buffer->owner == NULL) release_native(buffer);
    return DOE_MTL_OK;
}

void doe_mtl_buffer_use_retired(doe_mtl_buffer_use *use) {
    doe_mtl_buffer *buffer = use->buffer;
    if (use->older) use->older->newer = use->newer;
    if (use->newer) use->newer->older = use->older;
    else buffer->uses = use->older;
    buffer->owner = buffer->uses ? buffer->uses->commands : NULL;
    if (!buffer->uses && buffer->evict_requested) release_native(buffer);
}

uint64_t doe_mtl_buffer_size(const doe_mtl_buffer *buffer) {
    return buffer != NULL ? buffer->size : 0;
}

void *doe_mtl_buffer_native(const doe_mtl_buffer *buffer) {
    return buffer != NULL ? buffer->native : NULL;
}

static doe_mtl_status host_access(doe_mtl_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size) {
    if (buffer == NULL || (size != 0 && bytes == NULL)) return DOE_MTL_INVALID_ARGUMENT;
    if (!doe_mtl_range(buffer->size, offset, size)) return DOE_MTL_RANGE;
    if (buffer->memory != DOE_MTL_SHARED) return DOE_MTL_UNSUPPORTED;
    if (buffer->evict_requested) return DOE_MTL_INVALID_STATE;
    if (buffer->owner != NULL) return DOE_MTL_BUSY;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_buffer_write(doe_mtl_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size) {
    doe_mtl_status status = host_access(buffer, offset, bytes, size);
    unsigned char *contents;
    bool full;
    if (status != DOE_MTL_OK) return status;
    full = offset == 0 && size == buffer->size;
    if (buffer->poisoned && !full) return DOE_MTL_EXECUTION_FAILED;
    if (size == 0) return DOE_MTL_OK;
    contents = buffer->context->native.contents(buffer->context->native.userdata,
        buffer->native);
    if (contents == NULL) return DOE_MTL_NATIVE_FAILURE;
    memmove(contents + (size_t)offset, bytes, size);
    if (full) {
        buffer->initialized = true;
        buffer->poisoned = false;
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_buffer_host_range(doe_mtl_buffer *buffer, uint64_t offset,
    size_t size, const void **out) {
    static const unsigned char empty = 0;
    doe_mtl_status status = out != NULL ? host_access(buffer, offset, &empty, size) :
        DOE_MTL_INVALID_ARGUMENT;
    const unsigned char *contents;
    if (status != DOE_MTL_OK) return status;
    if (buffer->poisoned) return DOE_MTL_EXECUTION_FAILED;
    if (!buffer->initialized) return DOE_MTL_UNINITIALIZED;
    contents = buffer->context->native.contents(buffer->context->native.userdata,
        buffer->native);
    if (contents == NULL) return DOE_MTL_NATIVE_FAILURE;
    *out = contents + (size_t)offset;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_buffer_host_write_range(doe_mtl_buffer *buffer, uint64_t offset,
    size_t size, void **out) {
    unsigned char probe = 0;
    doe_mtl_status status = out ? host_access(buffer, offset, &probe, size) : DOE_MTL_INVALID_ARGUMENT;
    if (status != DOE_MTL_OK) return status;
    if (buffer->poisoned) return DOE_MTL_EXECUTION_FAILED;
    if (!buffer->initialized) return DOE_MTL_UNINITIALIZED;
    unsigned char *contents = buffer->context->native.contents(buffer->context->native.userdata, buffer->native);
    if (!contents) return DOE_MTL_NATIVE_FAILURE;
    *out = contents + (size_t)offset;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_buffer_read(doe_mtl_buffer *buffer, uint64_t offset,
    void *bytes, size_t size) {
    const void *source = NULL;
    doe_mtl_status status;
    if (size != 0 && bytes == NULL) return DOE_MTL_INVALID_ARGUMENT;
    status = doe_mtl_buffer_host_range(buffer, offset, size, &source);
    if (status != DOE_MTL_OK) return status;
    if (size != 0) memmove(bytes, source, size);
    return DOE_MTL_OK;
}

static bool valid_workgroup(const doe_mtl_context *context,
    doe_mtl_dimensions workgroup, size_t total) {
    const uint32_t axes[3] = {workgroup.x, workgroup.y, workgroup.z};
    size_t i;
    for (i = 0; i < 3; ++i) {
        if (axes[i] == 0 || axes[i] > context->caps.max_threads_per_dimension[i] ||
            axes[i] > total) return false;
        total /= axes[i];
    }
    return true;
}

doe_mtl_status doe_mtl_pipeline_create(doe_mtl_context *context,
    const doe_mtl_pipeline_desc *desc, doe_mtl_pipeline **out, doe_mtl_error *error) {
    doe_mtl_pipeline *pipeline;
    doe_mtl_status status;
    size_t max_threads = 0;
    if (context == NULL || desc == NULL || out == NULL || desc->msl == NULL ||
        desc->msl_length == 0 || desc->entrypoint == NULL || desc->entrypoint[0] == '\0')
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "pipeline create", error);
    if (desc->sizes_slot != UINT32_MAX && desc->sizes_slot >= DOE_MTL_BUFFER_SLOTS)
        return doe_mtl_fail(DOE_MTL_RANGE, "pipeline sizes slot", error);
    if (!valid_workgroup(context, desc->workgroup, SIZE_MAX))
        return doe_mtl_fail(DOE_MTL_RANGE, "pipeline workgroup", error);
    pipeline = calloc(1, sizeof(*pipeline));
    if (pipeline == NULL)
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "pipeline allocation", error);
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(pipeline);
        return doe_mtl_fail(status, "pipeline context lease", error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->workgroup = desc->workgroup;
    pipeline->sizes_slot = desc->sizes_slot;
    status = context->native.pipeline(context->native.userdata, context->device,
        desc, &pipeline->native, &max_threads, error);
    if (status == DOE_MTL_OK && !valid_workgroup(context, desc->workgroup, max_threads))
        status = doe_mtl_fail(DOE_MTL_RANGE, "pipeline maximum threads", error);
    if (status != DOE_MTL_OK) {
        doe_mtl_pipeline_release(pipeline);
        return status;
    }
    *out = pipeline;
    return doe_mtl_fail(DOE_MTL_OK, "pipeline create", error);
}

doe_mtl_status doe_mtl_pipeline_retain(doe_mtl_pipeline *pipeline) {
    if (pipeline == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (pipeline->references == UINT32_MAX) return DOE_MTL_RANGE;
    pipeline->references++;
    return DOE_MTL_OK;
}

void doe_mtl_pipeline_release(doe_mtl_pipeline *pipeline) {
    doe_mtl_context *context;
    if (pipeline == NULL || --pipeline->references != 0) return;
    context = pipeline->context;
    if (pipeline->native != NULL)
        context->native.release(context->native.userdata, pipeline->native);
    free(pipeline);
    doe_mtl_context_release(context);
}

void *doe_mtl_pipeline_native(const doe_mtl_pipeline *pipeline) {
    return pipeline != NULL ? pipeline->native : NULL;
}


doe_mtl_status doe_mtl_shader_validate(doe_mtl_context *context, const char *source,
    size_t length, doe_mtl_error *error) {
    if (!context || !source || !length)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shader source validation", error);
    if (!context->native.shader_validate)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shader source validation unavailable", error);
    return context->native.shader_validate(context->native.userdata, context->device, source, length, error);
}
