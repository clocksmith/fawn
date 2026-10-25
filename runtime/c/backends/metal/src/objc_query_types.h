/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_OBJC_QUERY_TYPES_H
#define DOE_MTL_OBJC_QUERY_TYPES_H
/* Included after NSUInteger/BOOL/NSRange definitions in objc_abi.h. */
typedef doe_objc_bool (*doe_objc_equal_string_call)(void *, void *, void *);
typedef void (*doe_objc_sample_counter_call)(void *, void *, void *, size_t, doe_objc_bool);
typedef void (*doe_objc_resolve_counter_call)(void *, void *, void *, doe_mtl_range_abi, void *, size_t);
typedef void (*doe_objc_event_call)(void *, void *, void *, uint64_t);
typedef struct doe_mtl_objc_queries {
    doe_objc_equal_string_call equal_string;
    doe_objc_sample_counter_call sample;
    doe_objc_resolve_counter_call resolve;
    doe_objc_event_call event;
    void *device, *new_event, *signal_event, *wait_event;
    void *descriptor_class, *compute_pass_class;
    void *common_timestamp_set, *common_timestamp;
    void *counter_sets, *counters, *count, *equal, *supports;
    void *set_counter_set, *set_count, *new_buffer, *sample_counter, *resolve_counter;
    void *attachments, *set_sample_buffer, *set_begin, *set_end, *compute_encoder;
} doe_mtl_objc_queries;
#endif
