/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"
#include <math.h>
#if !defined(_WIN32)
#include <dlfcn.h>

static void *constant_object(void *library, const char *name) {
    void *address = dlsym(library, name);
    void *value = NULL;
    if (address != NULL) memcpy(&value, address, sizeof(value));
    return value;
}

void doe_mtl_objc_load_queries(doe_mtl_objc *r, doe_objc_message message) {
    doe_mtl_objc_queries *q = &r->queries;
    q->equal_string = (doe_objc_equal_string_call)message;
    q->sample = (doe_objc_sample_counter_call)message;
    q->resolve = (doe_objc_resolve_counter_call)message;
    q->event = (doe_objc_event_call)message;
    q->event_value = (doe_objc_event_value_call)message;
    q->signaled_value = r->selector("signaledValue");
    q->clocks = (doe_objc_clock_call)message;
    q->sample_clocks = r->selector("sampleTimestamps:gpuTimestamp:");
    q->blit_pass_class = r->get_class("MTLBlitPassDescriptor");
    q->blit_encoder = r->selector("blitCommandEncoderWithDescriptor:");
    q->device = r->selector("device");
    q->new_event = r->selector("newSharedEvent");
    q->signal_event = r->selector("encodeSignalEvent:value:");
    q->wait_event = r->selector("encodeWaitForEvent:value:");
    q->descriptor_class = r->get_class("MTLCounterSampleBufferDescriptor");
    q->compute_pass_class = r->get_class("MTLComputePassDescriptor");
    q->common_timestamp_set = constant_object(r->metal_library, "MTLCommonCounterSetTimestamp");
    q->common_timestamp = constant_object(r->metal_library, "MTLCommonCounterTimestamp");
    q->counter_sets = r->selector("counterSets");
    q->counters = r->selector("counters");
    q->count = r->selector("count");
    q->equal = r->selector("isEqualToString:");
    q->supports = r->selector("supportsCounterSampling:");
    q->set_counter_set = r->selector("setCounterSet:");
    q->set_count = r->selector("setSampleCount:");
    q->new_buffer = r->selector("newCounterSampleBufferWithDescriptor:error:");
    q->sample_counter = r->selector("sampleCountersInBuffer:atSampleIndex:withBarrier:");
    q->resolve_counter = r->selector("resolveCounters:inRange:destinationBuffer:destinationOffset:");
    q->attachments = r->selector("sampleBufferAttachments");
    q->set_sample_buffer = r->selector("setSampleBuffer:");
    q->set_begin = r->selector("setStartOfEncoderSampleIndex:");
    q->set_end = r->selector("setEndOfEncoderSampleIndex:");
    q->compute_encoder = r->selector("computeCommandEncoderWithDescriptor:");
}
#endif /* These symbols are resolved only by the POSIX Objective-C loader. */

static void *timestamp_set(doe_mtl_objc *r, void *device) {
    doe_mtl_objc_queries *q = &r->queries;
    void *sets;
    size_t i, count;
    if (q->descriptor_class == NULL || q->common_timestamp_set == NULL ||
        q->common_timestamp == NULL ||
        !r->responds_call(device, r->responds, q->counter_sets) ||
        !r->responds_call(device, r->responds, q->new_buffer)) return NULL;
    sets = doe_mtl_objc_object(r, device, q->counter_sets);
    if (sets == NULL) return NULL;
    count = doe_mtl_objc_size(r, sets, q->count);
    for (i = 0; i < count; ++i) {
        void *set = r->graphics.index_object(sets, r->graphics.index, i);
        void *name = doe_mtl_objc_object(r, set, r->name);
        void *counters, *counter, *counter_name;
        if (!q->equal_string(name, q->equal, q->common_timestamp_set)) continue;
        counters = doe_mtl_objc_object(r, set, q->counters);
        /* Exact timestamp-only set preserves the eight-byte result contract;
         * never reinterpret a statistics set which happens to include time. */
        if (counters == NULL || doe_mtl_objc_size(r, counters, q->count) != 1) continue;
        counter = r->graphics.index_object(counters, r->graphics.index, 0);
        counter_name = doe_mtl_objc_object(r, counter, r->name);
        if (q->equal_string(counter_name, q->equal, q->common_timestamp)) return set;
    }
    return NULL;
}

