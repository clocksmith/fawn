/* SPDX-License-Identifier: MIT
 * Typed Objective-C call control; no system runtime or counter values. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct fixture fixture;
typedef struct object { fixture *fixture; } object;
struct fixture {
    doe_mtl_objc runtime;
    object pool_class, pool, device, descriptor_class, descriptor, sets, set, set_name,
        counters, counter, counter_name, common_set, common_counter, samples,
        command, encoder, destination, compute_class, compute_descriptor, attachments, attachment,
        error, description, event;
    unsigned tokens[32];
    unsigned pools, drains, releases, ends, samples_called, resolves, compute_encoders, signals, waits, events;
    uint64_t event_value;
    bool wrong_set, wrong_counter, extra_counter, stage, blit, fail_pool, fail_descriptor,
        fail_buffer, fail_encoder, missing_selector, fail_attachment, fail_event;
    size_t count, storage, sample_index, resolve_first, resolve_count, destination_offset, begin, end;
};
static fixture *owner(void *receiver) { assert(receiver != NULL); return ((object *)receiver)->fixture; }
static void *object_call(void *receiver, void *selector) {
    fixture *f = owner(receiver);
    doe_mtl_objc *r = &f->runtime;
    doe_mtl_objc_queries *q = &r->queries;
    if (selector == r->new_object) {
        if (receiver == &f->pool_class) { if (f->fail_pool) return NULL; f->pools++; return &f->pool; }
        if (f->fail_descriptor) return NULL;
        if (receiver == &f->descriptor_class) return &f->descriptor;
        assert(receiver == &f->compute_class);
        return &f->compute_descriptor;
    }
    if (selector == q->device) { assert(receiver == &f->command); return &f->device; }
    if (selector == q->new_event) { assert(receiver == &f->device); f->events++; return f->fail_event ? NULL : &f->event; }
    if (selector == q->counter_sets) { assert(receiver == &f->device); return &f->sets; }
    if (selector == q->counters) { assert(receiver == &f->set); return &f->counters; }
    if (selector == r->name) {
        if (receiver == &f->set) return &f->set_name;
        assert(receiver == &f->counter); return &f->counter_name;
    }
    if (selector == r->blit_encoder) {
        assert(receiver == &f->command); return f->fail_encoder ? NULL : &f->encoder;
    }
    if (selector == q->attachments) {
        assert(receiver == &f->compute_descriptor); return &f->attachments;
    }
    assert(selector == r->localized_description && receiver == &f->error);
    return &f->description;
}
static void void_call(void *receiver, void *selector) {
    fixture *f = owner(receiver);
    if (selector == f->runtime.drain) { assert(receiver == &f->pool); f->drains++; }
    else if (selector == f->runtime.release) { assert(receiver == &f->descriptor || receiver == &f->compute_descriptor); f->releases++; }
    else { assert(selector == f->runtime.end_encoding && receiver == &f->encoder); f->ends++; }
}
static size_t size_call(void *receiver, void *selector) {
    fixture *f = owner(receiver);
    assert(selector == f->runtime.queries.count);
    if (receiver == &f->sets) return 1;
    assert(receiver == &f->counters);
    return f->extra_counter ? 2 : 1;
}
static void *index_call(void *receiver, void *selector, size_t index) {
    fixture *f = owner(receiver);
    assert(selector == f->runtime.graphics.index && index == 0);
    if (receiver == &f->sets) return &f->set;
    if (receiver == &f->counters) return &f->counter;
    assert(receiver == &f->attachments);
    return f->fail_attachment ? NULL : &f->attachment;
}
static doe_objc_bool equal_call(void *receiver, void *selector, void *other) {
    fixture *f = owner(receiver);
    assert(selector == f->runtime.queries.equal);
    if (receiver == &f->set_name) { assert(other == &f->common_set); return (doe_objc_bool)!f->wrong_set; }
    assert(receiver == &f->counter_name && other == &f->common_counter);
    return (doe_objc_bool)!f->wrong_counter;
}
static doe_objc_bool responds_call(void *receiver, void *selector, void *method) {
    fixture *f = owner(receiver);
    assert(selector == f->runtime.responds && method != NULL);
    return (doe_objc_bool)!f->missing_selector;
}
static doe_objc_bool supports_call(void *receiver, void *selector, size_t point) {
    fixture *f = owner(receiver);
    assert(receiver == &f->device && selector == f->runtime.queries.supports);
    assert(point == 0 || point == 4);
    return (doe_objc_bool)(point == 0 ? f->stage : f->blit);
}
static void set_object(void *receiver, void *selector, void *value) {
    fixture *f = owner(receiver);
    if (receiver == &f->descriptor) assert(selector == f->runtime.queries.set_counter_set && value == &f->set);
    else assert(receiver == &f->attachment && selector == f->runtime.queries.set_sample_buffer && value == &f->samples);
}
static void set_size(void *receiver, void *selector, size_t value) {
    fixture *f = owner(receiver);
    doe_mtl_objc_queries *q = &f->runtime.queries;
    if (selector == q->set_count) { assert(receiver == &f->descriptor); f->count = value; }
    else if (selector == f->runtime.set_storage) { assert(receiver == &f->descriptor); f->storage = value; }
    else if (selector == q->set_begin) { assert(receiver == &f->attachment); f->begin = value; }
    else { assert(receiver == &f->attachment && selector == q->set_end); f->end = value; }
}
static void *new_buffer(void *receiver, void *selector, void *descriptor, void **error) {
    fixture *f = owner(receiver);
    assert(receiver == &f->device && selector == f->runtime.queries.new_buffer && descriptor == &f->descriptor);
    if (f->fail_buffer) { *error = &f->error; return NULL; }
    return &f->samples;
}
static intptr_t integer_call(void *receiver, void *selector) {
    fixture *f = owner(receiver);
    assert(receiver == &f->error && selector == f->runtime.code);
    return -811;
}
static const char *string_call(void *receiver, void *selector) {
    fixture *f = owner(receiver);
    assert(receiver == &f->description && selector == f->runtime.utf8);
    return "native counter creation failed";
}
static void sample_call(void *receiver, void *selector, void *buffer, size_t index, doe_objc_bool barrier) {
    fixture *f = owner(receiver);
    assert(receiver == &f->encoder && selector == f->runtime.queries.sample_counter && buffer == &f->samples && barrier);
    f->samples_called++;
    f->sample_index = index;
}
static void event_call(void *receiver, void *selector, void *event, uint64_t value) {
    fixture *f = owner(receiver);
    assert(receiver == &f->command && event == &f->event && value != 0);
    if (selector == f->runtime.queries.signal_event) {
        assert(f->signals == f->waits);
        f->signals++; f->event_value = value;
    } else {
        assert(selector == f->runtime.queries.wait_event && f->signals == f->waits + 1);
        assert(value == f->event_value); f->waits++;
    }
}
static void resolve_call(void *receiver, void *selector, void *buffer, doe_mtl_range_abi range, void *destination, size_t offset) {
    fixture *f = owner(receiver);
    assert(receiver == &f->encoder && selector == f->runtime.queries.resolve_counter && buffer == &f->samples && destination == &f->destination);
    assert(f->signals == f->waits && f->waits > f->resolves);
    f->resolves++;
    f->resolve_first = range.location;
    f->resolve_count = range.length;
    f->destination_offset = offset;
}
static void *compute_call(void *receiver, void *selector, void *descriptor) {
    fixture *f = owner(receiver);
    assert(receiver == &f->command && selector == f->runtime.queries.compute_encoder && descriptor == &f->compute_descriptor);
    f->compute_encoders++;
    return f->fail_encoder ? NULL : &f->encoder;
}
static void init(fixture *f) {
    doe_mtl_objc *r = &f->runtime;
    doe_mtl_objc_queries *q = &r->queries;
    object *objects[] = {&f->pool_class, &f->pool, &f->device, &f->descriptor_class, &f->descriptor,
        &f->sets, &f->set, &f->set_name, &f->counters, &f->counter, &f->counter_name, &f->common_set,
        &f->common_counter, &f->samples, &f->command, &f->encoder, &f->destination,
        &f->compute_class, &f->compute_descriptor, &f->attachments, &f->attachment, &f->error, &f->description, &f->event};
    size_t i;
    memset(f, 0, sizeof(*f));
    for (i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i) objects[i]->fixture = f;
    r->pool_class = &f->pool_class;
    q->descriptor_class = &f->descriptor_class;
    q->compute_pass_class = &f->compute_class;
    q->common_timestamp_set = &f->common_set;
    q->common_timestamp = &f->common_counter;
    r->new_object = &f->tokens[0];
    r->drain = &f->tokens[1];
    r->release = &f->tokens[2];
    r->responds = &f->tokens[3];
    r->name = &f->tokens[4];
    r->graphics.index = &f->tokens[5];
    r->set_storage = &f->tokens[6];
    r->blit_encoder = &f->tokens[7];
    r->end_encoding = &f->tokens[8];
    r->localized_description = &f->tokens[9];
    r->code = &f->tokens[10];
    r->utf8 = &f->tokens[11];
    q->counter_sets = &f->tokens[12];
    q->counters = &f->tokens[13];
    q->count = &f->tokens[14];
    q->equal = &f->tokens[15];
    q->supports = &f->tokens[16];
    q->set_counter_set = &f->tokens[17];
    q->set_count = &f->tokens[18];
    q->new_buffer = &f->tokens[19];
    q->sample_counter = &f->tokens[20];
    q->resolve_counter = &f->tokens[21];
    q->attachments = &f->tokens[22];
    q->set_sample_buffer = &f->tokens[23];
    q->set_begin = &f->tokens[24];
    q->set_end = &f->tokens[25];
    q->compute_encoder = &f->tokens[26];
    q->device = &f->tokens[27];
    q->new_event = &f->tokens[28];
    q->signal_event = &f->tokens[29];
    q->wait_event = &f->tokens[30];
    q->event = event_call;
    r->object_call = object_call;
    r->void_call = void_call;
    r->size_call = size_call;
    r->responds_call = responds_call;
    r->samples_call = supports_call;
    r->graphics.index_object = index_call;
    r->set_object_call = set_object;
    r->set_size_call = set_size;
    r->pipeline_call = new_buffer;
    r->integer_call = integer_call;
    r->string_call = string_call;
    r->named_call = compute_call;
    q->equal_string = equal_call;
    q->sample = sample_call;
    q->resolve = resolve_call;
    f->storage = SIZE_MAX;
}
static void discovery_and_creation(void) {
    unsigned scenario;
    for (scenario = 0; scenario < 7; ++scenario) {
        fixture f; doe_mtl_caps caps = {0}; doe_mtl_error error = {0}; void *out = NULL;
        init(&f);
        f.stage = true;
        f.blit = scenario == 0;
        f.wrong_set = scenario == 1; f.wrong_counter = scenario == 2; f.extra_counter = scenario == 3;
        f.fail_buffer = scenario == 4; f.fail_descriptor = scenario == 5; f.missing_selector = scenario == 6;
        doe_mtl_objc_query_caps(&f.runtime, &f.device, &caps);
        assert(caps.timestamp_stage == (scenario == 0 || scenario == 4 || scenario == 5));
        assert(caps.timestamp_blit == (scenario == 0));
        if (scenario == 0) {
            assert(doe_mtl_objc_timestamps(&f.runtime, &f.device, 19, &out, &error) == DOE_MTL_OK);
            assert(out == &f.samples && f.count == 19 && f.storage == 0 && f.releases == 1);
        } else {
            assert(doe_mtl_objc_timestamps(&f.runtime, &f.device, 19, &out, &error) ==
                (scenario == 4 || scenario == 5 ? DOE_MTL_NATIVE_FAILURE : DOE_MTL_UNSUPPORTED));
            assert(out == NULL);
            if (scenario == 4) assert(error.native_code == -811 && strstr(error.message, "counter") != NULL);
        }
        assert(f.pools == f.drains);
    }
}
static void encoding_and_cleanup(void) {
    fixture f;
    doe_mtl_native_timestamps pair;
    void *event = NULL;
    init(&f); pair = (doe_mtl_native_timestamps){&f.samples, 2, 7};
    assert(doe_mtl_objc_timestamp(&f.runtime, &f.command, &f.samples, 5, NULL) == DOE_MTL_OK);
    assert(f.samples_called == 1 && f.sample_index == 5 && f.ends == 1);
    assert(doe_mtl_objc_resolve_timestamps(&f.runtime, &f.command, &f.samples, 3, 4, &f.destination, 24, &event, 1, NULL) == DOE_MTL_OK);
    assert(event == &f.event && f.events == 1 && f.event_value == 1);
    assert(f.resolves == 1 && f.resolve_first == 3 && f.resolve_count == 4 && f.destination_offset == 24 && f.ends == 2);
    assert(doe_mtl_objc_timed_compute(&f.runtime, &f.command, &pair) == &f.encoder);
    assert(f.begin == 2 && f.end == 7 && f.compute_encoders == 1 && f.releases == 1);
    f.fail_attachment = true;
    assert(doe_mtl_objc_timed_compute(&f.runtime, &f.command, &pair) == NULL && f.releases == 2);
    f.missing_selector = true;
    assert(doe_mtl_objc_timestamp(&f.runtime, &f.command, &f.samples, 0, NULL) == DOE_MTL_UNSUPPORTED);
    assert(doe_mtl_objc_resolve_timestamps(&f.runtime, &f.command, &f.samples, 0, 1, &f.destination, 0, &event, 2, NULL) == DOE_MTL_UNSUPPORTED);
    assert(f.ends == 3 && f.samples_called == 1 && f.resolves == 1);
    f.fail_encoder = true;
    assert(doe_mtl_objc_timestamp(&f.runtime, &f.command, &f.samples, 0, NULL) == DOE_MTL_NATIVE_FAILURE);
    f.fail_pool = true;
    assert(doe_mtl_objc_resolve_timestamps(&f.runtime, &f.command, &f.samples, 0, 1, &f.destination, 0, &event, 2, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(f.pools == f.drains);
    init(&f); event = NULL; f.fail_event = true;
    assert(doe_mtl_objc_resolve_timestamps(&f.runtime, &f.command, &f.samples,
        0, 1, &f.destination, 0, &event, 1, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(event == NULL && f.signals == 0 && f.resolves == 0 && f.pools == f.drains);
    f.fail_event = false;
    assert(doe_mtl_objc_resolve_timestamps(&f.runtime, &f.command, &f.samples,
        0, 1, &f.destination, 0, &event, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_objc_resolve_timestamps(&f.runtime, &f.command, &f.samples,
        0, 1, &f.destination, 0, &event, 2, NULL) == DOE_MTL_OK);
    assert(f.events == 2 && f.signals == 2 && f.waits == 2 && f.event_value == 2);
}
int main(void) {
    discovery_and_creation();
    encoding_and_cleanup();
    puts("Metal typed timestamp controls: exact counter-set identity, descriptors, stage indices, blit sample/resolve and failures passed; no SDK/GPU");
    return 0;
}
