/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Derived ownership contract and source attribution: ../PROVENANCE.md. */
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Noreturn void di_fatal(const char *operation, unsigned status)
{
    fprintf(stderr, "C instance %s failed closed: status %u\n", operation, status);
    abort();
}

static int valid_alignment(size_t alignment)
{
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

static int allocation_layout(doe_future_storage_requirements queue,
                             size_t *offset, size_t *size, size_t *alignment)
{
    const size_t control_alignment = _Alignof(doe_instance);
    if (!valid_alignment(queue.alignment) || !valid_alignment(control_alignment))
        return 0;
    if (sizeof(doe_instance) > SIZE_MAX - (queue.alignment - 1)) return 0;
    *offset = (sizeof(doe_instance) + queue.alignment - 1) & ~(queue.alignment - 1);
    if (queue.size > SIZE_MAX - *offset) return 0;
    *size = *offset + queue.size;
    *alignment = queue.alignment > control_alignment ? queue.alignment : control_alignment;
    return 1;
}

static int valid_allocator(doe_instance_allocator allocator)
{
    return allocator.allocate != NULL && allocator.deallocate != NULL;
}

doe_instance_status doe_instance_create(const doe_instance_config *config,
                                        doe_instance **out)
{
    doe_instance_config saved;
    doe_future_storage_requirements requirements;
    doe_future_status status;
    doe_instance *instance;
    size_t offset, size, alignment;
    if (out == NULL) return DOE_INSTANCE_INVALID_ARGUMENT;
    *out = NULL;
    if (config == NULL) return DOE_INSTANCE_INVALID_ARGUMENT;
    if (config->abi_version != DOE_INSTANCE_ABI_VERSION)
        return DOE_INSTANCE_UNSUPPORTED_VERSION;
    if (!valid_allocator(config->object_allocator) ||
        !valid_allocator(config->wait_allocator)) return DOE_INSTANCE_INVALID_ARGUMENT;
    saved = *config;
    status = doe_future_queue_requirements(DOE_FUTURES_ABI_VERSION, &requirements);
    if (status != DOE_FUTURE_OK) di_fatal("queue requirements", status);
    if (!allocation_layout(requirements, &offset, &size, &alignment))
        return DOE_INSTANCE_LAYOUT_OVERFLOW;
    instance = saved.object_allocator.allocate(saved.object_allocator.state,
        saved.object_allocator.vtable, size, alignment);
    if (instance == NULL) return DOE_INSTANCE_OUT_OF_MEMORY;
    if (saved.allocated != NULL)
        saved.allocated(saved.hook_userdata, instance, size, alignment);
    *instance = (doe_instance){
        .magic = DOE_INSTANCE_MAGIC, .config = saved,
        .allocation_size = size, .allocation_alignment = alignment
    };
    atomic_init(&instance->references, 1);
    status = doe_future_queue_init(DOE_FUTURES_ABI_VERSION,
        (unsigned char *)instance + offset, requirements.size, &instance->queue);
    if (status == DOE_FUTURE_CLEANUP_REQUIRED) di_fatal("queue init cleanup", status);
    if (status != DOE_FUTURE_OK) {
        saved.object_allocator.deallocate(saved.object_allocator.state,
            saved.object_allocator.vtable, instance, size, alignment);
        return DOE_INSTANCE_QUEUE_INITIALIZATION;
    }
    *out = instance;
    return DOE_INSTANCE_OK;
}

doe_instance *doe_instance_from_handle(void *readable_handle)
{
    uint32_t magic;
    if (readable_handle == NULL) return NULL;
    memcpy(&magic, readable_handle, sizeof(magic));
    return magic == DOE_INSTANCE_MAGIC ? readable_handle : NULL;
}

void doe_instance_retain(doe_instance *instance)
{
    uint32_t count;
    if (instance == NULL) return;
    count = atomic_load_explicit(&instance->references, memory_order_relaxed);
    for (;;) {
        if (count == 0 || count >= DI_REFERENCE_LIMIT) di_fatal("retain count", count);
        if (atomic_compare_exchange_weak_explicit(&instance->references, &count,
            count + 1, memory_order_relaxed, memory_order_relaxed)) return;
    }
}

static void finalize(doe_instance *instance)
{
    const doe_instance_config config = instance->config;
    const size_t size = instance->allocation_size;
    const size_t alignment = instance->allocation_alignment;
    const doe_future_status status = doe_future_queue_deinit(instance->queue);
    if (status != DOE_FUTURE_OK) di_fatal("queue deinit", status);
    if (config.remove_label != NULL) config.remove_label(config.hook_userdata, instance);
    instance->magic = 0;
    config.object_allocator.deallocate(config.object_allocator.state,
        config.object_allocator.vtable, instance, size, alignment);
}

void doe_instance_release(doe_instance *instance)
{
    uint32_t count;
    int guarded;
    if (instance == NULL) return;
    guarded = instance->config.external_texture_count != NULL &&
        instance->config.external_texture_count(instance->config.hook_userdata, instance) > 0;
    count = atomic_load_explicit(&instance->references, memory_order_relaxed);
    for (;;) {
        if (count == 0) di_fatal("release count", count);
        if (guarded && count == 1) return;
        if (atomic_compare_exchange_weak_explicit(&instance->references, &count,
            count - 1, memory_order_acq_rel, memory_order_relaxed)) break;
    }
    if (count == 1) finalize(instance);
}

void doe_instance_undo_retain(doe_instance *instance)
{
    uint32_t count;
    if (instance == NULL) return;
    count = atomic_load_explicit(&instance->references, memory_order_relaxed);
    for (;;) {
        if (count <= 1) di_fatal("undo retain count", count);
        if (atomic_compare_exchange_weak_explicit(&instance->references, &count,
            count - 1, memory_order_acq_rel, memory_order_relaxed)) return;
    }
}

static void retain_lease(void *userdata)
{
    doe_instance_retain(userdata);
}

static void release_lease(void *userdata)
{
    doe_instance_release(userdata);
}

doe_instance_queue_lease doe_instance_borrow_queue(doe_instance *instance)
{
    if (instance == NULL) di_fatal("borrow queue", DOE_INSTANCE_INVALID_ARGUMENT);
    return (doe_instance_queue_lease){instance->queue, instance, retain_lease, release_lease};
}
