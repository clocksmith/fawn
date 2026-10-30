/* SPDX-License-Identifier: MIT
 * Actual Windows build, typed allocation/copy/map call controls only; no GPU. */
#include "../src/d3d12_frontend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#error This control exercises the actual Windows-only frontend body.
#endif

struct doe_dx_buffer {
    doe_dx_memory memory;
    uint64_t size;
    unsigned references;
    bool initialized, pending;
};
struct doe_dx_commands { doe_dx_buffer *source[8], *destination[8]; unsigned count; };
typedef struct control {
    unsigned allocations, copies, reads, live, readback_zeroes, async_submissions;
    uint64_t copy_size, zero_size;
    doe_dx_memory destination;
    native_result submission;
    doe_dx_status copy_status, zero_status;
    doe_dx_commands *pending;
} control;
static control seen;

uint64_t native_allocation_bound(const native_device *device)
{
    (void)device;
    return 4096;
}
doe_dx_status doe_dx_buffer_create(doe_dx_context *context, uint64_t size,
    doe_dx_memory memory, doe_dx_buffer **out, doe_dx_error *error)
{
    (void)context; (void)error;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    **out = (doe_dx_buffer){.memory=memory,.size=size,.references=1,
        .initialized=memory == DOE_DX_UPLOAD};
    seen.allocations++;
    seen.live++;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_buffer_create_readback_zeroed(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error)
{
    seen.readback_zeroes++; seen.zero_size=size;
    if (seen.zero_status != DOE_DX_OK) return seen.zero_status;
    doe_dx_status status=doe_dx_buffer_create(context,size,DOE_DX_READBACK,out,error);
    if (status == DOE_DX_OK) (*out)->initialized=true;
    return status;
}
/* Frontend composition seam only. Cache ownership/reuse is independently
 * exercised by the backend's test_zero_upload against the actual owner. */
doe_dx_status doe_dx_zero_upload_acquire(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error)
{
    return doe_dx_buffer_create(context, size, DOE_DX_UPLOAD, out, error);
}
doe_dx_status doe_dx_upload_acquire(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error)
{
    return doe_dx_buffer_create(context,size,DOE_DX_UPLOAD,out,error);
}
void doe_dx_buffer_release(doe_dx_buffer *buffer)
{
    if (buffer && !--buffer->references) { seen.live--; free(buffer); }
}
doe_dx_status doe_dx_buffer_write(doe_dx_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error)
{
    (void)error;
    assert(buffer->memory == DOE_DX_UPLOAD && bytes && offset + size <= buffer->size);
    return DOE_DX_OK;
}
doe_dx_status doe_dx_buffer_read(doe_dx_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error)
{
    (void)error;
    seen.reads++;
    assert(buffer->memory == DOE_DX_READBACK && offset + size <= buffer->size);
    if (buffer->pending) return DOE_DX_BUSY;
    if (!buffer->initialized) return DOE_DX_UNINITIALIZED;
    /* A call marker, never an oracle for GPU-produced or initialized data. */
    memset(bytes, 0x5a, size);
    return DOE_DX_OK;
}
native_failure native_command_create(const native_backend *backend,
    native_command_handle *out)
{
    out->kind = WGPUBackendType_D3D12;
    out->value.d3d12.context = backend->value.d3d12.context;
    out->value.d3d12.commands = calloc(1, sizeof(doe_dx_commands));
    assert(out->value.d3d12.commands);
    return native_failure_status(NATIVE_OK);
}
doe_dx_status doe_dx_commands_copy(doe_dx_commands *commands, doe_dx_buffer *source,
    uint64_t from, doe_dx_buffer *destination, uint64_t to, uint64_t size,
    doe_dx_error *error)
{
    (void)error;
    if (seen.copy_status != DOE_DX_OK) return seen.copy_status;
    /* An accepted (pending) initializing copy also authorizes later GPU reads. */
    assert((source->initialized || source->pending) && from + size <= source->size &&
        to + size <= destination->size);
    assert(commands->count < 8);
    commands->source[commands->count] = source;
    commands->destination[commands->count++] = destination;
    source->references++;
    destination->references++;
    destination->pending = true;
    seen.copies++;
    seen.copy_size = size;
    seen.destination = destination->memory;
    return DOE_DX_OK;
}
native_result native_command_release(native_command_handle *commands)
{
    doe_dx_commands *work = commands->value.d3d12.commands;
    for (unsigned i = 0; i < work->count; ++i) {
        doe_dx_buffer_release(work->source[i]);
        doe_dx_buffer_release(work->destination[i]);
    }
    free(work);
    return NATIVE_OK;
}
native_failure native_submit_backend_commands(native_device *device,
    native_command_handle commands)
{
    (void)device;
    doe_dx_commands *work = commands.value.d3d12.commands;
    if (seen.submission != NATIVE_OK) {
        assert(!seen.pending);
        seen.pending = work;
        return native_failure_status(seen.submission);
    }
    for (unsigned i = 0; i < work->count; ++i) {
        work->destination[i]->pending = false;
        work->destination[i]->initialized = true;
    }
    assert(native_command_release(&commands) == NATIVE_OK);
    return native_failure_status(NATIVE_OK);
}
static void retire_pending(bool success);
native_failure native_submit_backend_commands_async(native_device *device,
    native_command_handle commands)
{
    (void)device;
    if (seen.pending) retire_pending(true); /* Queue order: the earlier one completes first. */
    seen.pending=commands.value.d3d12.commands;
    seen.async_submissions++;
    return native_failure_status(seen.submission);
}
static void retire_pending(bool success)
{
    assert(seen.pending);
    native_command_handle pending={.kind=WGPUBackendType_D3D12,
        .value.d3d12={.commands=seen.pending}};
    for (unsigned i = 0; i < seen.pending->count; ++i) {
        seen.pending->destination[i]->pending=false;
        seen.pending->destination[i]->initialized=success;
    }
    assert(native_command_release(&pending) == NATIVE_OK);
    seen.pending=NULL;
}
int main(void)
{
    native_device device = {0};
    device.backend.kind = WGPUBackendType_D3D12;
    device.backend.value.d3d12.caps.limits.uniform_offset_alignment = 256;
    native_buffer buffer = {.device=&device};
    assert(native_d3d12_buffer_create(&buffer, 13, DOE_VK_BUFFER_UNIFORM, true).status == NATIVE_INVALID_ARGUMENT);
    assert(!seen.allocations && !buffer.handle.d3d12);
    assert(native_d3d12_buffer_create(&buffer, 13, DOE_VK_BUFFER_COPY_DST, true).status == NATIVE_OK);
    assert(buffer.handle.d3d12->memory == DOE_DX_READBACK);
    assert(buffer.handle.d3d12->allocation_size == 16 && seen.zero_size == 16);
    assert(seen.readback_zeroes == 1 && !seen.copies && !seen.async_submissions && seen.live == 1);
    unsigned allocations = seen.allocations, copies = seen.copies;
    unsigned char bytes[16] = {0};
    assert(native_d3d12_buffer_read(&buffer, 0, bytes, sizeof(bytes)).status == NATIVE_OK);
    assert(bytes[0] == 0x5a && seen.allocations == allocations && seen.copies == copies);
    buffer.handle.d3d12->allocation->pending = true;
    memset(bytes, 0x71, sizeof(bytes));
    assert(native_d3d12_buffer_read(&buffer, 0, bytes, sizeof(bytes)).status == NATIVE_BUSY);
    assert(bytes[0] == 0x71 && seen.allocations == allocations && seen.copies == copies);
    buffer.handle.d3d12->allocation->pending = false;
    buffer.handle.d3d12->allocation->initialized = false;
    assert(native_d3d12_buffer_read(&buffer, 0, bytes, sizeof(bytes)).status == NATIVE_INVALID_STATE);
    assert(bytes[0] == 0x71);
    native_d3d12_buffer_close(&buffer);
    assert(!seen.live);
    /* DEFAULT creation defers its zero fill: no copy or submission yet. */
    unsigned submissions = seen.async_submissions;
    copies = seen.copies;
    assert(native_d3d12_buffer_create(&buffer, 13, DOE_VK_BUFFER_UNIFORM, false).status == NATIVE_OK);
    assert(buffer.handle.d3d12->memory == DOE_DX_DEFAULT && buffer.handle.d3d12->allocation_size == 256);
    assert(buffer.handle.d3d12->zero_deferred && device.backend.value.d3d12.deferred_zero == buffer.handle.d3d12);
    assert(seen.copies == copies && seen.async_submissions == submissions && seen.live == 1);
    native_buffer other = {.device=&device};
    assert(native_d3d12_buffer_create(&other, 4096, DOE_VK_BUFFER_COPY_DST, false).status == NATIVE_OK);
    /* A CPU read materializes every deferred fill, whole width and padding, in one
     * submission before its own transfer. */
    assert(native_d3d12_buffer_read(&buffer, 0, bytes, sizeof(bytes)).status == NATIVE_OK);
    assert(seen.async_submissions == submissions + 1 && seen.pending && seen.pending->count == 2);
    assert(seen.pending->destination[0]->size == 4096 && seen.pending->destination[1]->size == 256);
    assert(!device.backend.value.d3d12.deferred_zero && !buffer.handle.d3d12->zero_deferred);
    retire_pending(true); /* Native completion marker; no GPU/data execution. */
    copies = seen.copies;
    assert(native_d3d12_buffer_read(&buffer, 0, bytes, sizeof(bytes)).status == NATIVE_OK);
    assert(seen.copies == copies + 1 && seen.destination == DOE_DX_READBACK);
    native_d3d12_buffer_close(&buffer);
    native_d3d12_buffer_close(&other);
    assert(!seen.live);
    /* A whole-allocation write is the complete initializing copy: no zero fill. */
    assert(native_d3d12_buffer_create(&buffer, 4096, DOE_VK_BUFFER_COPY_DST, false).status == NATIVE_OK);
    unsigned char page[4096] = {7};
    copies = seen.copies;
    assert(native_d3d12_buffer_write(&buffer, 0, page, sizeof(page)).status == NATIVE_OK);
    assert(seen.copies == copies + 1 && seen.pending && seen.pending->count == 1 && seen.copy_size == 4096);
    assert(!buffer.handle.d3d12->zero_deferred && !device.backend.value.d3d12.deferred_zero);
    retire_pending(true);
    native_d3d12_buffer_close(&buffer);
    /* A partial write (here: uniform padding) materializes the fill first. */
    assert(native_d3d12_buffer_create(&buffer, 13, DOE_VK_BUFFER_UNIFORM, false).status == NATIVE_OK);
    copies = seen.copies;
    submissions = seen.async_submissions;
    assert(native_d3d12_buffer_write(&buffer, 0, page, 16).status == NATIVE_OK);
    /* The 256-byte fill was submitted (and here retired) before the 16-byte copy. */
    assert(seen.async_submissions == submissions + 2 && seen.copies == copies + 2);
    assert(seen.copy_size == 16 && seen.pending && seen.pending->count == 1);
    retire_pending(true);
    native_d3d12_buffer_close(&buffer);
    /* Release before any observation unlinks without a fill. */
    copies = seen.copies;
    assert(native_d3d12_buffer_create(&buffer, 13, DOE_VK_BUFFER_UNIFORM, false).status == NATIVE_OK);
    native_d3d12_buffer_close(&buffer);
    assert(!device.backend.value.d3d12.deferred_zero && seen.copies == copies && !seen.live);
    assert(native_d3d12_materialize_zero(&device).status == NATIVE_OK && seen.copies == copies);
    seen.zero_status=DOE_DX_NATIVE_FAILURE;
    assert(native_d3d12_buffer_create(&buffer,13,DOE_VK_BUFFER_COPY_DST,true).status == NATIVE_FAILURE);
    assert(!buffer.handle.d3d12 && !seen.live);
    seen.zero_status=DOE_DX_OK;
    /* A failed fill recording leaves every buffer deferred and nothing submitted. */
    assert(native_d3d12_buffer_create(&buffer, 13, DOE_VK_BUFFER_COPY_DST, false).status == NATIVE_OK);
    seen.copy_status = DOE_DX_NATIVE_FAILURE;
    submissions = seen.async_submissions;
    assert(native_d3d12_materialize_zero(&device).status == NATIVE_FAILURE);
    assert(buffer.handle.d3d12->zero_deferred && seen.async_submissions == submissions && !seen.pending);
    seen.copy_status = DOE_DX_OK;
    seen.submission = NATIVE_PENDING;
    assert(native_d3d12_materialize_zero(&device).status == NATIVE_PENDING);
    assert(!buffer.handle.d3d12->zero_deferred && seen.pending);
    /* The injected backend now reports terminal failure, permitting release. */
    retire_pending(false);
    native_d3d12_buffer_close(&buffer);
    assert(!seen.live);
    puts("PASS actual Windows frontend composition: READBACK/DEFAULT placement, full zeroing spans/padding, direct map, pending rejection and failure retention, deferred whole-width zero fills batched before reads/partial writes, skipped only by a whole-allocation write; no GPU");
    return 0;
}
