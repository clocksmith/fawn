/* SPDX-License-Identifier: MIT
 * Exact typed method-call controls, not an Objective-C runtime or Apple SDK test. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct call_fixture call_fixture;
typedef struct call_object {
    call_fixture *fixture;
    unsigned references;
} call_object;

typedef enum selector_index {
    SELECT_NEW, SELECT_ALLOC, SELECT_RETAIN, SELECT_RELEASE, SELECT_DRAIN,
    SELECT_UTF8, SELECT_STRING_INIT, SELECT_NEW_LIBRARY, SELECT_NEW_FUNCTION,
    SELECT_NEW_PIPELINE, SELECT_PIPELINE_MAX, SELECT_DESCRIPTION, SELECT_CODE,
    SELECT_NEW_COMMAND, SELECT_BLIT_ENCODER, SELECT_COMPUTE_ENCODER,
    SELECT_COPY, SELECT_FILL, SELECT_END, SELECT_PIPELINE, SELECT_BUFFER,
    SELECT_BYTES, SELECT_DISPATCH, SELECT_COMMIT, SELECT_WAIT, SELECT_STATUS,
    SELECT_ERROR, SELECT_COUNT
} selector_index;
struct call_fixture {
    doe_mtl_objc runtime;
    unsigned selector[SELECT_COUNT];
    call_object pool_class, string_class, pool, device, queue, command;
    call_object source, name, library, function, pipeline, encoder, source_buffer,
        destination_buffer, error, description;
    unsigned pools, drains, strings, releases, retain_calls, ends, pipeline_sets;
    unsigned buffer_sets, bytes_sets, dispatches, copies, fills, commits, waits;
    size_t native_status, wait_status;
    int64_t error_code;
    bool fail_pool, fail_encoder, fail_library, fail_function, fail_pipeline;
    doe_mtl_native_binding bound[DOE_MTL_BUFFER_SLOTS];
    uint32_t sizes[DOE_MTL_BUFFER_SLOTS];
    size_t bytes_length, bytes_slot;
    doe_mtl_size grid, group;
    size_t source_offset, destination_offset, length;
    uint8_t value;
};

static call_fixture *owner(void *object) {
    assert(object != NULL);
    return ((call_object *)object)->fixture;
}

static void *object_call(void *receiver, void *selector) {
    call_fixture *fixture = owner(receiver);
    doe_mtl_objc *runtime = &fixture->runtime;
    if (selector == runtime->new_object) {
        assert(receiver == &fixture->pool_class);
        if (fixture->fail_pool) return NULL;
        fixture->pools++;
        return &fixture->pool;
    }
    if (selector == runtime->alloc) {
        call_object *result;
        assert(receiver == &fixture->string_class);
        result = fixture->strings++ == 0 ? &fixture->source : &fixture->name;
        result->references = 1;
        return result;
    }
    if (selector == runtime->retain) {
        ((call_object *)receiver)->references++;
        fixture->retain_calls++;
        return receiver;
    }
    if (selector == runtime->new_command) {
        assert(receiver == &fixture->queue);
        return &fixture->command;
    }
    if (selector == runtime->compute_encoder || selector == runtime->blit_encoder) {
        assert(receiver == &fixture->command);
        return fixture->fail_encoder ? NULL : &fixture->encoder;
    }
    if (selector == runtime->localized_description) {
        assert(receiver == &fixture->error);
        return &fixture->description;
    }
    assert(selector == runtime->error && receiver == &fixture->command);
    return &fixture->error;
}

static void void_call(void *receiver, void *selector) {
    call_fixture *fixture = owner(receiver);
    doe_mtl_objc *runtime = &fixture->runtime;
    if (selector == runtime->drain) {
        assert(receiver == &fixture->pool);
        fixture->drains++;
    } else if (selector == runtime->release) {
        call_object *object = receiver;
        assert(object->references > 0);
        object->references--;
        fixture->releases++;
    } else if (selector == runtime->end_encoding) {
        assert(receiver == &fixture->encoder);
        fixture->ends++;
    } else if (selector == runtime->commit) {
        assert(receiver == &fixture->command);
        fixture->commits++;
        fixture->native_status = 2;
    } else {
        assert(selector == runtime->wait && receiver == &fixture->command);
        fixture->waits++;
        fixture->native_status = fixture->wait_status;
    }
}

static size_t size_call(void *receiver, void *selector) {
    call_fixture *fixture = owner(receiver);
    if (selector == fixture->runtime.pipeline_max_threads) {
        assert(receiver == &fixture->pipeline);
        return 256;
    }
    assert(selector == fixture->runtime.status && receiver == &fixture->command);
    return fixture->native_status;
}

static intptr_t integer_call(void *receiver, void *selector) {
    call_fixture *fixture = owner(receiver);
    assert(selector == fixture->runtime.code && receiver == &fixture->error);
    return (intptr_t)fixture->error_code;
}

static const char *string_call(void *receiver, void *selector) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->description && selector == fixture->runtime.utf8);
    return "diagnostic preserved before draining autorelease pool";
}

static void *string_init_call(void *receiver, void *selector,
    const void *bytes, size_t length, size_t encoding) {
    call_fixture *fixture = owner(receiver);
    assert(selector == fixture->runtime.string_init && encoding == 4);
    if (receiver == &fixture->source) assert(length == 5 && memcmp(bytes, "a\0bcd", 5) == 0);
    else assert(receiver == &fixture->name && length == 6 && memcmp(bytes, "kernel", 6) == 0);
    return receiver;
}

static void *library_call(void *receiver, void *selector,
    void *source, void *options, void **error) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->device && selector == fixture->runtime.new_library);
    assert(source == &fixture->source && options == NULL && error != NULL);
    if (fixture->fail_library) {
        *error = &fixture->error;
        return NULL;
    }
    fixture->library.references = 1;
    return &fixture->library;
}

static void *named_call(void *receiver, void *selector, void *name) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->library && selector == fixture->runtime.new_function);
    assert(name == &fixture->name);
    if (fixture->fail_function) return NULL;
    fixture->function.references = 1;
    return &fixture->function;
}

static void *pipeline_call(void *receiver, void *selector, void *function, void **error) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->device && selector == fixture->runtime.new_pipeline);
    assert(function == &fixture->function && error != NULL);
    if (fixture->fail_pipeline) {
        *error = &fixture->error;
        return NULL;
    }
    fixture->pipeline.references = 1;
    return &fixture->pipeline;
}

static void set_object_call(void *receiver, void *selector, void *pipeline) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->encoder && selector == fixture->runtime.set_pipeline);
    assert(pipeline == &fixture->pipeline);
    fixture->pipeline_sets++;
}

static void bind_buffer_call(void *receiver, void *selector, void *buffer,
    size_t offset, size_t slot) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->encoder && selector == fixture->runtime.set_buffer);
    assert(slot < DOE_MTL_BUFFER_SLOTS);
    fixture->bound[slot].buffer = buffer;
    fixture->bound[slot].offset = offset;
    fixture->buffer_sets++;
}

static void bind_bytes_call(void *receiver, void *selector, const void *bytes,
    size_t length, size_t slot) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->encoder && selector == fixture->runtime.set_bytes);
    assert(length <= sizeof(fixture->sizes));
    memcpy(fixture->sizes, bytes, length);
    fixture->bytes_length = length;
    fixture->bytes_slot = slot;
    fixture->bytes_sets++;
}

static void dispatch_call(void *receiver, void *selector, doe_mtl_size grid, doe_mtl_size group) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->encoder && selector == fixture->runtime.dispatch);
    fixture->grid = grid;
    fixture->group = group;
    fixture->dispatches++;
}

static void copy_call(void *receiver, void *selector, void *source,
    size_t source_offset, void *destination, size_t destination_offset, size_t length) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->encoder && selector == fixture->runtime.copy_buffer);
    assert(source == &fixture->source_buffer && destination == &fixture->destination_buffer);
    fixture->source_offset = source_offset;
    fixture->destination_offset = destination_offset;
    fixture->length = length;
    fixture->copies++;
}

static void fill_call(void *receiver, void *selector, void *buffer,
    doe_mtl_range_abi range, uint8_t value) {
    call_fixture *fixture = owner(receiver);
    assert(receiver == &fixture->encoder && selector == fixture->runtime.fill_buffer);
    assert(buffer == &fixture->destination_buffer);
    fixture->destination_offset = range.location;
    fixture->length = range.length;
    fixture->value = value;
    fixture->fills++;
}

static void fixture_init(call_fixture *fixture) {
    doe_mtl_objc *runtime = &fixture->runtime;
    call_object *objects[] = {&fixture->pool_class, &fixture->string_class, &fixture->pool,
        &fixture->device, &fixture->queue, &fixture->command, &fixture->source,
        &fixture->name, &fixture->library, &fixture->function, &fixture->pipeline,
        &fixture->encoder, &fixture->source_buffer, &fixture->destination_buffer,
        &fixture->error, &fixture->description};
    size_t i;
    memset(fixture, 0, sizeof(*fixture));
    for (i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i) objects[i]->fixture = fixture;
    runtime->pool_class = &fixture->pool_class;
    runtime->string_class = &fixture->string_class;
    runtime->new_object = &fixture->selector[SELECT_NEW];
    runtime->alloc = &fixture->selector[SELECT_ALLOC];
    runtime->retain = &fixture->selector[SELECT_RETAIN];
    runtime->release = &fixture->selector[SELECT_RELEASE];
    runtime->drain = &fixture->selector[SELECT_DRAIN];
    runtime->utf8 = &fixture->selector[SELECT_UTF8];
    runtime->string_init = &fixture->selector[SELECT_STRING_INIT];
    runtime->new_library = &fixture->selector[SELECT_NEW_LIBRARY];
    runtime->new_function = &fixture->selector[SELECT_NEW_FUNCTION];
    runtime->new_pipeline = &fixture->selector[SELECT_NEW_PIPELINE];
    runtime->pipeline_max_threads = &fixture->selector[SELECT_PIPELINE_MAX];
    runtime->localized_description = &fixture->selector[SELECT_DESCRIPTION];
    runtime->code = &fixture->selector[SELECT_CODE];
    runtime->new_command = &fixture->selector[SELECT_NEW_COMMAND];
    runtime->blit_encoder = &fixture->selector[SELECT_BLIT_ENCODER];
    runtime->compute_encoder = &fixture->selector[SELECT_COMPUTE_ENCODER];
    runtime->copy_buffer = &fixture->selector[SELECT_COPY];
    runtime->fill_buffer = &fixture->selector[SELECT_FILL];
    runtime->end_encoding = &fixture->selector[SELECT_END];
    runtime->set_pipeline = &fixture->selector[SELECT_PIPELINE];
    runtime->set_buffer = &fixture->selector[SELECT_BUFFER];
    runtime->set_bytes = &fixture->selector[SELECT_BYTES];
    runtime->dispatch = &fixture->selector[SELECT_DISPATCH];
    runtime->commit = &fixture->selector[SELECT_COMMIT];
    runtime->wait = &fixture->selector[SELECT_WAIT];
    runtime->status = &fixture->selector[SELECT_STATUS];
    runtime->error = &fixture->selector[SELECT_ERROR];
    runtime->object_call = object_call;
    runtime->void_call = void_call;
    runtime->size_call = size_call;
    runtime->integer_call = integer_call;
    runtime->string_call = string_call;
    runtime->string_init_call = string_init_call;
    runtime->library_call = library_call;
    runtime->named_call = named_call;
    runtime->pipeline_call = pipeline_call;
    runtime->set_object_call = set_object_call;
    runtime->bind_buffer_call = bind_buffer_call;
    runtime->bind_bytes_call = bind_bytes_call;
    runtime->dispatch_call = dispatch_call;
    runtime->copy_call = copy_call;
    runtime->fill_call = fill_call;
}

static void pipeline_controls(void) {
    unsigned failure;
    for (failure = 0; failure < 4; ++failure) {
        call_fixture fixture;
        doe_mtl_error error = {0};
        void *out = NULL;
        size_t maximum = 0;
        doe_mtl_pipeline_desc desc = {"a\0bcd", 5, "kernel", {1, 1, 1}, UINT32_MAX};
        fixture_init(&fixture);
        fixture.error_code = -7654;
        fixture.fail_library = failure == 1;
        fixture.fail_function = failure == 2;
        fixture.fail_pipeline = failure == 3;
        if (failure == 0) {
            assert(doe_mtl_objc_pipeline(&fixture.runtime, &fixture.device, &desc,
                &out, &maximum, &error) == DOE_MTL_OK);
            assert(out == &fixture.pipeline && maximum == 256);
            doe_mtl_objc_release(&fixture.runtime, out);
        } else {
            assert(doe_mtl_objc_pipeline(&fixture.runtime, &fixture.device, &desc,
                &out, &maximum, &error) == DOE_MTL_NATIVE_FAILURE);
            assert(out == NULL && maximum == 0);
            if (failure != 2) {
                assert(error.native_code == -7654);
                assert(strstr(error.message, "preserved") != NULL);
            }
        }
        assert(fixture.source.references == 0 && fixture.name.references == 0);
        assert(fixture.library.references == 0 && fixture.function.references == 0);
        assert(fixture.pipeline.references == 0 && fixture.pools == fixture.drains);
    }
}

static void encoding_controls(void) {
    call_fixture fixture;
    doe_mtl_native_resources resources = {0};
    void *command = NULL;
    fixture_init(&fixture);
    assert(doe_mtl_objc_command(&fixture.runtime, &fixture.queue, &command, NULL) == DOE_MTL_OK);
    assert(command == &fixture.command && fixture.command.references == 1 && fixture.retain_calls == 1);
    assert(doe_mtl_objc_copy(&fixture.runtime, command, &fixture.source_buffer, 12,
        &fixture.destination_buffer, 28, 40, NULL) == DOE_MTL_OK);
    assert(fixture.source_offset == 12 && fixture.destination_offset == 28 && fixture.length == 40);
    assert(doe_mtl_objc_fill(&fixture.runtime, command, &fixture.destination_buffer,
        16, 32, 0x87, NULL) == DOE_MTL_OK);
    assert(fixture.destination_offset == 16 && fixture.length == 32 && fixture.value == 0x87);
    resources.buffers[3] = (doe_mtl_native_binding){&fixture.source_buffer, 12, 28};
    resources.buffers[7] = (doe_mtl_native_binding){&fixture.destination_buffer, 4, 16};
    assert(doe_mtl_objc_dispatch(&fixture.runtime, command, &fixture.pipeline,
        &resources, 30, (doe_mtl_dimensions){2, 5, 7}, (doe_mtl_dimensions){8, 4, 2}, 3, NULL, NULL) == DOE_MTL_OK);
    assert(fixture.pipeline_sets == 1 && fixture.buffer_sets == 31 && fixture.dispatches == 3);
    assert(fixture.bound[3].buffer == &fixture.source_buffer && fixture.bound[3].offset == 12);
    assert(fixture.bound[7].buffer == &fixture.destination_buffer && fixture.bound[7].offset == 4);
    assert(fixture.bound[0].buffer == NULL && fixture.bound[0].offset == 0);
    assert(fixture.sizes[3] == 28 && fixture.sizes[7] == 16 && fixture.sizes[0] == 0);
    assert(fixture.bytes_slot == 30 && fixture.bytes_length == 124 && fixture.bytes_sets == 1);
    assert(fixture.grid.width == 2 && fixture.grid.height == 5 && fixture.grid.depth == 7);
    assert(fixture.group.width == 8 && fixture.group.height == 4 && fixture.group.depth == 2);
    assert(fixture.ends == 3 && fixture.pools == fixture.drains);
    fixture.fail_encoder = true;
    assert(doe_mtl_objc_dispatch(&fixture.runtime, command, &fixture.pipeline,
        &resources, UINT32_MAX, (doe_mtl_dimensions){1, 1, 1},
        (doe_mtl_dimensions){1, 1, 1}, 1, NULL, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(fixture.ends == 3 && fixture.dispatches == 3 && fixture.pools == fixture.drains);
    doe_mtl_objc_release(&fixture.runtime, command);
    assert(fixture.command.references == 0);
}

static void completion_controls(void) {
    call_fixture fixture;
    int64_t code = 123;
    fixture_init(&fixture);
    assert(doe_mtl_objc_observe(&fixture.runtime, &fixture.command, true, &code) == 0);
    assert(code == 0 && fixture.waits == 0);
    doe_mtl_objc_commit(&fixture.runtime, &fixture.command);
    assert(fixture.commits == 1 && fixture.native_status == 2);
    assert(doe_mtl_objc_observe(&fixture.runtime, &fixture.command, false, &code) == 2);
    assert(fixture.waits == 0);
    fixture.wait_status = 5;
    fixture.error_code = -54321;
    assert(doe_mtl_objc_observe(&fixture.runtime, &fixture.command, true, &code) == 5);
    assert(fixture.waits == 1 && code == -54321);
    fixture.native_status = 3;
    fixture.wait_status = 4;
    assert(doe_mtl_objc_observe(&fixture.runtime, &fixture.command, true, &code) == 4);
    assert(fixture.waits == 2 && code == 0 && fixture.pools == fixture.drains);
    fixture.fail_pool = true;
    assert(doe_mtl_objc_observe(&fixture.runtime, &fixture.command, true, &code) == SIZE_MAX);
    assert(fixture.waits == 2);
}

int main(void) {
    pipeline_controls();
    encoding_controls();
    completion_controls();
    puts("Metal typed-call controls: pipeline cleanup, exact dispatch/copy/fill and completion passed; no SDK/GPU");
    return 0;
}