void doe_mtl_objc_query_caps(doe_mtl_objc *r, void *device, doe_mtl_caps *caps) {
    doe_mtl_objc_queries *q = &r->queries;
    if (q->supports == NULL || !r->responds_call(device, r->responds, q->supports)) return;
    uint64_t cpu_begin = 0, gpu_begin = 0, cpu_end = 0, gpu_end = 0;
    bool clock = q->clocks != NULL && q->sample_clocks != NULL &&
        r->responds_call(device, r->responds, q->sample_clocks);
    if (clock) q->clocks(device, q->sample_clocks, &cpu_begin, &gpu_begin);
    if (timestamp_set(r, device) == NULL) return;
    caps->timestamp_stage = q->compute_pass_class != NULL && q->blit_pass_class != NULL &&
        r->samples_call(device, q->supports, 0);
    caps->timestamp_blit = r->samples_call(device, q->supports, 4) != 0;
    if (clock) q->clocks(device, q->sample_clocks, &cpu_end, &gpu_end);
    /* Apple documents sampleTimestamps CPU values in nanoseconds. Their ratio
     * calibrates the GPU clock; mach_absolute_time's scale does not apply.
     * Counter discovery separates the two native samples without polling. */
    if (cpu_end > cpu_begin && gpu_end > gpu_begin) {
        float period = (float)((double)(cpu_end - cpu_begin) / (double)(gpu_end - gpu_begin));
        if (isfinite(period) && period > 0) caps->timestamp_period_ns = period;
    }
}

doe_mtl_status doe_mtl_objc_timestamps(void *userdata, void *device,
    uint32_t count, void **out, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    doe_mtl_objc_queries *q = &r->queries;
    void *pool = doe_mtl_objc_pool(r);
    void *set, *descriptor, *buffer, *native_error = NULL;
    doe_mtl_status status;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    set = timestamp_set(r, device);
    if (set == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "timestamp counter set", error);
    }
    descriptor = doe_mtl_objc_object(r, q->descriptor_class, r->new_object);
    if (descriptor == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "timestamp descriptor", error);
    }
    r->set_object_call(descriptor, q->set_counter_set, set);
    r->set_size_call(descriptor, q->set_count, count);
    r->set_size_call(descriptor, r->set_storage, 0); /* Shared storage. */
    buffer = r->pipeline_call(device, q->new_buffer, descriptor, &native_error);
    status = buffer == NULL ? doe_mtl_objc_error(r, native_error,
        "newCounterSampleBufferWithDescriptor:error:", error) : DOE_MTL_OK;
    doe_mtl_objc_void(r, descriptor, r->release);
    doe_mtl_objc_drain(r, pool);
    if (status != DOE_MTL_OK) return status;
    *out = buffer;
    return doe_mtl_fail(DOE_MTL_OK, "timestamp native buffer", error);
}

doe_mtl_status doe_mtl_objc_timestamp(void *userdata, void *command,
    void *timestamps, uint32_t index, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r);
    void *encoder;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    encoder = doe_mtl_objc_object(r, command, r->blit_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blit timestamp encoder", error);
    }
    if (!r->responds_call(encoder, r->responds, r->queries.sample_counter)) {
        doe_mtl_objc_void(r, encoder, r->end_encoding);
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "blit timestamp selector", error);
    }
    r->queries.sample(encoder, r->queries.sample_counter, timestamps, index, (doe_objc_bool)1);
    doe_mtl_objc_void(r, encoder, r->end_encoding);
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_OK, "sampleCountersInBuffer:atSampleIndex:withBarrier:", error);
}

