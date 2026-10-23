/* SPDX-License-Identifier: MIT
 * Actual Vulkan object adoption helpers shared by software-only consumers. */
#ifndef DOE_RESOURCE_VULKAN_FIXTURE_H
#define DOE_RESOURCE_VULKAN_FIXTURE_H
#include "doe_resources_extended.h"
#include "doe_vk_texture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct lifecycle { unsigned native_releases; size_t allocations; } lifecycle;
static inline void *allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    lifecycle *life = state;
    assert(!vtable && alignment <= _Alignof(max_align_t));
    void *memory = malloc(size);
    if (memory) life->allocations++;
    return memory;
}
static inline void deallocate(void *state, const void *vtable, void *memory, size_t size, size_t alignment)
{
    lifecycle *life = state;
    assert(!vtable && size && alignment <= _Alignof(max_align_t) && life->allocations);
    life->allocations--;
    free(memory);
}
static inline bool alive(void *context) { return doe_vk_context_alive(context); }
static inline void retain(void *context) { if (doe_vk_context_retain(context) != DOE_VK_OK) abort(); }
static inline void release(void *context) { doe_vk_context_release(context); }
static inline void release_buffer(void *state, void *buffer)
{
    ((lifecycle *)state)->native_releases++;
    doe_vk_buffer_release(buffer);
}
static inline void release_texture(void *state, void *texture)
{
    ((lifecycle *)state)->native_releases++;
    doe_vk_texture_release(texture);
}
static inline void require(doe_vk_status status, const char *operation)
{
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}

static inline void release_view(void *state, void *view)
{
    ((lifecycle *)state)->native_releases++;
    doe_vk_texture_view_release(view);
}
static inline void release_pipeline(void *state, void *pipeline)
{
    ((lifecycle *)state)->native_releases++;
    doe_vk_pipeline_release(pipeline);
}
#endif
