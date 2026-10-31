/* SPDX-License-Identifier: MIT
 * Exact typed call controls, not an SDK or physical Metal test. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>
#if defined(__BLOCKS__)
#include <Block.h>
#endif

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

/* addCompletedHandler: receives the command, its exact selector and a block
 * that reports once to the caller's notify/data; nothing runs at registration. */
typedef struct handler_fixture {
    doe_mtl_objc runtime;
    int command, selector, pool_class, new_object, drain, pool;
    unsigned registrations, notified, pools, drains;
#if defined(__BLOCKS__)
    void (^handler)(void *);
#endif
} handler_fixture;
static handler_fixture *handlers;
static void *pool_new(void *receiver, void *selector)
{
    assert(receiver == &handlers->pool_class && selector == &handlers->new_object);
    ++handlers->pools;
    return &handlers->pool;
}
static void pool_drain(void *receiver, void *selector)
{
    assert(receiver == &handlers->pool && selector == &handlers->drain);
    ++handlers->drains;
}
#if defined(__BLOCKS__)
static void add_handler(void *receiver, void *selector, void (^handler)(void *))
{
    assert(receiver == &handlers->command && selector == &handlers->selector);
    ++handlers->registrations;
    handlers->handler = Block_copy(handler); /* As Metal does: the argument is a stack block. */
}
#endif
static void notified(void *data) { ++((handler_fixture *)data)->notified; }

static void completed_handler(void)
{
    handler_fixture fixture = {0};
    handlers = &fixture;
    fixture.runtime.pool_class = &fixture.pool_class;
    fixture.runtime.new_object = &fixture.new_object;
    fixture.runtime.drain = &fixture.drain;
    fixture.runtime.object_call = pool_new;
    fixture.runtime.void_call = pool_drain;
    fixture.runtime.add_completed_handler = &fixture.selector;
#if defined(__BLOCKS__)
    fixture.runtime.handler_message = (doe_objc_message)add_handler;
    assert(doe_mtl_objc_completed_handler(&fixture.runtime,&fixture.command,notified,&fixture,NULL) == DOE_MTL_OK);
    assert(fixture.registrations == 1 && fixture.notified == 0 && fixture.pools == 1 && fixture.drains == 1);
    fixture.handler(&fixture.command);
    assert(fixture.notified == 1);
    Block_release(fixture.handler);
#else
    assert(doe_mtl_objc_completed_handler(&fixture.runtime,&fixture.command,notified,&fixture,NULL) == DOE_MTL_UNSUPPORTED);
    assert(fixture.notified == 0);
#endif
}

int main(void)
{
    completed_handler();
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
    puts("PASS bounded Metal event wait: exact handles/u64/BOOL, budgets, timeout, signal, terminal failure, unknown and unavailable selector; completed-handler registration");
    return 0;
}
