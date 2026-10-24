/* SPDX-License-Identifier: MIT */
#include "d3d12_indirect.h"
#include "d3d12_texture.h"
#include "d3d12_graphics_internal.h"

#if defined(_WIN32)
static native_failure record_clear(native_command_handle *command, const doe_resource_command *record)
{
    native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
    doe_dx_buffer *zero = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_buffer_create(command->value.d3d12.context,
        record->value.clear.size, DOE_DX_UPLOAD, &zero, &error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy(command->value.d3d12.commands, zero, 0,
            buffer->handle.d3d12->allocation, record->value.clear.offset, record->value.clear.size, &error);
    native_failure failure = native_failure_d3d12(status, &error);
    /* Copy recording acquired its own private lease before this release. */
    doe_dx_buffer_release(zero);
    return failure;
}
#endif

native_failure native_d3d12_record_commands(native_command_handle *commands, doe_resource_commands *resources)
{
#if defined(_WIN32)
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(resources, &count);
    for (size_t i = 0; i < count; ++i) {
        const doe_resource_command *record = &records[i];
        native_failure failure;
        switch (record->kind) {
        case DOE_COMMAND_COPY_BUFFER: {
            if (!record->value.copy.size) continue;
            native_buffer *source = doe_resource_buffer_native(record->value.copy.source);
            native_buffer *destination = doe_resource_buffer_native(record->value.copy.destination);
            doe_dx_error error = {0};
            doe_dx_status status = doe_dx_commands_copy(commands->value.d3d12.commands,
                source->handle.d3d12->allocation, record->value.copy.source_offset,
                destination->handle.d3d12->allocation, record->value.copy.destination_offset,
                record->value.copy.size, &error);
            failure = native_failure_d3d12(status, &error);
            break;
        }
        case DOE_COMMAND_CLEAR_BUFFER:
            if (!record->value.clear.size) continue;
            failure = record_clear(commands, record);
            break;
        case DOE_COMMAND_DISPATCH:
            failure = native_d3d12_dispatch(commands, record);
            break;
        case DOE_COMMAND_DISPATCH_INDIRECT:
            failure = native_d3d12_dispatch_indirect(commands, record);
            break;
        case DOE_COMMAND_RENDER: {
            size_t consumed=0;
            failure=native_d3d12_render(commands,&records[i],count-i,&consumed);
            if (failure.status==NATIVE_OK) {
                if (!consumed || consumed>count-i) return native_failure_status(NATIVE_INVALID_STATE);
                i+=consumed-1;
            }
            break;
        }
        case DOE_COMMAND_BUFFER_TEXTURE:
        case DOE_COMMAND_COPY_TEXTURE:
            failure = native_d3d12_texture_copy(commands,record);
            break;
        default:
            return native_failure_status(NATIVE_UNSUPPORTED);
        }
        if (failure.status != NATIVE_OK) return failure;
    }
    return native_failure_status(NATIVE_OK);
#else
    (void)commands;
    (void)resources;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
