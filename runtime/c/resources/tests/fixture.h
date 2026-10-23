#ifndef DOE_RESOURCE_TEST_FIXTURE_H
#define DOE_RESOURCE_TEST_FIXTURE_H
/* SPDX-License-Identifier: MIT
 * Host ownership fixtures only: no GPU implementation or execution oracle. */
#include "doe_resources_extended.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    size_t allocations, frees, live, fail_at;
    unsigned device_references;
    bool alive;
} fixture;
typedef struct native_token { unsigned releases; } native_token;

typedef union allocation_header {
    max_align_t alignment;
    struct { size_t size, alignment; } record;
} allocation_header;

static inline void *allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    fixture *f = state;
    assert(!vtable);
    assert(alignment <= _Alignof(max_align_t));
    if (f->allocations++ == f->fail_at) return NULL;
    assert(size <= SIZE_MAX - sizeof(allocation_header));
    allocation_header *header = malloc(sizeof(*header) + size);
    assert(header);
    header->record.size = size;
    header->record.alignment = alignment;
    f->live++;
    return header + 1;
}

static inline void deallocate(void *state, const void *vtable, void *memory,
    size_t size, size_t alignment)
{
    fixture *f = state;
    assert(!vtable && memory);
    allocation_header *header = (allocation_header *)memory - 1;
    assert(header->record.size == size && header->record.alignment == alignment);
    assert(f->live);
    f->live--;
    f->frees++;
    free(header);
}

static inline bool device_alive(void *identity) { return ((fixture *)identity)->alive; }
static inline void device_retain(void *identity) { ((fixture *)identity)->device_references++; }
static inline void device_release(void *identity)
{
    fixture *f = identity;
    assert(f->device_references);
    f->device_references--;
}
static inline void native_release(void *userdata, void *handle)
{
    fixture *f = userdata;
    native_token *token = handle;
    assert(f->device_references);
    assert(token->releases == 0);
    token->releases++;
}
static inline doe_resource_config config(fixture *f)
{
    return (doe_resource_config){
        .allocator = {f, NULL, allocate, deallocate},
        .device = {f, device_alive, device_retain, device_release}
    };
}
static inline fixture fresh(void)
{
    return (fixture){.fail_at = SIZE_MAX, .alive = true};
}
static inline void empty(const fixture *f)
{
    assert(!f->live && !f->device_references);
}
static inline doe_resource_native native(fixture *f, native_token *token)
{
    return (doe_resource_native){token, f, native_release};
}
static inline doe_resource_buffer *buffer(fixture *f, native_token *token, uint64_t size, uint64_t usage)
{
    doe_resource_config c = config(f);
    doe_resource_buffer_desc desc = {size, usage, false};
    doe_resource_buffer *result = NULL;
    assert(doe_resource_buffer_create(&c, &desc, native(f, token), &result) == DOE_RESOURCE_OK);
    return result;
}
static inline doe_resource_texture_limits texture_limits(void)
{
    return (doe_resource_texture_limits){8192, 8192, 2048, 256};
}
static inline doe_resource_texture_desc texture_desc(void)
{
    return (doe_resource_texture_desc){WGPUTextureDimension_2D, WGPUTextureFormat_RGBA8Unorm,
        8, 8, 1, 1, 1, WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
        WGPUTextureUsage_TextureBinding, NULL, 0};
}

#endif
