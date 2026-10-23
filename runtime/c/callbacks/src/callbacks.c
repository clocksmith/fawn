/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_callbacks.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct doe_callback_event {
    doe_callback_allocator allocator;
    doe_callback_owner owner;
    doe_future_completion *completion;
    doe_callback_deliver_context deliver;
    void *context;
    size_t allocation_size;
    size_t allocation_alignment;
};

static void require_ok(const char *operation, doe_future_status status)
{
    if (status == DOE_FUTURE_OK) return;
    fprintf(stderr, "C callbacks %s failed closed: status %d\n", operation,
            (int)status);
    abort();
}

static bool power_of_two(size_t alignment)
{
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

static bool add_size(size_t first, size_t second, size_t *out)
{
    if (second > SIZE_MAX - first) return false;
    *out = first + second;
    return true;
}

static bool align_size(size_t size, size_t alignment, size_t *out)
{
    size_t padded;
    if (!add_size(size, alignment - 1, &padded)) return false;
    *out = padded & ~(alignment - 1);
    return true;
}

static void dispatch(doe_future_completion *completion, void *userdata)
{
    doe_callback_event *event = userdata;
    /* The callback can release the last caller reference. Snapshot everything
     * needed after delivery; the completion stays registered through cleanup. */
    const doe_callback_event saved = *event;
    saved.deliver(saved.context);
    require_ok("finish completion", doe_future_finish(completion));
    require_ok("completion deinit", doe_future_completion_deinit(completion));
    saved.allocator.deallocate(saved.allocator.state, saved.allocator.vtable,
        event, saved.allocation_size, saved.allocation_alignment);
    saved.owner.release(saved.owner.userdata);
}

void doe_callback_mark_ready(doe_callback_event *event)
{
    const doe_callback_owner owner = event->owner;
    doe_future_completion *completion = event->completion;
    owner.retain(owner.userdata);
    const doe_future_status status = doe_future_mark_ready(completion);
    /* Neither event nor completion need exist now. The separate call lease
     * keeps the queue alive until accepted-C active-call accounting returns. */
    require_ok("publish readiness", status);
    owner.release(owner.userdata);
}

static void publish_ready(doe_callback_event *event, void *userdata)
{
    (void)userdata;
    doe_callback_mark_ready(event);
}

doe_callback_status doe_callback_ready(const doe_callback_request *request,
                                        uint64_t *out_future_id)
{
    return doe_callback_publish(request, publish_ready, NULL, out_future_id);
}

doe_callback_status doe_callback_publish(const doe_callback_request *request,
                                          doe_callback_publisher publisher,
                                          void *publisher_userdata,
                                          uint64_t *out_future_id)
{
    if (request == NULL || out_future_id == NULL)
        return DOE_CALLBACK_INVALID_ARGUMENT;
    if (request->abi_version != DOE_CALLBACKS_ABI_VERSION)
        return DOE_CALLBACK_UNSUPPORTED_VERSION;
    if (request->mode > DOE_FUTURE_MODE_ALLOW_SPONTANEOUS)
        return DOE_CALLBACK_UNSUPPORTED_MODE;
    if (request->owner == NULL && request->mode != 0 &&
        request->mode != DOE_FUTURE_MODE_ALLOW_SPONTANEOUS)
        return DOE_CALLBACK_INSTANCE_UNAVAILABLE;
    if (request->source_context == NULL || request->deliver == NULL ||
        !power_of_two(request->context_alignment))
        return DOE_CALLBACK_INVALID_ARGUMENT;
    if (request->owner == NULL) {
        request->deliver(request->source_context);
        uint64_t id;
        require_ok("legacy identity", doe_future_legacy_id(&id));
        *out_future_id = id;
        return DOE_CALLBACK_OK;
    }
    if (request->owner->queue == NULL || request->owner->retain == NULL ||
        request->owner->release == NULL || request->allocator.allocate == NULL ||
        request->allocator.deallocate == NULL || request->copy == NULL ||
        publisher == NULL)
        return DOE_CALLBACK_INVALID_ARGUMENT;

    doe_future_storage_requirements completion;
    require_ok("completion requirements", doe_future_completion_requirements(
        DOE_FUTURES_ABI_VERSION, &completion));
    if (!power_of_two(completion.alignment) || completion.size == 0)
        require_ok("completion requirements", DOE_FUTURE_INVALID_STATE);
    size_t completion_offset, completion_end, context_offset, allocation_size;
    const size_t context_size = request->context_size == 0 ? 1 : request->context_size;
    if (!align_size(sizeof(doe_callback_event), completion.alignment,
                    &completion_offset) ||
        !add_size(completion_offset, completion.size, &completion_end) ||
        !align_size(completion_end, request->context_alignment, &context_offset) ||
        !add_size(context_offset, context_size, &allocation_size))
        return DOE_CALLBACK_LAYOUT_OVERFLOW;
    size_t alignment = _Alignof(doe_callback_event);
    if (completion.alignment > alignment) alignment = completion.alignment;
    if (request->context_alignment > alignment)
        alignment = request->context_alignment;

    void *allocation = request->allocator.allocate(request->allocator.state,
        request->allocator.vtable, allocation_size, alignment);
    if (allocation == NULL) return DOE_CALLBACK_OUT_OF_MEMORY;
    if (((uintptr_t)allocation & (alignment - 1)) != 0)
        require_ok("allocator alignment", DOE_FUTURE_MISALIGNED_STORAGE);
    uint64_t id;
    require_ok("instance identity", doe_future_new_id(request->owner->queue, &id));
    doe_callback_event *event = allocation;
    *event = (doe_callback_event){
        .allocator = request->allocator,
        .owner = *request->owner,
        .deliver = request->deliver,
        .context = (unsigned char *)allocation + context_offset,
        .allocation_size = allocation_size,
        .allocation_alignment = alignment
    };
    require_ok("completion init", doe_future_completion_init(
        DOE_FUTURES_ABI_VERSION, (unsigned char *)allocation + completion_offset,
        completion.size, (doe_future_mode)request->mode, dispatch, event,
        &event->completion));
    request->copy(event->context, request->source_context);
    event->owner.retain(event->owner.userdata);
    require_ok("register completion", doe_future_register(event->owner.queue,
        event->completion, id));
    publisher(event, publisher_userdata);
    /* Publication may already have freed event and its owner. */
    *out_future_id = id;
    return DOE_CALLBACK_OK;
}
