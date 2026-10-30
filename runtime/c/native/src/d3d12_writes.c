/* SPDX-License-Identifier: MIT
 * D3D12 queue writes record into one open per-device command instead of one
 * command list and submission each. The next queue submission adopts that
 * command and records its own work after the writes, so both execute in one
 * ExecuteCommandLists call under one fence. Every other recording, CPU read,
 * map or work-done wait first submits it on its own. A copy log (staging and
 * destination leases) rebuilds the writes if an adopted recording fails, so a
 * rejected submission never loses earlier writes. Caller holds the device mutex. */
#include "d3d12_frontend.h"
#include <stdlib.h>

#if defined(_WIN32)
enum { WRITE_CAPACITY = 64 };
/* Dawn submits pending uploads once 16 MiB are outstanding (DynamicUploader.cpp:138-158). */
#define WRITE_BYTES_LIMIT (UINT64_C(16) << 20)

typedef struct native_d3d12_write {
    doe_dx_buffer *staging, *destination;
    uint64_t offset, size;
} native_d3d12_write;
struct native_d3d12_writes {
    doe_dx_commands *open; /* Records every logged copy; NULL after an adopted discard. */
    size_t count;
    uint64_t bytes;
    native_d3d12_write log[WRITE_CAPACITY];
};

static native_command_handle handle(native_device *device, doe_dx_commands *commands)
{
    return (native_command_handle){.kind=WGPUBackendType_D3D12,
        .value.d3d12={device->backend.value.d3d12.context, commands}};
}

static native_failure record(doe_dx_commands *commands, const native_d3d12_write *write)
{
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_commands_copy(commands, write->staging, 0,
        write->destination, write->offset, write->size, &error);
    return native_failure_d3d12(status, &error);
}

/* Re-records the whole log into a fresh command when none is open. Failure
 * leaves the log, and so every accepted write, intact for the next attempt. */
static native_failure ensure_open(native_device *device, native_d3d12_writes *writes)
{
    if (writes->open) return native_failure_status(NATIVE_OK);
    native_command_handle command = {0};
    native_failure failure = native_command_create(&device->backend, &command);
    for (size_t i = 0; failure.status == NATIVE_OK && i < writes->count; ++i)
        failure = record(command.value.d3d12.commands, &writes->log[i]);
    if (failure.status != NATIVE_OK) {
        if (native_command_release(&command) != NATIVE_OK) abort();
        return failure;
    }
    writes->open = command.value.d3d12.commands;
    return failure;
}

static void drop_log(native_d3d12_writes *writes)
{
    for (size_t i = 0; i < writes->count; ++i) {
        doe_dx_buffer_release(writes->log[i].staging);
        doe_dx_buffer_release(writes->log[i].destination);
    }
    writes->count = 0;
    writes->bytes = 0;
}
#endif

native_failure native_d3d12_flush_writes(native_device *device)
{
#if defined(_WIN32)
    if (device->backend.kind != WGPUBackendType_D3D12) return native_failure_status(NATIVE_OK);
    native_d3d12_writes *writes = device->backend.value.d3d12.writes;
    if (!writes || !writes->count) return native_failure_status(NATIVE_OK);
    native_failure failure = ensure_open(device, writes);
    if (failure.status != NATIVE_OK) return failure;
    native_command_handle command = handle(device, writes->open);
    writes->open = NULL;
    drop_log(writes); /* The command holds its own staging/destination leases. */
    /* The pending owner adopts it on every result, including unknown completion. */
    return native_submit_backend_commands_async(device, command);
#else
    (void)device;
    return native_failure_status(NATIVE_OK);
#endif
}

native_failure native_d3d12_prepare_recording(native_device *device)
{
    native_failure failure = native_d3d12_materialize_zero(device);
    return failure.status == NATIVE_OK ? native_d3d12_flush_writes(device) : failure;
}

#if defined(_WIN32)
native_failure native_d3d12_write_record(native_device *device, doe_dx_buffer *staging,
    doe_dx_buffer *destination, uint64_t offset, uint64_t size)
{
    native_d3d12_writes *writes = device->backend.value.d3d12.writes;
    if (!writes) {
        writes = calloc(1, sizeof(*writes));
        if (!writes) return native_failure_status(NATIVE_OUT_OF_MEMORY);
        device->backend.value.d3d12.writes = writes;
    }
    /* Two copies into one COPY_DEST buffer are not ordered within a list: a
     * second write into a logged destination submits the earlier ones first. */
    for (size_t i = 0; i < writes->count; ++i) {
        if (writes->log[i].destination != destination) continue;
        native_failure flushed = native_d3d12_flush_writes(device);
        if (flushed.status != NATIVE_OK) return flushed;
        break;
    }
    native_failure failure = ensure_open(device, writes);
    if (failure.status != NATIVE_OK) return failure;
    native_d3d12_write write = {staging, destination, offset, size};
    if (doe_dx_buffer_retain(staging) != DOE_DX_OK) return native_failure_status(NATIVE_FAILURE);
    if (doe_dx_buffer_retain(destination) != DOE_DX_OK) {
        doe_dx_buffer_release(staging);
        return native_failure_status(NATIVE_FAILURE);
    }
    /* Copy admission precedes native recording: a rejected copy leaves the
     * command and every earlier write unchanged. */
    failure = record(writes->open, &write);
    if (failure.status != NATIVE_OK) {
        doe_dx_buffer_release(staging);
        doe_dx_buffer_release(destination);
        return failure;
    }
    writes->log[writes->count++] = write;
    writes->bytes += size;
    if (writes->count == WRITE_CAPACITY || writes->bytes >= WRITE_BYTES_LIMIT)
        return native_d3d12_flush_writes(device);
    return failure;
}

native_failure native_d3d12_adopt_writes(native_device *device, native_command_handle *out,
    bool *adopted)
{
    *adopted = false;
    native_d3d12_writes *writes = device->backend.value.d3d12.writes;
    if (!writes || !writes->count) return native_failure_status(NATIVE_OK);
    native_failure failure = ensure_open(device, writes);
    if (failure.status != NATIVE_OK) return failure;
    /* The submission's own copies into a written buffer must follow the writes. */
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_commands_order_buffer_writes(writes->open, &error);
    /* A READBACK destination cannot transition: keep a separate execution. */
    if (status == DOE_DX_UNSUPPORTED) return native_d3d12_flush_writes(device);
    if (status != DOE_DX_OK) return native_failure_d3d12(status, &error);
    *out = handle(device, writes->open);
    writes->open = NULL;
    *adopted = true;
    return native_failure_status(NATIVE_OK);
}

void native_d3d12_writes_recorded(native_device *device)
{
    drop_log(device->backend.value.d3d12.writes);
}

void native_d3d12_writes_rejected(native_device *device, native_command_handle *adopted)
{
    (void)device;
    /* Partial native records cannot be removed from a list: discard it whole.
     * The retained log re-records the writes before any later use. */
    if (native_command_release(adopted) != NATIVE_OK) abort();
}
#endif

void native_d3d12_writes_close(native_backend *backend)
{
#if defined(_WIN32)
    native_d3d12_writes *writes = backend->value.d3d12.writes;
    if (!writes) return;
    /* Final device release: no remaining owner can observe these writes. */
    if (writes->open) doe_dx_commands_release(writes->open);
    drop_log(writes);
    free(writes);
    backend->value.d3d12.writes = NULL;
#else
    (void)backend;
#endif
}
