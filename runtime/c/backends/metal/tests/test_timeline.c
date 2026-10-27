/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>
static uint64_t signaled, observed;
static unsigned event_releases, signals;
static int event;
static void (*ordinary_release)(void *, void *);
static doe_mtl_status create(void *u, void *device, void **out, doe_mtl_error *e) {
    (void)u; assert(device); *out=&event; return doe_mtl_fail(DOE_MTL_OK,"event",e);
}
static doe_mtl_status signal_event(void *u, void *command, void *handle, uint64_t value, doe_mtl_error *e) {
    (void)u;assert(command&&handle==&event&&value>signaled);signaled=value;++signals;
    return doe_mtl_fail(DOE_MTL_OK,"signal",e);
}
static uint64_t value(void *u, void *handle) {(void)u;assert(handle==&event);return observed;}
static void release(void *u, void *handle) {
    if(handle==&event)++event_releases;else ordinary_release(u,handle);
}
int main(void) {
    mock_native mock={0};doe_mtl_native native=mock_driver(&mock);
    ordinary_release=native.release;native.release=release;
    native.timeline_create=create;native.timeline_signal=signal_event;native.timeline_value=value;
    doe_mtl_context *context=NULL;doe_mtl_error error={0};
    assert(doe_mtl_context_open(native,&context,&error)==DOE_MTL_OK);
    assert(doe_mtl_context_prefer_timeline(context,&error)==DOE_MTL_OK);
    doe_mtl_caps caps={0};assert(doe_mtl_context_caps(context,&caps)==DOE_MTL_OK&&caps.timeline_enabled);
    doe_mtl_commands *commands=NULL;
    assert(doe_mtl_commands_create(context,&commands,&error)==DOE_MTL_OK);
    assert(doe_mtl_context_prefer_timeline(context,&error)==DOE_MTL_INVALID_STATE);
    assert(doe_mtl_commands_submit(commands,&error)==DOE_MTL_OK&&signaled==1&&mock.commits==1);
    observed=1;
    assert(doe_mtl_commands_poll(commands,&error)==DOE_MTL_PENDING); /* Event alone cannot hide later NSError. */
    mock_result(&mock,5,-77);
    assert(doe_mtl_commands_poll(commands,&error)==DOE_MTL_EXECUTION_FAILED&&error.native_code==-77);
    assert(doe_mtl_commands_wait(commands,&error)==DOE_MTL_EXECUTION_FAILED&&error.native_code==-77);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_OK);
    assert(doe_mtl_commands_create(context,&commands,&error)==DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands,&error)==DOE_MTL_OK&&signaled==2);
    mock_result(&mock,4,0);
    assert(doe_mtl_commands_poll(commands,&error)==DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_BUSY);
    observed=2;assert(doe_mtl_commands_poll(commands,&error)==DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_OK);
    context->timeline_next=UINT64_MAX;
    assert(doe_mtl_commands_create(context,&commands,&error)==DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands,&error)==DOE_MTL_RANGE&&signals==2&&mock.commits==2);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_OK);
    doe_mtl_context_release(context);assert(event_releases==1);mock_assert_closed(&mock);
    puts("PASS real event-call contract: monotonic signals, terminal-error authority, missing-event proof, overflow precommit and retained event lifetime");
}
