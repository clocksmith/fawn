/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Smallest DEFAULT size published known zero (the placed-buffer bound). */
#define DOE_DX_KNOWN_ZERO_DEFAULT_MIN (UINT64_C(1) << 20)

/* staging: unspecified-content UPLOAD for caller data (doe_dx_native_staging_create). */
static doe_dx_status buffer_create(doe_dx_context *context, uint64_t size,
    doe_dx_memory memory, bool staging, doe_dx_buffer **out, doe_dx_error *error) {
    if (!context || !out || !size || memory < DOE_DX_DEFAULT || memory > DOE_DX_READBACK)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "buffer.create", error);
    if (size > SIZE_MAX || size > context->caps.limits.max_buffer_size || context->objects == SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE, 0, "buffer.create", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "buffer.create", error);
    doe_dx_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "buffer.allocate", error);
    doe_dx_status status = staging ?
        doe_dx_native_staging_create(context->native, size, &buffer->native, error) :
        doe_dx_native_buffer_create(context->native, size, memory, &buffer->native, error);
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

doe_dx_status doe_dx_buffer_create(doe_dx_context *context, uint64_t size,
    doe_dx_memory memory, doe_dx_buffer **out, doe_dx_error *error) {
    return buffer_create(context, size, memory, false, out, error);
}

doe_dx_status doe_dx_buffer_create_readback_zeroed(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error) {
    if (!out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"readback_zero.create",error);
    doe_dx_buffer *buffer = NULL;
    doe_dx_status status = doe_dx_buffer_create(context,size,DOE_DX_READBACK,&buffer,error);
    if (status != DOE_DX_OK) return status;
    if (!doe_dx_native_buffer_zeroed(buffer->native))
        status = doe_dx_native_zero_readback(buffer->native,error);
    if (status != DOE_DX_OK) {
        doe_dx_buffer_release(buffer);
        return doe_dx_context_result(context,status,error);
    }
    buffer->initialized = true; /* Synchronous CPU writes finished before any GPU use. */
    *out = buffer;
    return doe_dx_fail(DOE_DX_OK,0,"readback_zero.create",error);
}

doe_dx_status doe_dx_buffer_create_default_known_zero(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, bool *known_zero, doe_dx_error *error) {
    if (!out || !known_zero)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"default_known_zero.create",error);
    doe_dx_buffer *buffer = NULL;
    doe_dx_status status = doe_dx_buffer_create(context,size,DOE_DX_DEFAULT,&buffer,error);
    if (status != DOE_DX_OK) return status;
    /* D3D12 zeroed the whole allocation at creation and no command has used it.
     * Only buffers beyond the 1 MiB placement bound (always new committed
     * resources) are published known zero: small buffers keep their deferred
     * zero copy, which is the queue's first ExecuteCommandLists. Skipping it
     * moved that first-submission cost into the first timed submit and map
     * (prepared host operation 3.83 -> 4.00 ms on bee). */
    const bool zero = size > DOE_DX_KNOWN_ZERO_DEFAULT_MIN && doe_dx_native_buffer_zeroed(buffer->native);
    if (zero) buffer->initialized = true;
    *out = buffer;
    *known_zero = zero;
    return doe_dx_fail(DOE_DX_OK,0,"default_known_zero.create",error);
}

doe_dx_status doe_dx_zero_upload_acquire(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error) {
    if (!context || !out || !size)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "zero_upload.acquire", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "zero_upload.acquire", error);
    enum { CACHE_BYTES = 1024 * 1024 };
    if (size <= CACHE_BYTES && context->zero_upload && context->zero_upload->size >= size) {
        doe_dx_status status = doe_dx_buffer_retain(context->zero_upload);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "zero_upload.retain", error);
        *out = context->zero_upload;
        return doe_dx_fail(DOE_DX_OK, 0, "zero_upload.reuse", error);
    }
    doe_dx_buffer *prepared = NULL;
    doe_dx_status status = doe_dx_buffer_create(context, size, DOE_DX_UPLOAD, &prepared, error);
    if (status != DOE_DX_OK) return status;
    prepared->immutable_zero = true; /* Native construction completed the whole CPU zero fill. */
    if (size <= CACHE_BYTES) {
        status = doe_dx_buffer_retain(prepared);
        if (status != DOE_DX_OK) {
            doe_dx_buffer_release(prepared);
            return doe_dx_fail(status, 0, "zero_upload.retain", error);
        }
        doe_dx_buffer *previous = context->zero_upload;
        context->zero_upload = prepared; /* Creation lease belongs to this context. */
        doe_dx_buffer_release(previous); /* Existing caller/command leases survive growth. */
    }
    *out = prepared;
    return doe_dx_fail(DOE_DX_OK, 0, "zero_upload.acquire", error);
}

