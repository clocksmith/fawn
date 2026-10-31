/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include "../src/progress.h"
#include "../src/internal.h"
#include "../src/texture_internal.h"
#include "../src/query_internal.h"
#include "../include/doe_metal_indirect.h"
#include <assert.h>
#include <stdio.h>

static void rollback_groups(void)
{
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock),&context,NULL) == DOE_MTL_OK);
    doe_mtl_buffer *held = NULL, *arguments = NULL;
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&held,NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&arguments,NULL) == DOE_MTL_OK);
    doe_mtl_commands *first = NULL;
    assert(doe_mtl_commands_create(context,&first,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(first,held,0,64,1,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(first,NULL) == DOE_MTL_OK);
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_desc td = {DOE_MTL_TEXTURE_2D,DOE_MTL_RGBA8_UNORM,4,4,1,1,
        DOE_MTL_TEXTURE_RENDER_TARGET,false,1};
    assert(doe_mtl_texture_create(context,&td,&texture,NULL) == DOE_MTL_OK);
    doe_mtl_texture_view *view = NULL;
    doe_mtl_texture_view_desc vd = {DOE_MTL_TEXTURE_2D,DOE_MTL_RGBA8_UNORM,0,1,0,1,
        DOE_MTL_SWIZZLE_RED,DOE_MTL_SWIZZLE_GREEN,DOE_MTL_SWIZZLE_BLUE,DOE_MTL_SWIZZLE_ALPHA};
    assert(doe_mtl_texture_view_create(texture,&vd,&view,NULL) == DOE_MTL_OK);
    doe_mtl_timestamps *timestamps = NULL;
    assert(doe_mtl_timestamps_create(context,2,&timestamps,NULL) == DOE_MTL_OK);
    doe_mtl_pipeline_desc pd = {.msl="call ledger",.msl_length=11,.entrypoint="consumer",
        .workgroup={1,1,1},.sizes_slot=30};
    doe_mtl_pipeline *pipeline = NULL;
    assert(doe_mtl_pipeline_create(context,&pd,&pipeline,NULL) == DOE_MTL_OK);
    doe_mtl_dispatch_guard *guard = NULL;
    doe_mtl_dispatch_guard_desc gd = {.pipeline=pd,.binding_slot=3,.max_groups={65535,65535,65535}};
    assert(doe_mtl_dispatch_guard_create(context,&gd,&guard,NULL) == DOE_MTL_OK);
    const unsigned baseline = mock.live;
    for (unsigned empty = 0; empty != 2; ++empty) {
        doe_mtl_commands *provisional = NULL;
        assert(doe_mtl_commands_create(context,&provisional,NULL) == DOE_MTL_OK);
        assert(doe_mtl_commands_initialize_attachment(provisional,view,NULL) == DOE_MTL_OK);
        assert(doe_mtl_commands_timestamp(provisional,timestamps,0,NULL) == DOE_MTL_OK);
        doe_mtl_indirect_dispatch indirect = {.guard=guard,.pipeline=pipeline,.arguments=arguments};
        assert(doe_mtl_commands_dispatch_indirect(provisional,&indirect,NULL) == DOE_MTL_OK);
        assert(texture->owner == provisional && timestamps->owner == provisional && arguments->owner == provisional);
        assert(doe_mtl_commands_copy(provisional,held,0,held,0,empty ? 0 : 4,NULL) == DOE_MTL_BUSY);
        assert(doe_mtl_commands_conflict(provisional) == first);
        assert(doe_mtl_commands_submit(provisional,NULL) == DOE_MTL_BUSY && mock.commits == 1);
        assert(doe_mtl_commands_release(provisional) == DOE_MTL_OK);
        assert(!texture->initialized[0] && !texture->owner && !timestamps->available[0] && !timestamps->owner);
        assert(!arguments->owner && held->owner == first && mock.live == baseline);
    }
    mock_command_result(first->native,4,0);
    assert(doe_mtl_commands_poll(first,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(first) == DOE_MTL_OK);
    doe_mtl_dispatch_guard_release(guard); doe_mtl_pipeline_release(pipeline);
    doe_mtl_timestamps_release(timestamps);
    doe_mtl_texture_view_release(view); doe_mtl_texture_release(texture);
    doe_mtl_buffer_release(arguments); doe_mtl_buffer_release(held);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

typedef struct completion_ledger { unsigned acquired, notified; } completion_ledger;
static void ledger_acquire(void *data) { ++((completion_ledger *)data)->acquired; }
static void ledger_notify(void *data)
{
    completion_ledger *ledger = data;
    assert(ledger->notified < ledger->acquired);
    ++ledger->notified;
}

/* Every committed command registers exactly one completed handler, after its
 * last fallible encode; Metal reports success and failure through it, and a
 * failed registration reports synchronously without committing. */
static void completion_handlers(void)
{
    mock_native mock;
    completion_ledger ledger = {0};
    const doe_mtl_completion_sink sink = {&ledger, ledger_acquire, ledger_notify};
    doe_mtl_native bare = mock_driver(&mock);
    bare.completed_handler = NULL;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(bare,&context,NULL) == DOE_MTL_OK);
    assert(doe_mtl_context_set_completion(context,sink) == DOE_MTL_UNSUPPORTED);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);

    assert(doe_mtl_context_open(mock_driver(&mock),&context,NULL) == DOE_MTL_OK);
    assert(doe_mtl_context_set_completion(context,sink) == DOE_MTL_OK);
    assert(doe_mtl_context_set_completion(context,sink) == DOE_MTL_INVALID_STATE);
    doe_mtl_buffer *buffer = NULL;
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&buffer,NULL) == DOE_MTL_OK);
    doe_mtl_commands *ok = NULL, *failed = NULL, *refused = NULL;
    assert(doe_mtl_commands_create(context,&ok,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(ok,buffer,0,64,1,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(ok,NULL) == DOE_MTL_OK);
    assert(mock.handlers == 1 && ledger.acquired == 1 && ledger.notified == 0);
    assert(doe_mtl_commands_poll(ok,NULL) == DOE_MTL_PENDING && ledger.notified == 0);
    mock_command_result(ok->native,4,0);
    assert(ledger.notified == 1);
    assert(doe_mtl_commands_poll(ok,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(ok) == DOE_MTL_OK);

    assert(doe_mtl_commands_create(context,&failed,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(failed,buffer,0,64,2,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(failed,NULL) == DOE_MTL_OK);
    mock_command_result(failed->native,5,-31);
    assert(ledger.acquired == 2 && ledger.notified == 2);
    assert(doe_mtl_commands_poll(failed,NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(doe_mtl_commands_release(failed) == DOE_MTL_OK);

    doe_mtl_buffer *spare = NULL; /* buffer is poisoned by the failed command. */
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_SHARED,&spare,NULL) == DOE_MTL_OK);
    mock.fail = MOCK_HANDLER;
    assert(doe_mtl_commands_create(context,&refused,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(refused,spare,0,64,3,NULL) == DOE_MTL_OK);
    doe_mtl_error error = {0};
    assert(doe_mtl_commands_submit(refused,&error) == DOE_MTL_NATIVE_FAILURE && error.native_code == -71);
    assert(mock.commits == 2 && ledger.acquired == 3 && ledger.notified == 3);
    assert(!doe_mtl_commands_in_flight(refused) && doe_mtl_commands_release(refused) == DOE_MTL_OK);
    mock.fail = MOCK_NONE;
    assert(!spare->owner && spare->initialized);
    doe_mtl_buffer_release(spare);
    doe_mtl_buffer_release(buffer);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

int main(void)
{
    rollback_groups();
    completion_handlers();
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock),&context,NULL) == DOE_MTL_OK);
    doe_mtl_buffer *a = NULL, *b = NULL, *scratch = NULL;
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_PRIVATE,&a,NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_PRIVATE,&b,NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,64,DOE_MTL_PRIVATE,&scratch,NULL) == DOE_MTL_OK);
    doe_mtl_commands *first = NULL, *second = NULL, *provisional = NULL;
    assert(doe_mtl_commands_create(context,&first,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(first,a,0,64,1,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(first,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_create(context,&second,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(second,b,0,64,2,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(second,NULL) == DOE_MTL_OK);
    assert(mock.commits == 2 && mock.waits == 0);

    assert(doe_mtl_commands_create(context,&provisional,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(provisional,scratch,0,64,3,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(provisional,a,0,scratch,0,64,NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_conflict(provisional) == first);
    assert(doe_mtl_commands_release(provisional) == DOE_MTL_OK);
    assert(!scratch->initialized && !scratch->owner && a->owner == first);
    assert(mock.commits == 2); /* No partial recording dispatched. */

    doe_mtl_wait_lease lease = {0};
    assert(doe_mtl_commands_hold(first,&lease) == DOE_MTL_OK);
    assert(doe_mtl_commands_block(&lease,0) == DOE_MTL_PENDING);
    assert(!a->initialized && a->owner == first && mock.waits == 0);
    mock_command_result(first->native,0,0);
    assert(doe_mtl_commands_block(&lease,0) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_poll(first,NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(first) == DOE_MTL_BUSY);
    assert(a->owner == first && !a->initialized);
    doe_mtl_buffer_release(a); /* Pending command keeps actual buffer alive. */
    doe_mtl_context_release(context);
    assert(mock.closes == 0);
    mock_command_result(first->native,4,0);
    assert(doe_mtl_commands_poll(first,NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(first) == DOE_MTL_OK);
    /* Foreground retirement released shell/resources; the actual native handle
     * and runtime remain valid for an already unlocked waiter. */
    assert(doe_mtl_commands_block(&lease,0) == DOE_MTL_OK);
    assert(mock.releases_by_kind[MOCK_KIND_BUFFER] == 1);
    doe_mtl_commands_unhold(&lease);

    mock_command_result(second->native,5,-919);
    doe_mtl_error error = {0};
    assert(doe_mtl_commands_poll(second,&error) == DOE_MTL_EXECUTION_FAILED);
    assert(error.native_code == -919 && b->poisoned && !b->owner);
    assert(doe_mtl_commands_release(second) == DOE_MTL_OK);
    doe_mtl_buffer_release(b);
    doe_mtl_buffer_release(scratch);
    mock_assert_closed(&mock);
    puts("PASS Metal native wait leases, disjoint acceptance, BUSY rollback, unknown/failure/early drops (CPU call ledger)");
    return 0;
}
