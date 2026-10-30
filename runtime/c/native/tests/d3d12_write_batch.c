/* SPDX-License-Identifier: MIT
 * Actual Windows build of the D3D12 queue-write batch owner at the typed native
 * call seam: which copies each command records, in which order, and when it is
 * submitted. A call ledger only; no GPU, data or ordering-on-hardware claim. */
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
    unsigned references, id;
};
enum { OPS = 80, SUBMITS = 32 };
typedef struct op { char kind; unsigned source, destination; uint64_t offset, size; } op;
struct doe_dx_commands { op ops[OPS]; doe_dx_buffer *held[2 * OPS]; unsigned count, held_count, id; };
typedef struct control {
    unsigned ids, live, commands, live_commands, submitted_count;
    doe_dx_commands *submitted[SUBMITS];
    doe_dx_status copy_status;
    native_result create_status;
} control;
static control seen;

uint64_t native_allocation_bound(const native_device *device)
{
    (void)device;
    return UINT64_C(1) << 26;
}
doe_dx_status doe_dx_buffer_create(doe_dx_context *context, uint64_t size,
    doe_dx_memory memory, doe_dx_buffer **out, doe_dx_error *error)
{
    (void)context; (void)error;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    **out = (doe_dx_buffer){.memory=memory,.size=size,.references=1,.id=++seen.ids};
    seen.live++;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_buffer_create_readback_zeroed(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error)
{
    return doe_dx_buffer_create(context, size, DOE_DX_READBACK, out, error);
}
doe_dx_status doe_dx_zero_upload_acquire(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error)
{
    return doe_dx_buffer_create(context, size, DOE_DX_UPLOAD, out, error);
}
doe_dx_status doe_dx_upload_acquire(doe_dx_context *context, uint64_t size,
    doe_dx_buffer **out, doe_dx_error *error)
{
    return doe_dx_buffer_create(context, size, DOE_DX_UPLOAD, out, error);
}
doe_dx_status doe_dx_buffer_retain(doe_dx_buffer *buffer)
{
    buffer->references++;
    return DOE_DX_OK;
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
    (void)buffer; (void)offset; (void)error;
    memset(bytes, 0, size);
    return DOE_DX_OK;
}
doe_dx_status doe_dx_buffer_lend_read(doe_dx_buffer *buffer, uint64_t offset, size_t size,
    const void **out, doe_dx_error *error)
{
    (void)buffer; (void)offset; (void)size; (void)out; (void)error;
    return DOE_DX_UNSUPPORTED;
}
void doe_dx_buffer_return_read(doe_dx_buffer *buffer) { (void)buffer; }

native_failure native_command_create(const native_backend *backend, native_command_handle *out)
{
    if (seen.create_status != NATIVE_OK) return native_failure_status(seen.create_status);
    out->kind = WGPUBackendType_D3D12;
    out->value.d3d12.context = backend->value.d3d12.context;
    out->value.d3d12.commands = calloc(1, sizeof(doe_dx_commands));
    assert(out->value.d3d12.commands);
    out->value.d3d12.commands->id = ++seen.commands;
    seen.live_commands++;
    return native_failure_status(NATIVE_OK);
}
static void hold(doe_dx_commands *commands, doe_dx_buffer *buffer)
{
    buffer->references++;
    commands->held[commands->held_count++] = buffer;
}
doe_dx_status doe_dx_commands_copy(doe_dx_commands *commands, doe_dx_buffer *source,
    uint64_t from, doe_dx_buffer *destination, uint64_t to, uint64_t size, doe_dx_error *error)
{
    (void)error;
    if (seen.copy_status != DOE_DX_OK) return seen.copy_status;
    assert(!from && to + size <= destination->size && commands->count < OPS);
    commands->ops[commands->count++] = (op){'c', source->id, destination->id, to, size};
    hold(commands, source);
    hold(commands, destination);
    return DOE_DX_OK;
}
doe_dx_status doe_dx_commands_order_buffer_writes(doe_dx_commands *commands, doe_dx_error *error)
{
    (void)error;
    for (unsigned i = 0; i < commands->held_count; ++i)
        if (commands->held[i]->memory == DOE_DX_READBACK) return DOE_DX_UNSUPPORTED;
    commands->ops[commands->count++] = (op){.kind='o'};
    return DOE_DX_OK;
}
void doe_dx_commands_release(doe_dx_commands *commands)
{
    for (unsigned i = 0; i < commands->held_count; ++i) doe_dx_buffer_release(commands->held[i]);
    seen.live_commands--;
    free(commands);
}
native_result native_command_release(native_command_handle *commands)
{
    if (commands->kind == WGPUBackendType_D3D12) doe_dx_commands_release(commands->value.d3d12.commands);
    *commands = (native_command_handle){0};
    return NATIVE_OK;
}
native_failure native_submit_backend_commands_async(native_device *device, native_command_handle commands)
{
    (void)device;
    assert(seen.submitted_count < SUBMITS);
    seen.submitted[seen.submitted_count++] = commands.value.d3d12.commands;
    return native_failure_status(NATIVE_OK);
}
native_failure native_submit_backend_commands(native_device *device, native_command_handle commands)
{
    return native_submit_backend_commands_async(device, commands);
}
/* Retires every recorded submission: the pending owner's terminal release. */
static void retire(void)
{
    for (unsigned i = 0; i < seen.submitted_count; ++i) doe_dx_commands_release(seen.submitted[i]);
    seen.submitted_count = 0;
}
static bool copy_is(const op *value, unsigned destination, uint64_t offset, uint64_t size)
{
    return value->kind == 'c' && value->destination == destination && value->offset == offset && value->size == size;
}
static unsigned id(const native_buffer *buffer) { return buffer->handle.d3d12->allocation->id; }

static native_buffer make(native_device *device, uint64_t size, bool readback)
{
    native_buffer buffer = {.device=device};
    assert(native_d3d12_buffer_create(&buffer, size, DOE_VK_BUFFER_COPY_DST, readback).status == NATIVE_OK);
    return buffer;
}

int main(void)
{
    native_device device = {0};
    device.backend.kind = WGPUBackendType_D3D12;
    device.backend.value.d3d12.caps.limits.uniform_offset_alignment = 256;
    unsigned char bytes[4096] = {1};
    native_buffer a = make(&device, 4096, false), b = make(&device, 4096, false);
    /* Any write first materializes every deferred zero fill on its own. */
    assert(native_d3d12_materialize_zero(&device).status == NATIVE_OK && seen.submitted_count == 1);
    retire();
    unsigned created = seen.commands;
    /* Whole writes skip zero fills: both copies record into one open command, in
     * call order, with nothing submitted. */
    assert(native_d3d12_buffer_write(&a, 0, bytes, 4096).status == NATIVE_OK);
    assert(native_d3d12_buffer_write(&b, 0, bytes, 4096).status == NATIVE_OK);
    assert(!seen.submitted_count && seen.commands == created + 1);
    /* The submission adopts the open command: writes, ordering, then its work. */
    native_command_handle adopted = {0};
    bool taken = false;
    assert(native_d3d12_adopt_writes(&device, &adopted, &taken).status == NATIVE_OK && taken);
    doe_dx_commands *work = adopted.value.d3d12.commands;
    assert(work->id == created + 1 && work->count == 3 && work->ops[2].kind == 'o');
    assert(copy_is(&work->ops[0], id(&a), 0, 4096) && copy_is(&work->ops[1], id(&b), 0, 4096));
    assert(doe_dx_commands_copy(work, a.handle.d3d12->allocation, 0, b.handle.d3d12->allocation, 0, 64, NULL) == DOE_DX_OK);
    native_d3d12_writes_recorded(&device);
    assert(native_submit_backend_commands_async(&device, adopted).status == NATIVE_OK);
    assert(seen.submitted_count == 1 && seen.submitted[0] == work && work->count == 4);
    /* Nothing remains open: flushing is a no-op. */
    assert(native_d3d12_flush_writes(&device).status == NATIVE_OK && seen.submitted_count == 1);
    retire();

    /* A rejected adopted recording discards its partial work, never the writes:
     * the log re-records exactly the same copies, which then execute alone. */
    assert(native_d3d12_buffer_write(&a, 0, bytes, 4096).status == NATIVE_OK);
    assert(native_d3d12_buffer_write(&b, 512, bytes, 64).status == NATIVE_OK);
    doe_dx_commands *open = NULL;
    assert(native_d3d12_adopt_writes(&device, &adopted, &taken).status == NATIVE_OK && taken);
    open = adopted.value.d3d12.commands;
    unsigned staging = open->ops[0].source;
    assert(doe_dx_commands_copy(open, a.handle.d3d12->allocation, 0, b.handle.d3d12->allocation, 0, 64, NULL) == DOE_DX_OK);
    native_d3d12_writes_rejected(&device, &adopted);
    assert(!adopted.value.d3d12.commands && !seen.submitted_count);
    seen.create_status = NATIVE_OUT_OF_MEMORY; /* A failed rebuild still loses nothing. */
    assert(native_d3d12_flush_writes(&device).status == NATIVE_OUT_OF_MEMORY && !seen.submitted_count);
    seen.create_status = NATIVE_OK;
    assert(native_d3d12_flush_writes(&device).status == NATIVE_OK && seen.submitted_count == 1);
    work = seen.submitted[0];
    assert(work->count == 2 && work->ops[0].source == staging);
    assert(copy_is(&work->ops[0], id(&a), 0, 4096) && copy_is(&work->ops[1], id(&b), 512, 64));
    retire();

    /* A second write into a logged destination submits the earlier copy first. */
    assert(native_d3d12_buffer_write(&a, 0, bytes, 4096).status == NATIVE_OK);
    assert(native_d3d12_buffer_write(&a, 64, bytes, 64).status == NATIVE_OK);
    assert(seen.submitted_count == 1 && seen.submitted[0]->count == 1 && copy_is(&seen.submitted[0]->ops[0], id(&a), 0, 4096));
    /* A rejected copy leaves the open command and the log unchanged. */
    seen.copy_status = DOE_DX_RANGE;
    assert(native_d3d12_buffer_write(&b, 0, bytes, 4096).status != NATIVE_OK);
    seen.copy_status = DOE_DX_OK;
    /* Any other recording (here a DEFAULT CPU read) submits the writes first. */
    unsigned char out[16];
    assert(native_d3d12_buffer_read(&b, 0, out, sizeof(out)).status == NATIVE_OK);
    assert(seen.submitted_count == 3 && seen.submitted[1]->count == 1 && copy_is(&seen.submitted[1]->ops[0], id(&a), 64, 64));
    assert(seen.submitted[2]->count == 1 && seen.submitted[2]->ops[0].source == id(&b));
    retire();

    /* READBACK state cannot be ordered in-list: the writes execute alone. */
    native_buffer readback = make(&device, 256, true);
    assert(native_d3d12_buffer_write(&readback, 0, bytes, 256).status == NATIVE_OK);
    assert(native_d3d12_adopt_writes(&device, &adopted, &taken).status == NATIVE_OK && !taken);
    assert(seen.submitted_count == 1 && copy_is(&seen.submitted[0]->ops[0], id(&readback), 0, 256));
    retire();

    /* Bounded retention: the 64th logged write or 16 MiB submit the command. */
    native_buffer many[64];
    for (unsigned i = 0; i < 64; ++i) {
        many[i] = make(&device, 256, false);
        assert(native_d3d12_buffer_write(&many[i], 0, bytes, 256).status == NATIVE_OK);
        assert(seen.submitted_count == (i == 63 ? 1u : 0u));
    }
    assert(seen.submitted[0]->count == 64 && copy_is(&seen.submitted[0]->ops[63], id(&many[63]), 0, 256));
    retire();
    native_buffer large = make(&device, UINT64_C(16) << 20, false);
    static unsigned char block[16u << 20];
    assert(native_d3d12_buffer_write(&large, 0, block, sizeof(block)).status == NATIVE_OK);
    assert(seen.submitted_count == 1);
    retire();

    /* Final release discards a never-observed open command and every lease. */
    assert(native_d3d12_buffer_write(&a, 0, bytes, 4096).status == NATIVE_OK);
    native_d3d12_buffer_close(&a);
    native_d3d12_buffer_close(&b);
    native_d3d12_buffer_close(&readback);
    native_d3d12_buffer_close(&large);
    for (unsigned i = 0; i < 64; ++i) native_d3d12_buffer_close(&many[i]);
    assert(seen.live && seen.live_commands == 1);
    native_d3d12_writes_close(&device.backend);
    assert(!seen.live && !seen.live_commands && !seen.submitted_count && !device.backend.value.d3d12.writes);
    puts("PASS actual Windows queue-write batch owner: one open command in call order, adoption with ordering before the submission's work, rejected adoption re-recorded exactly and retained across a failed rebuild, same-destination and observation flushes, rejected copies, READBACK separation, 64-write and 16 MiB bounds and discard at final release; call ledger only, no GPU");
    return 0;
}
