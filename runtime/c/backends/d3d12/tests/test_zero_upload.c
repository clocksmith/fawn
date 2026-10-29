/* SPDX-License-Identifier: MIT
 * Actual resource/context owner; typed native-call seam, no GPU/data simulation. */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *create_context(void)
{
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    return context;
}

static void reuse_growth_and_failures(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *first = NULL, *same = NULL, *grown = NULL, *large = NULL;
    assert(doe_dx_zero_upload_acquire(context,64,&first,NULL) == DOE_DX_OK);
    assert(first == context->zero_upload && first->references == 2);
    assert(first->immutable_zero && first->initialized && first->memory == DOE_DX_UPLOAD);
    size_t attempts = mock.allocation_attempts;
    assert(doe_dx_zero_upload_acquire(context,16,&same,NULL) == DOE_DX_OK);
    assert(same == first && same->references == 3 && mock.allocation_attempts == attempts);
    unsigned char poison = 19;
    mock.failure = MOCK_WRITE;
    assert(doe_dx_buffer_write(first,0,&poison,1,NULL) == DOE_DX_UNSUPPORTED);
    mock.failure = MOCK_NONE;
    assert(doe_dx_zero_upload_acquire(context,256,&grown,NULL) == DOE_DX_OK);
    assert(grown != first && context->zero_upload == grown && grown->references == 2);
    assert(first->references == 2 && mock.buffers == 2);
    doe_dx_buffer_release(first);
    doe_dx_buffer_release(same);
    assert(mock.buffers == 1);
    doe_dx_buffer *untouched = grown;
    mock.failure = MOCK_BUFFER;
    assert(doe_dx_zero_upload_acquire(context,512,&untouched,NULL) == DOE_DX_NATIVE_FAILURE);
    assert(untouched == grown && context->zero_upload == grown && grown->references == 2);
    mock.failure = MOCK_NONE;
    attempts = mock.allocation_attempts;
    mock.allocation_failure = attempts + 1;
    assert(doe_dx_zero_upload_acquire(context,512,&untouched,NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(untouched == grown && context->zero_upload == grown && grown->references == 2);
    mock.allocation_failure = 0;
    assert(doe_dx_zero_upload_acquire(context,0,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_zero_upload_acquire(NULL,4,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_zero_upload_acquire(context,4,NULL,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_zero_upload_acquire(context,UINT64_MAX,&untouched,NULL) == DOE_DX_RANGE);
    assert(untouched == grown && context->zero_upload == grown);
    assert(doe_dx_zero_upload_acquire(context,1024u*1024u+4u,&large,NULL) == DOE_DX_OK);
    assert(large != grown && large->references == 1 && large->immutable_zero);
    assert(context->zero_upload == grown && doe_dx_buffer_size(large) == 1024u*1024u+4u);
    doe_dx_buffer_release(large);
    assert(mock.buffers == 1);
    /* Destruction drops only the cache lease: the returned caller lease lives. */
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    assert(!context->zero_upload && grown->references == 1 && mock.buffers == 1);
    doe_dx_buffer_release(grown);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void pending_growth_and_teardown(bool lose)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *zero = NULL, *replacement = NULL, *destination = NULL;
    doe_dx_commands *commands = NULL;
    assert(doe_dx_zero_upload_acquire(context,64,&zero,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&destination,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,zero,0,destination,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_OK);
    assert(doe_dx_zero_upload_acquire(context,256,&replacement,NULL) == DOE_DX_OK);
    assert(replacement != zero && zero->references == 2); /* Caller plus pending command. */
    doe_dx_buffer_release(zero);
    doe_dx_buffer_release(destination);
    doe_dx_buffer_release(replacement);
    doe_dx_commands_release(commands);
    assert(mock.buffers == 3 && mock.commands == 1);
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    assert(!context->zero_upload && mock.buffers == 2 && mock.commands == 1);
    if (lose) {
        doe_dx_buffer *untouched = NULL;
        mock.lost = true;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
        assert(doe_dx_zero_upload_acquire(context,64,&untouched,NULL) == DOE_DX_DEVICE_LOST);
        assert(!untouched);
    } else {
        mock.completed = 1;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    }
    assert(!mock.buffers && !mock.commands);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void failed_native_close_remains_retryable(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *zero = NULL;
    assert(doe_dx_zero_upload_acquire(context,64,&zero,NULL) == DOE_DX_OK);
    doe_dx_buffer_release(zero);
    mock.close_status = DOE_DX_COMPLETION_UNKNOWN;
    assert(doe_dx_context_destroy(context) == DOE_DX_COMPLETION_UNKNOWN);
    assert(!context->zero_upload && !context->objects && !mock.buffers && mock.contexts == 1);
    mock.close_status = DOE_DX_OK;
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

int main(void)
{
    mock_reset();
    reuse_growth_and_failures();
    pending_growth_and_teardown(false);
    pending_growth_and_teardown(true);
    failed_native_close_remains_retryable();
    puts("PASS D3D12 immutable zero-upload owner: reuse/growth, denied writes, failed preparation, bounded retention, outstanding caller/GPU leases and terminal cleanup; no SDK/GPU");
    return 0;
}