static doe_mtl_status order_timestamps(doe_mtl_objc *r, void *command,
    void **event, uint64_t value, doe_mtl_error *error) {
    if (!r->responds_call(command, r->responds, r->queries.signal_event) ||
        !r->responds_call(command, r->responds, r->queries.wait_event)) {
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "timestamp resolve ordering", error);
    }
    if (*event == NULL) {
        void *device = doe_mtl_objc_object(r, command, r->queries.device);
        *event = doe_mtl_objc_object(r, device, r->queries.new_event);
        if (*event == NULL) {
            return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newSharedEvent", error);
        }
    }
    /* Stage samples can remain invisible to a later encoder in this command.
     * A native event orders their publication before GPU resolve. Its +1 lease
     * belongs to the existing timestamp use until terminal completion. */
    r->queries.event(command, r->queries.signal_event, *event, value);
    r->queries.event(command, r->queries.wait_event, *event, value);
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_objc_resolve_timestamps(void *userdata, void *command,
    void *timestamps, uint32_t first, uint32_t count, void *destination,
    size_t offset, void **event, uint64_t value, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r);
    void *encoder;
    const doe_mtl_range_abi range = {first, count};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    doe_mtl_status status = order_timestamps(r, command, event, value, error);
    if (status != DOE_MTL_OK) { doe_mtl_objc_drain(r, pool); return status; }
    encoder = doe_mtl_objc_object(r, command, r->blit_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "timestamp resolve encoder", error);
    }
    if (!r->responds_call(encoder, r->responds, r->queries.resolve_counter)) {
        doe_mtl_objc_void(r, encoder, r->end_encoding);
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "timestamp resolve selector", error);
    }
    r->queries.resolve(encoder, r->queries.resolve_counter, timestamps, range, destination, offset);
    doe_mtl_objc_void(r, encoder, r->end_encoding);
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_OK, "resolveCounters:inRange:destinationBuffer:destinationOffset:", error);
}

void *doe_mtl_objc_timed_compute(doe_mtl_objc *r, void *command,
    const doe_mtl_native_timestamps *timestamps) {
    doe_mtl_objc_queries *q = &r->queries;
    void *descriptor, *attachments, *attachment, *encoder = NULL;
    if (q->compute_pass_class == NULL ||
        !r->responds_call(command, r->responds, q->compute_encoder)) return NULL;
    descriptor = doe_mtl_objc_object(r, q->compute_pass_class, r->new_object);
    if (descriptor == NULL) return NULL;
    attachments = doe_mtl_objc_object(r, descriptor, q->attachments);
    attachment = attachments != NULL ? r->graphics.index_object(attachments, r->graphics.index, 0) : NULL;
    if (attachment != NULL) {
        r->set_object_call(attachment, q->set_sample_buffer, timestamps->buffer);
        r->set_size_call(attachment, q->set_begin, timestamps->begin);
        r->set_size_call(attachment, q->set_end, timestamps->end);
        encoder = r->named_call(command, q->compute_encoder, descriptor);
    }
    doe_mtl_objc_void(r, descriptor, r->release);
    return encoder;
}

doe_mtl_status doe_mtl_objc_timestamp_boundary(void *userdata, void *command, void *timestamps,
    uint32_t index, bool stage, void *scratch, void **event, uint64_t value, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    doe_mtl_objc_queries *q = &r->queries;
    void *pool = doe_mtl_objc_pool(r);
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    doe_mtl_status status = order_timestamps(r, command, event, value, error);
    if (status != DOE_MTL_OK) { doe_mtl_objc_drain(r, pool); return status; }
    if (!stage) {
        status = doe_mtl_objc_timestamp(userdata, command, timestamps, index, error);
        doe_mtl_objc_drain(r, pool);
        return status;
    }
    void *descriptor = doe_mtl_objc_object(r, q->blit_pass_class, r->new_object);
    void *encoder = NULL;
    if (descriptor != NULL) {
        void *attachments = doe_mtl_objc_object(r, descriptor, q->attachments);
        void *attachment = attachments != NULL ? r->graphics.index_object(attachments, r->graphics.index, 0) : NULL;
        if (attachment != NULL) {
            r->set_object_call(attachment, q->set_sample_buffer, timestamps);
            r->set_size_call(attachment, q->set_begin, index);
            r->set_size_call(attachment, q->set_end, SIZE_MAX);
            encoder = r->named_call(command, q->blit_encoder, descriptor);
        }
        doe_mtl_objc_void(r, descriptor, r->release);
    }
    if (encoder == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blitCommandEncoderWithDescriptor:", error);
    }
    r->fill_call(encoder, r->fill_buffer, scratch, (doe_mtl_range_abi){0, 4}, 0);
    doe_mtl_objc_void(r, encoder, r->end_encoding);
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_OK, "stage timestamp", error);
}
