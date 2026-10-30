/* SPDX-License-Identifier: MIT
 * Actual caller/command/cache owner controls; native-call seam, no GPU oracle. */
#include "mock_native.h"
#include <stdio.h>

enum { PAGE_BYTES = 1024 * 1024 };

static doe_dx_context *create_context(void)
{
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    return context;
}

static void caller_leases_and_independent_zero_cache(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *first = NULL, *busy = NULL, *large = NULL, *zero = NULL;
    assert(doe_dx_upload_acquire(context,64,&first,NULL) == DOE_DX_OK);
    assert(first == context->upload_staging && first->references == 2);
    assert(first->size == PAGE_BYTES && first->initialized && !first->immutable_zero);
    assert(first->memory == DOE_DX_UPLOAD && !first->owner && !first->pending);
    assert(doe_dx_upload_acquire(context,19,&busy,NULL) == DOE_DX_OK);
    assert(busy != first && busy->size == 19 && busy->references == 1);
    assert(context->upload_staging == first && first->references == 2);
    unsigned char data = 0x6d;
    mock.failure = MOCK_WRITE;
    assert(doe_dx_buffer_write(first,0,&data,1,NULL) == DOE_DX_NATIVE_FAILURE);
    mock.failure = MOCK_NONE;
    assert(doe_dx_buffer_write(first,PAGE_BYTES-1,&data,1,NULL) == DOE_DX_OK);
    assert(doe_dx_zero_upload_acquire(context,64,&zero,NULL) == DOE_DX_OK);
    assert(zero != first && zero == context->zero_upload && zero->immutable_zero);
    assert(doe_dx_buffer_write(zero,0,&data,1,NULL) == DOE_DX_UNSUPPORTED);
    doe_dx_buffer_release(zero);
    doe_dx_buffer_release(busy);
    doe_dx_buffer_release(first);
    size_t attempts = mock.allocation_attempts;
    doe_dx_buffer *reused = NULL;
    assert(doe_dx_upload_acquire(context,32,&reused,NULL) == DOE_DX_OK);
    assert(reused == first && reused->references == 2 && mock.allocation_attempts == attempts);
    doe_dx_buffer_release(reused);
    assert(doe_dx_upload_acquire(context,PAGE_BYTES+4u,&large,NULL) == DOE_DX_OK);
    assert(large != first && large->size == PAGE_BYTES+4u && large->references == 1);
    assert(context->upload_staging == first && first->references == 1);
    doe_dx_buffer_release(large);
    assert(doe_dx_upload_acquire(context,64,&reused,NULL) == DOE_DX_OK);
    assert(reused == first);
    /* Cache destruction releases only its lease, including the immutable cache. */
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    assert(!context->upload_staging && !context->zero_upload && reused->references == 1);
    assert(mock.buffers == 1);
    doe_dx_buffer_release(reused);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void admission_and_page_limits(bool allocation_limit)
{
    doe_dx_context *context = create_context();
    context->caps.limits.max_buffer_size = allocation_limit ? 131072u : 65536u;
    context->caps.limits.max_buffer_allocation_bytes = allocation_limit ? 65536u : 131072u;
    doe_dx_buffer *buffer = NULL;
    assert(doe_dx_upload_acquire(context,4,&buffer,NULL) == DOE_DX_OK);
    assert(buffer->size == 65536 && buffer == context->upload_staging);
    doe_dx_buffer *untouched = buffer;
    size_t attempts = mock.allocation_attempts;
    assert(doe_dx_upload_acquire(NULL,4,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_upload_acquire(context,0,&untouched,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_upload_acquire(context,4,NULL,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_upload_acquire(context,UINT64_MAX,&untouched,NULL) == DOE_DX_RANGE);
    assert(doe_dx_upload_acquire(context,context->caps.limits.max_buffer_size+1,&untouched,NULL) == DOE_DX_RANGE);
    assert(untouched == buffer && mock.allocation_attempts == attempts);
    doe_dx_buffer_release(buffer);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void speculative_oom_retry_and_growth(void)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *small = NULL;
    size_t attempts = mock.allocation_attempts;
    mock.allocation_failure = attempts+1;
    assert(doe_dx_upload_acquire(context,64,&small,NULL) == DOE_DX_OK);
    assert(mock.allocation_attempts == attempts+2); /* Larger page OOM, exact retry. */
    assert(small == context->upload_staging && small->size == 64 && small->references == 2);
    mock.allocation_failure = 0;
    doe_dx_buffer_release(small);
    doe_dx_buffer *untouched = small;
    size_t allocations = mock.allocations;
    attempts = mock.allocation_attempts;
    mock.allocation_failure = attempts+1;
    mock.failure = MOCK_BUFFER; /* Page wrapper OOM, exact native allocation failure. */
    doe_dx_error error = {0};
    assert(doe_dx_upload_acquire(context,128,&untouched,&error) == DOE_DX_NATIVE_FAILURE);
    assert(error.status == DOE_DX_NATIVE_FAILURE && !strcmp(error.operation,"mock.injected"));
    assert(mock.allocation_attempts == attempts+2 && mock.allocations == allocations);
    assert(untouched == small && context->upload_staging == small && small->references == 1);
    assert(mock.buffers == 1 && small->size == 64);
    mock.allocation_failure = 0;
    attempts = mock.allocation_attempts;
    assert(doe_dx_upload_acquire(context,128,&untouched,NULL) == DOE_DX_NATIVE_FAILURE);
    assert(mock.allocation_attempts == attempts+1); /* Non-OOM never retries. */
    assert(untouched == small && context->upload_staging == small && mock.allocations == allocations);
    mock.failure = MOCK_NONE;
    attempts = mock.allocation_attempts;
    mock.allocation_failure = attempts+1;
    assert(doe_dx_upload_acquire(context,PAGE_BYTES,&untouched,NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(mock.allocation_attempts == attempts+1); /* Already exact: no OOM retry. */
    assert(untouched == small && context->upload_staging == small && mock.allocations == allocations);
    mock.allocation_failure = 0;
    doe_dx_buffer *grown = NULL;
    assert(doe_dx_upload_acquire(context,128,&grown,NULL) == DOE_DX_OK);
    /* The old cached wrapper may be freed/reused by malloc; check owner/counts. */
    assert(grown == context->upload_staging && grown->size == PAGE_BYTES && grown->references == 2);
    assert(mock.buffers == 1 && mock.allocations == allocations);
    doe_dx_buffer_release(grown);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void recorded_and_pending_exclude_reuse(bool unknown_signal, bool lose)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *staging = NULL, *destination = NULL, *other = NULL;
    doe_dx_commands *commands = NULL;
    assert(doe_dx_upload_acquire(context,64,&staging,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&destination,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,staging,0,destination,0,64,NULL) == DOE_DX_OK);
    doe_dx_buffer_release(staging); /* The cache and recorded command now own it. */
    assert(staging->references == 2 && staging->owner == commands);
    assert(doe_dx_upload_acquire(context,32,&other,NULL) == DOE_DX_OK);
    assert(other != staging && other->size == 32 && other->references == 1);
    doe_dx_buffer_release(other);
    unsigned char data = 0x7b;
    assert(doe_dx_buffer_write(staging,0,&data,1,NULL) == DOE_DX_BUSY);
    if (unknown_signal) mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL) ==
        (unknown_signal ? DOE_DX_COMPLETION_UNKNOWN : DOE_DX_OK));
    mock.failure = MOCK_NONE;
    assert(staging->owner == commands || staging->pending == commands);
    assert(doe_dx_buffer_write(staging,0,&data,1,NULL) == DOE_DX_BUSY);
    size_t waits = mock.waits, signals = mock.signals;
    assert(doe_dx_upload_acquire(context,17,&other,NULL) == DOE_DX_OK);
    assert(other != staging && other->size == 17 && other->references == 1);
    assert(mock.waits == waits && mock.signals == signals && context->upload_staging == staging);
    doe_dx_buffer_release(other);
    doe_dx_buffer_release(destination);
    doe_dx_commands_release(commands);
    if (lose) {
        mock.lost = true;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
        other = staging;
        size_t attempts = mock.allocation_attempts;
        assert(doe_dx_upload_acquire(context,64,&other,NULL) == DOE_DX_DEVICE_LOST);
        assert(other == staging && mock.allocation_attempts == attempts);
        assert(doe_dx_buffer_write(staging,0,&data,1,NULL) == DOE_DX_DEVICE_LOST);
    } else {
        mock.completed = 100; /* Also covers a known-fence recovery signal. */
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
        assert(staging->references == 1 && !staging->owner && !staging->pending);
        size_t attempts = mock.allocation_attempts;
        assert(doe_dx_upload_acquire(context,64,&other,NULL) == DOE_DX_OK);
        assert(other == staging && mock.allocation_attempts == attempts);
        assert(doe_dx_buffer_write(other,0,&data,1,NULL) == DOE_DX_OK);
        doe_dx_buffer_release(other);
    }
    assert(mock.buffers == 1 && !mock.commands);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void teardown_pending_lease(bool lose)
{
    doe_dx_context *context = create_context();
    doe_dx_buffer *staging = NULL, *destination = NULL;
    doe_dx_commands *commands = NULL;
    assert(doe_dx_upload_acquire(context,64,&staging,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&destination,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,staging,0,destination,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_OK);
    doe_dx_buffer_release(staging);
    doe_dx_buffer_release(destination);
    doe_dx_commands_release(commands);
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    assert(!context->upload_staging && mock.buffers == 2 && mock.commands == 1);
    if (lose) {
        mock.lost = true;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
    } else {
        mock.completed = 1;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    }
    assert(!mock.buffers && !mock.commands);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

int main(void)
{
    mock_reset();
    caller_leases_and_independent_zero_cache();
    admission_and_page_limits(false);
    admission_and_page_limits(true);
    speculative_oom_retry_and_growth();
    recorded_and_pending_exclude_reuse(false,false);
    recorded_and_pending_exclude_reuse(true,false);
    recorded_and_pending_exclude_reuse(false,true);
    recorded_and_pending_exclude_reuse(true,true);
    teardown_pending_lease(false);
    teardown_pending_lease(true);
    puts("PASS D3D12 caller upload-staging owner: caller/recording/pending exclusion, completion/recovery reuse, exact uncached sizes, bounded cache, OOM retry/rollback, zero-cache isolation and terminal leases; no SDK/GPU/data oracle");
    return 0;
}
