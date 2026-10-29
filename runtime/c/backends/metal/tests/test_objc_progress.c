/* SPDX-License-Identifier: MIT
 * Exact typed call controls, not an SDK or physical Metal test. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct wait_fixture {
    doe_mtl_objc runtime;
    int command, event, status_selector, wait_selector, responds_selector;
    size_t status, after_wait;
    unsigned waits;
    uint64_t value, milliseconds;
    bool supported, signaled;
} wait_fixture;

static size_t command_status(void *receiver, void *selector)
{
    wait_fixture *fixture = (wait_fixture *)((char *)receiver - offsetof(wait_fixture,command));
    assert(receiver == &fixture->command && selector == &fixture->status_selector);
    return fixture->status;
}

static doe_objc_bool responds(void *receiver, void *selector, void *method)
{
    wait_fixture *fixture = (wait_fixture *)((char *)receiver - offsetof(wait_fixture,event));
    assert(receiver == &fixture->event && selector == &fixture->responds_selector);
    assert(method == &fixture->wait_selector);
    return (doe_objc_bool)fixture->supported;
}

static doe_objc_bool wait_event(void *receiver, void *selector, uint64_t value, uint64_t milliseconds)
{
    wait_fixture *fixture = (wait_fixture *)((char *)receiver - offsetof(wait_fixture,event));
    assert(receiver == &fixture->event && selector == &fixture->wait_selector);
    ++fixture->waits;
    fixture->value = value;
    fixture->milliseconds = milliseconds;
    fixture->status = fixture->after_wait;
    return (doe_objc_bool)fixture->signaled;
}

static doe_mtl_status block(wait_fixture *fixture, uint64_t timeout)
{
    return doe_mtl_objc_command_block(&fixture->runtime,&fixture->command,
        &fixture->event,UINT64_C(0x123456789abcdef0),timeout);
}

int main(void)
{
    wait_fixture fixture = {.status=2,.after_wait=2,.supported=true};
    fixture.runtime.status = &fixture.status_selector;
    fixture.runtime.size_call = command_status;
    fixture.runtime.responds = &fixture.responds_selector;
    fixture.runtime.responds_call = responds;
    fixture.runtime.queries.wait_until_value = &fixture.wait_selector;
    fixture.runtime.queries.event_wait = wait_event;
    assert(block(&fixture,0) == DOE_MTL_PENDING && !fixture.waits);
    assert(block(&fixture,999999) == DOE_MTL_PENDING && !fixture.waits);
    assert(block(&fixture,1999999) == DOE_MTL_PENDING && fixture.waits == 1);
    assert(fixture.milliseconds == 1 && fixture.value == UINT64_C(0x123456789abcdef0));
    fixture.signaled = true;
    assert(block(&fixture,UINT64_MAX) == DOE_MTL_OK);
    assert(fixture.milliseconds == UINT64_MAX / UINT64_C(1000000));
    /* A wake is not a completion verdict; pending status survives the signal. */
    assert(fixture.status == 2);
    fixture.signaled = false;
    fixture.after_wait = 5;
    assert(block(&fixture,1000000) == DOE_MTL_OK && fixture.status == 5);
    unsigned waits = fixture.waits;
    assert(block(&fixture,1000000) == DOE_MTL_OK && fixture.waits == waits);
    fixture.status = 4;
    assert(block(&fixture,1000000) == DOE_MTL_OK && fixture.waits == waits);
    fixture.status = 0;
    assert(block(&fixture,1000000) == DOE_MTL_COMPLETION_UNKNOWN && fixture.waits == waits);
    fixture.status = 3;
    fixture.after_wait = 0;
    assert(block(&fixture,1000000) == DOE_MTL_COMPLETION_UNKNOWN);
    fixture.status = 2;
    fixture.supported = false;
    waits = fixture.waits;
    assert(block(&fixture,1000000) == DOE_MTL_UNSUPPORTED && fixture.waits == waits);
    fixture.supported = true;
    assert(doe_mtl_objc_command_block(&fixture.runtime,&fixture.command,NULL,1,1000000) == DOE_MTL_UNSUPPORTED);
    assert(doe_mtl_objc_command_block(&fixture.runtime,&fixture.command,&fixture.event,0,1000000) == DOE_MTL_UNSUPPORTED);
    fixture.runtime.queries.event_wait = NULL;
    assert(block(&fixture,1000000) == DOE_MTL_UNSUPPORTED && fixture.waits == waits);
    puts("PASS bounded Metal event wait: exact handles/u64/BOOL, budgets, timeout, signal, terminal failure, unknown and unavailable selector");
    return 0;
}
