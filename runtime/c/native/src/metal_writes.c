/* SPDX-License-Identifier: MIT
 * Metal queue writes to a buffer that submitted or recorded work still uses
 * record a staged copy into one open per-device command instead of creating
 * and committing a command buffer each. Every settlement (poll, wait, map,
 * work done, drain) and every other submission commits it first, so queue
 * order and every owner wait are preserved: the open command owns its
 * destinations exactly as a committed one does. Dawn likewise submits pending
 * uploads with the next submission or at 16 MiB outstanding. Caller holds the
 * device mutex. */
#include "internal.h"
#include <stdlib.h>

enum { METAL_WRITE_CAPACITY = 256 };
#define METAL_WRITE_BYTES_LIMIT (UINT64_C(16) << 20)

native_failure native_flush_metal_writes(native_device *device)
{
    if (!device->metal_writes.kind) return native_failure_status(NATIVE_OK);
    native_command_handle commands = device->metal_writes;
    device->metal_writes = (native_command_handle){0};
    device->metal_write_count = 0;
    device->metal_write_bytes = 0;
    /* The pending owner adopts it on every result, including unknown completion. */
    return native_submit_backend_commands_async(device, commands);
}

void native_release_metal_writes(native_device *device)
{
    native_command_handle commands = device->metal_writes;
    device->metal_writes = (native_command_handle){0};
    /* Never submitted: the release undoes its acquire. */
    if (native_command_release(&commands) != NATIVE_OK) abort();
}

native_failure native_metal_write_record(native_device *device, native_buffer *destination,
    uint64_t offset, const void *bytes, size_t size)
{
    if (!size) return native_failure_status(NATIVE_OK);
    doe_mtl_buffer *staging = NULL;
    doe_mtl_error error = {0};
    doe_mtl_status status = doe_mtl_buffer_create(device->backend.value.metal.context,
        size, DOE_MTL_SHARED, &staging, &error);
    if (status == DOE_MTL_OK) status = doe_mtl_buffer_write(staging, 0, bytes, size);
    native_failure failure = native_failure_metal(status, &error);
    if (failure.status == NATIVE_OK && !device->metal_writes.kind)
        failure = native_command_create(&device->backend, &device->metal_writes);
    /* Copy admission precedes native recording: a rejected copy leaves the
     * open command and every earlier write unchanged. */
    if (failure.status == NATIVE_OK) {
        status = doe_mtl_commands_copy(device->metal_writes.value.metal, staging, 0,
            destination->handle.metal, offset, size, &error);
        failure = native_failure_metal(status, &error);
    }
    if (staging) doe_mtl_buffer_release(staging); /* The command holds its own lease. */
    if (failure.status != NATIVE_OK) return failure;
    device->metal_write_count += 1;
    device->metal_write_bytes += size;
    if (device->metal_write_count >= METAL_WRITE_CAPACITY || device->metal_write_bytes >= METAL_WRITE_BYTES_LIMIT)
        return native_flush_metal_writes(device);
    return failure;
}
