/* SPDX-License-Identifier: MIT
 * Actual owner controls at the typed native-call seam, not a GPU/data oracle. */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *create_context(void)
{
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    return context;
}

static void admission_and_completed_initialization(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *ordinary = NULL, *zeroed = NULL;
    unsigned char byte = 0xa7;
    assert(doe_dx_buffer_create(context,257,DOE_DX_READBACK,&ordinary,NULL) == DOE_DX_OK);
    assert(!ordinary->initialized && !mock.readback_zeroes);
    assert(doe_dx_buffer_read(ordinary,0,&byte,1,NULL) == DOE_DX_UNINITIALIZED);
    assert(byte == 0xa7);
    assert(doe_dx_buffer_create_readback_zeroed(context,257,&zeroed,NULL) == DOE_DX_OK);
    assert(zeroed->context == context && zeroed->memory == DOE_DX_READBACK);
    assert(zeroed->initialized && !zeroed->accepted_initialized && !zeroed->owner && !zeroed->pending);
    assert(zeroed->references == 1 && doe_dx_buffer_size(zeroed) == 257);
    assert(mock.readback_zeroes == 1 && !mock.copies && !mock.executes && !mock.signals);
    assert(doe_dx_buffer_write(zeroed,0,&byte,1,NULL) == DOE_DX_UNSUPPORTED);
    /* Native-read failure proves admission reaches that seam; no fake zero data. */
    mock.failure = MOCK_READ;
    assert(doe_dx_buffer_read(zeroed,256,&byte,1,NULL) == DOE_DX_NATIVE_FAILURE);
    assert(byte == 0xa7);
    mock.failure = MOCK_NONE;
    doe_dx_buffer *untouched = zeroed;
    assert(doe_dx_buffer_create_readback_zeroed(NULL,4,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_buffer_create_readback_zeroed(context,0,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_buffer_create_readback_zeroed(context,4,NULL,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_buffer_create_readback_zeroed(context,UINT64_MAX,&untouched,NULL) == DOE_DX_RANGE);
    assert(untouched == zeroed && mock.readback_zeroes == 1 && context->objects == 2);
    doe_dx_buffer_release(ordinary);
    doe_dx_buffer_release(zeroed);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void construction_rollback(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *sentinel = NULL;
    assert(doe_dx_buffer_create(context,4,DOE_DX_UPLOAD,&sentinel,NULL) == DOE_DX_OK);
    size_t allocations = mock.allocations;
    doe_dx_buffer *untouched = sentinel;
    doe_dx_error error = {0};
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&untouched,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(error.status == DOE_DX_OUT_OF_MEMORY && !strcmp(error.operation,"buffer.allocate"));
    mock.allocation_failure = 0;
    assert(untouched == sentinel && mock.allocations == allocations && context->objects == 1);
    mock.failure = MOCK_BUFFER;
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&untouched,&error) == DOE_DX_NATIVE_FAILURE);
    assert(!mock.readback_zeroes && untouched == sentinel && mock.buffers == 1);
    assert(mock.allocations == allocations && context->objects == 1);
    mock.failure = MOCK_ZERO_READBACK;
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&untouched,&error) == DOE_DX_NATIVE_FAILURE);
    assert(error.status == DOE_DX_NATIVE_FAILURE && error.native_code == -100-(int32_t)MOCK_ZERO_READBACK);
    assert(!strcmp(error.operation,"mock.injected"));
    assert(mock.readback_zeroes == 1 && untouched == sentinel && mock.buffers == 1);
    assert(mock.allocations == allocations && context->objects == 1 && !context->lost);
    mock.failure = MOCK_NONE;
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&untouched,NULL) == DOE_DX_OK);
    assert(untouched != sentinel && untouched->initialized && mock.readback_zeroes == 2);
    doe_dx_buffer_release(untouched);
    doe_dx_buffer_release(sentinel);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void native_map_loss_rolls_back(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *sentinel = NULL;
    assert(doe_dx_buffer_create(context,4,DOE_DX_UPLOAD,&sentinel,NULL) == DOE_DX_OK);
    doe_dx_buffer *untouched = sentinel;
    doe_dx_error error = {0};
    mock.readback_zero_status = DOE_DX_DEVICE_LOST;
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&untouched,&error) == DOE_DX_DEVICE_LOST);
    assert(error.status == DOE_DX_DEVICE_LOST && error.native_code == -201);
    assert(!strcmp(error.operation,"mock.readback_zero"));
    assert(context->lost && context->objects == 1 && mock.buffers == 1 && untouched == sentinel);
    assert(mock.readback_zeroes == 1);
    size_t attempts = mock.allocation_attempts;
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&untouched,NULL) == DOE_DX_DEVICE_LOST);
    assert(untouched == sentinel && mock.allocation_attempts == attempts && mock.readback_zeroes == 1);
    doe_dx_buffer_release(sentinel);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void later_gpu_use_excludes_cpu(bool lose)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *upload = NULL, *readback = NULL;
    doe_dx_commands *commands = NULL;
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create_readback_zeroed(context,64,&readback,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,upload,0,readback,8,16,NULL) == DOE_DX_OK);
    unsigned char untouched = 0x6d;
    assert(readback->initialized && readback->owner == commands);
    assert(doe_dx_buffer_read(readback,0,&untouched,1,NULL) == DOE_DX_BUSY);
    assert(untouched == 0x6d);
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_OK);
    assert(readback->initialized && !readback->owner && readback->pending == commands);
    assert(doe_dx_buffer_read(readback,0,&untouched,1,NULL) == DOE_DX_BUSY);
    assert(untouched == 0x6d && mock.readback_zeroes == 1);
    doe_dx_commands_release(commands);
    doe_dx_buffer_release(upload);
    assert(mock.buffers == 2 && mock.commands == 1);
    if (lose) {
        mock.lost = true;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
        assert(doe_dx_buffer_read(readback,0,&untouched,1,NULL) == DOE_DX_DEVICE_LOST);
    } else {
        mock.completed = mock.last_signal;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
        assert(readback->initialized && !readback->owner && !readback->pending);
        mock.failure = MOCK_READ;
        assert(doe_dx_buffer_read(readback,0,&untouched,1,NULL) == DOE_DX_NATIVE_FAILURE);
        mock.failure = MOCK_NONE;
    }
    assert(untouched == 0x6d && mock.buffers == 1 && !mock.commands);
    doe_dx_buffer_release(readback);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

int main(void)
{
    mock_reset();
    admission_and_completed_initialization();
    construction_rollback();
    native_map_loss_rolls_back();
    later_gpu_use_excludes_cpu(false);
    later_gpu_use_excludes_cpu(true);
    puts("PASS D3D12 zeroed READBACK owner: admission, rollback, Map loss, completed initialization and later CPU exclusion/lifetime; no SDK/GPU/data oracle");
    return 0;
}
