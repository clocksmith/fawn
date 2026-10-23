/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include "../src/internal.h"
#include <sched.h>

static doe_resource_commands *copy_commands(fixture *f, doe_resource_buffer *source,
    doe_resource_buffer *destination)
{
    doe_resource_config c = config(f);
    doe_resource_encoder *encoder = NULL;
    doe_resource_commands *commands = NULL;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    doe_resource_buffer_copy copy = {source, destination, 0, 0, 64};
    assert(doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    return commands;
}

static unsigned references(const doe_resource_commands *commands)
{
    return atomic_load_explicit(&commands->base.references, memory_order_acquire);
}

static void abort_then_submit(void)
{
    fixture f = fresh();
    native_token a = {0}, b = {0}, unadopted = {0};
    doe_resource_buffer *source = buffer(&f, &a, 64, WGPUBufferUsage_CopySrc);
    doe_resource_buffer *destination = buffer(&f, &b, 64, WGPUBufferUsage_CopyDst);
    doe_resource_commands *commands = copy_commands(&f, source, destination);
    assert(references(commands) == 1);
    assert(doe_resource_commands_publish_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_OK);
    assert(references(commands) == 2);
    assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_claim_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_STATE);
    assert(references(commands) == 2);

    /* A real constructor allocation failure during unpublished preparation must
     * leave the same caller-owned command buffer available for fallback. */
    doe_resource_config c = config(&f);
    doe_resource_buffer_desc desc = {64, WGPUBufferUsage_CopySrc, false};
    doe_resource_buffer *failed = source;
    f.fail_at = f.allocations;
    assert(doe_resource_buffer_create(&c, &desc, native(&f, &unadopted), &failed) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(failed == source && !unadopted.releases && references(commands) == 2);
    size_t acquisitions = f.allocations;
    assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_OK);
    assert(references(commands) == 1 && f.allocations == acquisitions);
    assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_publish_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    assert(references(commands) == 2);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(references(commands) == 2);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(references(commands) == 1);
    assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands);
    doe_resource_buffer_release(source);
    doe_resource_buffer_release(destination);
    assert(a.releases == 1 && b.releases == 1);
    empty(&f);
}

static void publish_then_release(void)
{
    for (unsigned active = 0; active < 2; ++active) {
        fixture f = fresh();
        native_token a = {0}, b = {0};
        doe_resource_buffer *source = buffer(&f, &a, 64, WGPUBufferUsage_CopySrc);
        doe_resource_buffer *destination = buffer(&f, &b, 64, WGPUBufferUsage_CopyDst);
        doe_resource_commands *commands = copy_commands(&f, source, destination);
        assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_OK);
        assert(references(commands) == 2);
        size_t acquisitions = f.allocations;
        f.fail_at = acquisitions;
        assert(doe_resource_commands_publish_program(commands) == DOE_RESOURCE_OK);
        assert(references(commands) == 2 && f.allocations == acquisitions);
        assert(doe_resource_commands_publish_program(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_OK);
        assert(references(commands) == 3);
        assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        assert(references(commands) == 2);
        if (active) assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_OK);
        assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_OK);
        assert(references(commands) == (active ? 2u : 1u));
        assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_publish_program(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_STATE);
        if (active) {
            assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
            assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_INVALID_ARGUMENT) == DOE_RESOURCE_INVALID_ARGUMENT);
            assert(references(commands) == 2);
            assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
        }
        assert(references(commands) == 1 && f.allocations == acquisitions);
        assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_STATE);
        doe_resource_commands_release(commands);
        doe_resource_buffer_release(source);
        doe_resource_buffer_release(destination);
        assert(a.releases == 1 && b.releases == 1);
        empty(&f);
    }
}