doe_dx_status doe_dx_upload_acquire(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error) {
    if (!context || !out || !size)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"upload.acquire",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"upload.acquire",error);
    if (size > SIZE_MAX || size > context->caps.limits.max_buffer_size)
        return doe_dx_fail(DOE_DX_RANGE,0,"upload.acquire",error);
    doe_dx_buffer **slot = NULL;
    for (size_t i = 0; i < DOE_DX_UPLOAD_PAGES; ++i) {
        doe_dx_buffer **candidate = i ? &context->upload_pages[i - 1] : &context->upload_staging;
        doe_dx_buffer *page = *candidate;
        bool idle = !page || (page->references == 1 && !page->owner && !page->pending);
        if (!idle) continue;
        if (page && !page->evict_requested && page->size >= size) {
            doe_dx_status status = doe_dx_buffer_retain(page);
            if (status != DOE_DX_OK) return doe_dx_fail(status,0,"upload.retain",error);
            page->upload_slices=false; page->upload_used=page->size;
            *out = page;
            return doe_dx_fail(DOE_DX_OK,0,"upload.reuse",error);
        }
        if (!slot) slot = candidate;
    }
    /* Amortize small caller writes without ever reusing memory still held by
     * a caller, recording, or pending command. This is not a second queue. */
    uint64_t limit = 1024u * 1024u;
    if (limit > context->caps.limits.max_buffer_size) limit = context->caps.limits.max_buffer_size;
    if (limit > context->caps.limits.max_buffer_allocation_bytes)
        limit = context->caps.limits.max_buffer_allocation_bytes;
    bool cacheable = slot && size <= limit;
    uint64_t capacity = cacheable ? limit : size;
    doe_dx_buffer *prepared = NULL;
    /* An uncached request beyond one page is used once for exactly its caller
     * bytes: skip D3D12's creation zero (the bulk of a large write's staging cost). */
    doe_dx_status status = buffer_create(context,capacity,DOE_DX_UPLOAD,!cacheable && size > limit,
        &prepared,error);
    /* A speculative larger page must not turn an otherwise satisfiable small
     * request into OOM. Only that allocation result admits an exact-size retry. */
    if (status == DOE_DX_OUT_OF_MEMORY && capacity > size)
        status = doe_dx_buffer_create(context,size,DOE_DX_UPLOAD,&prepared,error);
    if (status != DOE_DX_OK) return status;
    if (cacheable) {
        status = doe_dx_buffer_retain(prepared);
        if (status != DOE_DX_OK) {
            doe_dx_buffer_release(prepared);
            return doe_dx_fail(status,0,"upload.retain",error);
        }
        doe_dx_buffer *previous = *slot;
        *slot = prepared;
        doe_dx_buffer_release(previous);
    }
    prepared->upload_used=prepared->size;
    *out = prepared;
    return doe_dx_fail(DOE_DX_OK,0,"upload.acquire",error);
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
    doe_dx_buffer_return_read(buffer);
    if (buffer->native) doe_dx_native_buffer_destroy(buffer->native);
    buffer->context->objects--;
    free(buffer);
}

void doe_dx_buffer_try_evict(doe_dx_buffer *buffer) {
    if (!buffer->evict_requested || buffer->owner || buffer->pending || !buffer->native) return;
    doe_dx_buffer_return_read(buffer);
    doe_dx_native_buffer_destroy(buffer->native);
    buffer->native = NULL;
}

doe_dx_status doe_dx_buffer_evict(doe_dx_buffer *buffer) {
    if (!buffer) return DOE_DX_INVALID_ARGUMENT;
    buffer->evict_requested = true;
    doe_dx_buffer_try_evict(buffer);
    return DOE_DX_OK;
}

uint64_t doe_dx_buffer_size(const doe_dx_buffer *buffer) {
    return buffer ? buffer->size : 0;
}

static doe_dx_status host_access(doe_dx_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, bool write) {
    if (!buffer || (!bytes && size)) return DOE_DX_INVALID_ARGUMENT;
    if (buffer->evict_requested) return DOE_DX_INVALID_STATE;
    if (buffer->context->lost) return DOE_DX_DEVICE_LOST;
    if (buffer->memory != (write ? DOE_DX_UPLOAD : DOE_DX_READBACK)) return DOE_DX_UNSUPPORTED;
    if (!doe_dx_range(buffer->size, offset, size)) return DOE_DX_RANGE;
    if (write && buffer->upload_slices) return DOE_DX_INVALID_STATE;
    if (write && buffer->immutable_zero) return DOE_DX_UNSUPPORTED;
    if (buffer->lent) return DOE_DX_INVALID_STATE;
    if (buffer->owner || buffer->pending) return DOE_DX_BUSY;
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

doe_dx_status doe_dx_buffer_lend_read(doe_dx_buffer *buffer, uint64_t offset, size_t size,
    const void **out, doe_dx_error *error) {
    if (!out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "buffer.lend", error);
    /* host_access needs a non-null destination only for nonzero reads. */
    doe_dx_status status = host_access(buffer, offset, out, size, false);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "buffer.lend", error);
    status = doe_dx_native_map_read(buffer->native, offset, size, out, error);
    if (status == DOE_DX_OK) buffer->lent = true;
    return doe_dx_context_result(buffer->context, status, error);
}

doe_dx_status doe_dx_buffer_lend_write(doe_dx_buffer *buffer,uint64_t offset,size_t size,
    void **out,doe_dx_error *error) {
    if (!out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"buffer.lend_write",error);
    doe_dx_status status=host_access(buffer,offset,out,size,true);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"buffer.lend_write",error);
    status=doe_dx_native_map_write(buffer->native,offset,size,out,error);
    if (status==DOE_DX_OK) {
        buffer->lent=true; buffer->lent_write=true;
        buffer->lend_offset=offset; buffer->lend_size=size;
    }
    return doe_dx_context_result(buffer->context,status,error);
}
void doe_dx_buffer_return_read(doe_dx_buffer *buffer) {
    if (!buffer || !buffer->lent) return;
    if (buffer->lent_write) doe_dx_native_unmap_write(buffer->native,buffer->lend_offset,buffer->lend_size);
    else doe_dx_native_unmap_read(buffer->native);
    buffer->lent = false; buffer->lent_write=false;
}
