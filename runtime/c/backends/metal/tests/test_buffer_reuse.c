/* SPDX-License-Identifier: MIT
 * Queue/lease call ledger only; mock commands do not execute GPU work. */
#include "mock_native.h"
#include "../src/internal.h"
#include "../src/progress.h"
#include <assert.h>
#include <stdio.h>

static doe_mtl_context *open_context(mock_native *mock) {
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(mock), &context, NULL) == DOE_MTL_OK);
    return context;
}

static doe_mtl_buffer *buffer_create(doe_mtl_context *context, doe_mtl_memory memory) {
    doe_mtl_buffer *buffer = NULL;
    assert(doe_mtl_buffer_create(context, 64, memory, &buffer, NULL) == DOE_MTL_OK);
    return buffer;
}

static doe_mtl_commands *commands_create(doe_mtl_context *context) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}

static void submitted_reuse(void) {
    mock_native mock;
    doe_mtl_context *context = open_context(&mock);
    doe_mtl_buffer *source = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *output = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *a = commands_create(context), *b = commands_create(context);
    unsigned char byte = 0xa5;
    assert(doe_mtl_commands_fill(a, source, 0, 64, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(b, source, 0, output, 0, 64, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_conflict(b) == a && !mock.copies);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    b = commands_create(context);
    assert(doe_mtl_commands_copy(b, source, 0, output, 0, 64, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    assert(mock.commits == 2 && mock.polls == 1 && mock.waits == 0);
    assert(doe_mtl_buffer_owner(source) == b);
    assert(doe_mtl_buffer_write(source, 0, &byte, 1) == DOE_MTL_BUSY);
    assert(doe_mtl_buffer_read(source, 0, &byte, 1) == DOE_MTL_BUSY && byte == 0xa5);
    mock_command_result(a->native, 4, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_owner(source) == b); /* Removing older use keeps newest. */
    assert(doe_mtl_buffer_write(source, 0, &byte, 1) == DOE_MTL_BUSY);
    mock_command_result(b->native, 0, 0);
    assert(doe_mtl_commands_poll(b, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    doe_mtl_commands *c = commands_create(context);
    assert(doe_mtl_commands_fill(c, source, 0, 64, 1, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_conflict(c) == b);
    assert(doe_mtl_commands_release(c) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_BUSY);
    mock_command_result(b->native, 4, 0);
    assert(doe_mtl_commands_poll(b, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_owner(source) == NULL);
    assert(doe_mtl_buffer_write(source, 0, &byte, 1) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    doe_mtl_buffer_release(source); doe_mtl_buffer_release(output);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void dependent_publication(bool fail) {
    mock_native mock;
    doe_mtl_context *context = open_context(&mock);
    doe_mtl_buffer *source = buffer_create(context, DOE_MTL_PRIVATE);
    doe_mtl_buffer *middle = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *output = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *a = commands_create(context), *b = commands_create(context);
    doe_mtl_commands *c = commands_create(context);
    doe_mtl_error error = {0};
    assert(doe_mtl_commands_fill(a, source, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(!source->initialized);
    /* GPU consumers inherit the committed fill; no host initialization published. */
    assert(doe_mtl_commands_copy(b, source, 0, middle, 0, 64, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(c, middle, 0, output, 0, 64, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(c, NULL) == DOE_MTL_OK);
    mock_command_result(b->native, 4, 0);
    mock_command_result(c->native, 4, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_PENDING);
    assert(a->state == DOE_MTL_SUBMITTED && b->state == DOE_MTL_SUBMITTED);
    unsigned char byte = 0xa5;
    assert(doe_mtl_buffer_read(output, 0, &byte, 1) == DOE_MTL_BUSY && byte == 0xa5);
    mock_command_result(a->native, 0, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(c) == DOE_MTL_BUSY && !source->initialized);
    mock_command_result(a->native, fail ? 5u : 4u, fail ? -8123 : 0);
    mock_command_result(c->native, 0, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(a->state == (fail ? DOE_MTL_FAILED : DOE_MTL_COMPLETE));
    assert(b->state == (fail ? DOE_MTL_FAILED : DOE_MTL_COMPLETE));
    assert(doe_mtl_buffer_owner(middle) == c && doe_mtl_buffer_owner(output) == c);
    assert(doe_mtl_buffer_read(output, 0, &byte, 1) == DOE_MTL_BUSY);
    mock_command_result(c->native, fail ? 5u : 4u, fail ? -77 : 0);
    assert(doe_mtl_commands_poll(c, &error) == (fail ? DOE_MTL_EXECUTION_FAILED : DOE_MTL_OK));
    if (fail) {
        assert(error.native_code == -8123);
        assert(strcmp(error.operation, "MTLCommandBuffer.error") == 0);
        assert(doe_mtl_buffer_read(output, 0, &byte, 1) == DOE_MTL_EXECUTION_FAILED);
        assert(byte == 0xa5 && source->poisoned && middle->poisoned && output->poisoned);
    } else {
        assert(source->initialized && !output->poisoned);
        assert(doe_mtl_buffer_read(output, 0, &byte, 1) == DOE_MTL_OK);
    }
    assert(doe_mtl_commands_release(c) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    doe_mtl_buffer_release(source); doe_mtl_buffer_release(middle); doe_mtl_buffer_release(output);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void mixed_predecessors(void) {
    mock_native mock;
    doe_mtl_context *context = open_context(&mock);
    doe_mtl_buffer *x = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *y = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *z = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *unrelated = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *a = commands_create(context), *b = commands_create(context);
    doe_mtl_commands *c = commands_create(context), *d = commands_create(context);
    doe_mtl_commands *other = commands_create(context);
    assert(doe_mtl_commands_fill(a, x, 0, 64, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(b, y, 0, 64, 2, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(c, x, 0, z, 0, 64, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(c, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(d, z, 0, y, 0, 64, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(d, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(other, unrelated, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(other, NULL) == DOE_MTL_OK);
    mock_command_result(a->native, 4, 0); mock_command_result(b->native, 4, 0);
    mock_command_result(d->native, 4, 0);
    assert(doe_mtl_commands_poll(d, NULL) == DOE_MTL_PENDING);
    assert(d->state == DOE_MTL_SUBMITTED && other->state == DOE_MTL_SUBMITTED);
    mock_command_result(c->native, 4, 0);
    assert(doe_mtl_commands_poll(d, NULL) == DOE_MTL_OK);
    assert(a->state == DOE_MTL_COMPLETE && b->state == DOE_MTL_COMPLETE);
    assert(c->state == DOE_MTL_COMPLETE && other->state == DOE_MTL_SUBMITTED);
    assert(doe_mtl_buffer_owner(unrelated) == other && !mock.waits);
    mock_command_result(other->native, 4, 0);
    assert(doe_mtl_commands_poll(other, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(c) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(d) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(other) == DOE_MTL_OK);
    doe_mtl_buffer_release(x); doe_mtl_buffer_release(y); doe_mtl_buffer_release(z);
    doe_mtl_buffer_release(unrelated); doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void failed_before_submit_and_eviction(void) {
    mock_native mock;
    doe_mtl_context *context = open_context(&mock);
    doe_mtl_buffer *source = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *output = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *a = commands_create(context), *b = commands_create(context);
    assert(doe_mtl_commands_fill(a, source, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(b, source, 0, output, 0, 64, NULL) == DOE_MTL_OK);
    mock_command_result(a->native, 5, -121);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_EXECUTION_FAILED);
    doe_mtl_error error = {0};
    assert(doe_mtl_commands_fill(b, output, 0, 64, 1, &error) == DOE_MTL_EXECUTION_FAILED);
    assert(error.native_code == -121);
    assert(doe_mtl_commands_submit(b, &error) == DOE_MTL_EXECUTION_FAILED);
    assert(error.native_code == -121 && mock.commits == 1);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK && !output->poisoned);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    doe_mtl_buffer_release(source);
    a = commands_create(context); b = commands_create(context);
    assert(doe_mtl_commands_fill(a, output, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(b, output, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    void *native = doe_mtl_buffer_native(output);
    assert(doe_mtl_buffer_evict(output) == DOE_MTL_OK);
    mock_command_result(a->native, 0, 0); mock_command_result(b->native, 4, 0);
    assert(doe_mtl_commands_poll(b, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_buffer_native(output) == native);
    mock_command_result(a->native, 4, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_native(output) == native && doe_mtl_buffer_owner(output) == b);
    assert(doe_mtl_commands_poll(b, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_native(output) == NULL && !output->owner);
    assert(doe_mtl_buffer_evict(output) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    doe_mtl_buffer_release(output); doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void unknown_commit_never_lends_order(void) {
    mock_native mock;
    doe_mtl_context *context = open_context(&mock);
    doe_mtl_buffer *buffer = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *a = commands_create(context), *b = commands_create(context);
    assert(doe_mtl_commands_fill(a, buffer, 0, 64, 0, NULL) == DOE_MTL_OK);
    mock.unknown_commit = true;
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    /* No caller poll is required to reject an unproved void commit result. */
    assert(doe_mtl_commands_fill(b, buffer, 0, 64, 0, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_conflict(b) == a && mock.waits == 0);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_BUSY);
    mock_command_result(a->native, 2, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_PENDING);
    b = commands_create(context);
    assert(doe_mtl_commands_fill(b, buffer, 0, 64, 0, NULL) == DOE_MTL_OK);
    mock_command_result(a->native, 0, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_BUSY && mock.commits == 1);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK && buffer->owner == a);
    mock_command_result(a->native, 4, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    doe_mtl_buffer_release(buffer); doe_mtl_context_release(context); mock_assert_closed(&mock);
}

int main(void) {
    submitted_reuse(); dependent_publication(false); dependent_publication(true);
    mixed_predecessors(); failed_before_submit_and_eviction(); unknown_commit_never_lends_order();
    puts("PASS Metal submitted buffer reuse: no admission waits, committed initialization, dependent failure/unknown retention, host exclusion, predecessor retirement and final-use eviction; host call ledger only");
    return 0;
}