static void abort_without_caller_or_device(void)
{
    for (unsigned caller_dropped = 0; caller_dropped < 2; ++caller_dropped) {
        fixture f = fresh();
        native_token a = {0}, b = {0};
        doe_resource_buffer *source = buffer(&f, &a, 64, WGPUBufferUsage_CopySrc);
        doe_resource_buffer *destination = buffer(&f, &b, 64, WGPUBufferUsage_CopyDst);
        doe_resource_commands *commands = copy_commands(&f, source, destination);
        assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_OK);
        if (caller_dropped) doe_resource_commands_release(commands);
        f.alive = false;
        assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_OK);
        if (!caller_dropped) {
            assert(references(commands) == 1);
            assert(doe_resource_commands_reserve_program(commands) == DOE_RESOURCE_UNAVAILABLE);
            f.alive = true;
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
        }
        doe_resource_buffer_release(source);
        doe_resource_buffer_release(destination);
        assert(a.releases == 1 && b.releases == 1);
        empty(&f);
    }
}

typedef struct attempt {
    doe_resource_commands *commands;
    atomic_uint *ready;
    atomic_bool *start;
    bool ordinary;
    doe_resource_status result;
} attempt;

static void *compete(void *userdata)
{
    attempt *value = userdata;
    atomic_fetch_add_explicit(value->ready, 1, memory_order_release);
    while (!atomic_load_explicit(value->start, memory_order_acquire)) sched_yield();
    value->result = value->ordinary ? doe_resource_commands_begin_submit(value->commands) :
        doe_resource_commands_reserve_program(value->commands);
    doe_resource_commands_release(value->commands);
    return NULL;
}

static void competing_claims(void)
{
    /* Only immutable dependencies are read concurrently; no device/resource
     * mutation or fixture allocation/finalization happens on worker threads. */
    for (unsigned ordinary = 0; ordinary < 2; ++ordinary) {
        fixture f = fresh();
        native_token a = {0}, b = {0};
        doe_resource_buffer *source = buffer(&f, &a, 64, WGPUBufferUsage_CopySrc);
        doe_resource_buffer *destination = buffer(&f, &b, 64, WGPUBufferUsage_CopyDst);
        doe_resource_commands *commands = copy_commands(&f, source, destination);
        atomic_uint ready;
        atomic_bool start;
        atomic_init(&ready, 0);
        atomic_init(&start, false);
        attempt values[2] = {{commands, &ready, &start, false, DOE_RESOURCE_PENDING},
            {commands, &ready, &start, ordinary != 0, DOE_RESOURCE_PENDING}};
        pthread_t threads[2];
        for (size_t i = 0; i < 2; ++i) {
            doe_resource_commands_retain(commands);
            assert(pthread_create(&threads[i], NULL, compete, &values[i]) == 0);
        }
        while (atomic_load_explicit(&ready, memory_order_acquire) != 2) sched_yield();
        atomic_store_explicit(&start, true, memory_order_release);
        for (size_t i = 0; i < 2; ++i) assert(pthread_join(threads[i], NULL) == 0);
        assert((values[0].result == DOE_RESOURCE_OK && values[1].result == DOE_RESOURCE_STATE) ||
            (values[1].result == DOE_RESOURCE_OK && values[0].result == DOE_RESOURCE_STATE));
        assert(references(commands) == 2);
        if (values[1].ordinary && values[1].result == DOE_RESOURCE_OK)
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        else assert(doe_resource_commands_abort_program(commands) == DOE_RESOURCE_OK);
        assert(references(commands) == 1);
        doe_resource_commands_release(commands);
        doe_resource_buffer_release(source);
        doe_resource_buffer_release(destination);
        empty(&f);
    }
}

int main(void)
{
    assert(doe_resource_commands_reserve_program(NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_commands_publish_program(NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_commands_abort_program(NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
    abort_then_submit();
    publish_then_release();
    abort_without_caller_or_device();
    competing_claims();
    puts("Program reservation: failure rollback, permanent publication, replay/release states, exact leases and competing claims PASS");
    return 0;
}
