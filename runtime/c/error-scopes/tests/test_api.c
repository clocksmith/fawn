/* SPDX-License-Identifier: MIT
 * Developer checks, not the frozen independent acceptance oracle. */
#include "doe_error_scopes.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct capture {
    size_t calls;
    uint32_t type;
    size_t length;
    bool null_data;
    unsigned char bytes[DOE_ERROR_SCOPE_MESSAGE_CAPACITY];
};

static void receive(uint32_t type, WGPUStringView message, void *first, void *second)
{
    struct capture *capture = first;
    assert(second == &capture->calls);
    capture->calls += 1;
    capture->type = type;
    capture->length = message.length;
    capture->null_data = message.data == NULL;
    assert(message.length <= sizeof(capture->bytes));
    if (message.length != 0) memcpy(capture->bytes, message.data, message.length);
    errno = ERANGE;
}

static void ok(doe_error_scope_result value)
{
    assert(value.status == DOE_ERROR_SCOPE_OK);
    assert(value.system_error == 0);
}

static doe_error_scope_stack *create(void)
{
    doe_error_scope_requirements requirements;
    ok(doe_error_scope_storage_requirements(DOE_ERROR_SCOPES_ABI_VERSION, &requirements));
    void *storage = malloc(requirements.size);
    assert(storage != NULL);
    doe_error_scope_stack *stack = NULL;
    ok(doe_error_scope_init(DOE_ERROR_SCOPES_ABI_VERSION, storage, requirements.size, &stack));
    assert(stack == storage);
    return stack;
}

static void filters_and_depth(void)
{
    const uint32_t filters[] = {0, 1, 2, 3, UINT32_MAX};
    const uint32_t types[] = {0, 1, 2, 3, 4, 5, UINT32_MAX};
    /* Explicit expected rows: catch-all, validation, OOM, internal, catch-all. */
    const bool expected[][7] = {
        {true, false, true, true, true, true, true},
        {false, false, true, false, false, false, false},
        {false, false, false, true, false, false, false},
        {false, false, false, false, true, false, false},
        {true, false, true, true, true, true, true}
    };
    doe_error_scope_stack *stack = create();
    for (size_t f = 0; f < 5; ++f) {
        for (size_t t = 0; t < 7; ++t) {
            struct capture uncaptured = {0};
            struct capture popped = {0};
            ok(doe_error_scope_set_handler(stack, receive, &uncaptured, &uncaptured.calls));
            ok(doe_error_scope_push(stack, filters[f]));
            ok(doe_error_scope_deliver(stack, types[t], NULL, 0));
            ok(doe_error_scope_pop(stack, receive, &popped, &popped.calls));
            assert(popped.calls == 1);
            assert(popped.type == (expected[f][t] ? types[t] : 1));
            assert(popped.null_data == !expected[f][t]);
            assert(uncaptured.calls == (expected[f][t] ? 0u : 1u));
            if (uncaptured.calls != 0) assert(!uncaptured.null_data);
        }
    }
    ok(doe_error_scope_set_handler(stack, NULL, NULL, NULL));
    for (size_t i = 0; i < 64; ++i) ok(doe_error_scope_push(stack, 1));
    assert(doe_error_scope_push(stack, 2).status == DOE_ERROR_SCOPE_LIMIT);
    ok(doe_error_scope_deliver(stack, 2, (const unsigned char *)"first", 5));
    ok(doe_error_scope_deliver(stack, 2, (const unsigned char *)"second", 6));
    struct capture capture = {0};
    ok(doe_error_scope_pop(stack, receive, &capture, &capture.calls));
    assert(capture.type == 2 && capture.length == 5);
    assert(memcmp(capture.bytes, "first", 5) == 0);
    for (size_t i = 0; i < 63; ++i) {
        ok(doe_error_scope_pop(stack, receive, &capture, &capture.calls));
        assert(capture.type == 1 && capture.null_data);
    }
    assert(doe_error_scope_pop(stack, NULL, NULL, NULL).status == DOE_ERROR_SCOPE_EMPTY);
    ok(doe_error_scope_deinit(stack));
    free(stack);
}

static void message_and_errno(void)
{
    doe_error_scope_stack *stack = create();
    unsigned char source[1024];
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (unsigned char)i;
    const size_t lengths[] = {0, 1, 511, 512, 513, sizeof(source)};
    for (size_t i = 0; i < 6; ++i) {
        struct capture capture = {0};
        errno = EDOM;
        ok(doe_error_scope_push(stack, 0));
        assert(errno == EDOM);
        ok(doe_error_scope_deliver(stack, UINT32_MAX, source, lengths[i]));
        assert(errno == EDOM);
        ok(doe_error_scope_pop(stack, receive, &capture, &capture.calls));
        assert(errno == ERANGE);
        const size_t expected = lengths[i] < 512 ? lengths[i] : 512;
        assert(capture.length == expected && !capture.null_data);
        assert(memcmp(capture.bytes, source, expected) == 0);
    }
    errno = EDOM;
    assert(doe_error_scope_pop(stack, NULL, NULL, NULL).status == DOE_ERROR_SCOPE_EMPTY);
    assert(errno == EDOM);
    ok(doe_error_scope_push(stack, 0));
    ok(doe_error_scope_pop(stack, NULL, NULL, NULL));
    assert(errno == EDOM);
    ok(doe_error_scope_deinit(stack));
    assert(doe_error_scope_push(stack, 0).status == DOE_ERROR_SCOPE_INVALID_STATE);
    assert(doe_error_scope_deliver(stack, 0, NULL, 1).status == DOE_ERROR_SCOPE_INVALID_ARGUMENT);
    assert(errno == EDOM);
    free(stack);
}

static void reenter(uint32_t type, WGPUStringView message, void *first, void *second)
{
    doe_error_scope_stack *stack = first;
    assert(type == 2 && message.length == 5);
    ok(doe_error_scope_push(stack, 3));
    ok(doe_error_scope_deliver(stack, 4, (const unsigned char *)"inner", 5));
    struct capture capture = {0};
    ok(doe_error_scope_pop(stack, receive, &capture, &capture.calls));
    assert(capture.type == 4);
    assert(memcmp(message.data, "outer", 5) == 0);
    assert(second == stack);
    ok(doe_error_scope_deinit(stack));
    free(stack);
    errno = ENOTTY;
}

int main(void)
{
    filters_and_depth();
    message_and_errno();
    doe_error_scope_stack *stack = create();
    ok(doe_error_scope_push(stack, 1));
    ok(doe_error_scope_deliver(stack, 2, (const unsigned char *)"outer", 5));
    ok(doe_error_scope_pop(stack, reenter, stack, stack));
    assert(errno == ENOTTY);
    stack = create();
    ok(doe_error_scope_set_handler(stack, reenter, stack, stack));
    ok(doe_error_scope_deliver(stack, 2, (const unsigned char *)"outer", 5));
    assert(errno == ENOTTY);
    puts("developer API: filter matrix, depth, first error, bytes, errno, reentry and self-free passed");
    return 0;
}
