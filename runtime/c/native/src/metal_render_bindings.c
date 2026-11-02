/* SPDX-License-Identifier: MIT */
#include "metal_render_bindings.h"
#include "metal_graphics_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct binding_counts { size_t buffers, textures, samplers; } binding_counts;
typedef struct binding_state {
    doe_resource_render_pipeline *pipeline;
    doe_resource_bind_group *groups[DOE_RESOURCE_MAX_GROUPS];
    doe_resource_dispatch_offsets offsets;
    doe_resource_vertex_binding vertices[DOE_RESOURCE_MAX_VERTEX_BUFFERS];
    binding_counts counts[2];
    native_metal_render_binding_view view;
} binding_state;
typedef struct binding_bucket { uint64_t hash; size_t index; } binding_bucket;
struct native_metal_render_binding_cache {
    binding_state *states;
    binding_bucket *buckets;
    size_t count, capacity, bucket_capacity;
    binding_counts totals;
    bool prepared;
    doe_mtl_buffer_binding *buffers;
    doe_mtl_texture_binding *textures;
    doe_mtl_sampler_binding *samplers;
};
static uint64_t mix(uint64_t hash, uint64_t value) { return (hash ^ value) * UINT64_C(1099511628211); }
static uint64_t hash_packet(const doe_resource_render_packet *packet) {
    uint64_t hash = mix(UINT64_C(14695981039346656037), (uintptr_t)packet->pipeline);
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; ++i) {
        hash = mix(hash, (uintptr_t)packet->groups[i]);
        hash = mix(hash, packet->offsets.counts[i]);
        for (size_t j = 0; j < packet->offsets.counts[i]; ++j) hash = mix(hash, packet->offsets.values[i][j]);
    }
    for (size_t i = 0; i < DOE_RESOURCE_MAX_VERTEX_BUFFERS; ++i) {
        hash = mix(hash, (uintptr_t)packet->vertices[i].buffer);
        hash = mix(mix(hash, packet->vertices[i].offset), packet->vertices[i].size);
    }
    return hash;
}
static bool same_state(const binding_state *state, const doe_resource_render_packet *packet) {
    if (state->pipeline != packet->pipeline) return false;
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; ++i) {
        if (state->groups[i] != packet->groups[i] || state->offsets.counts[i] != packet->offsets.counts[i]) return false;
        for (size_t j = 0; j < state->offsets.counts[i]; ++j)
            if (state->offsets.values[i][j] != packet->offsets.values[i][j]) return false;
    }
    for (size_t i = 0; i < DOE_RESOURCE_MAX_VERTEX_BUFFERS; ++i)
        if (state->vertices[i].buffer != packet->vertices[i].buffer || state->vertices[i].offset != packet->vertices[i].offset ||
            state->vertices[i].size != packet->vertices[i].size) return false;
    return true;
}
static size_t bucket_for(const native_metal_render_binding_cache *cache, uint64_t hash,
    const doe_resource_render_packet *packet) {
    size_t at = (size_t)hash & (cache->bucket_capacity - 1);
    while (cache->buckets[at].index) {
        if (cache->buckets[at].hash == hash && same_state(&cache->states[cache->buckets[at].index - 1], packet)) break;
        at = (at + 1) & (cache->bucket_capacity - 1);
    }
    return at;
}
static bool stage_counts(const native_pipeline *pipeline, size_t stage, binding_counts *counts) {
    if (!pipeline || !pipeline->metal_graphics) return false;
    *counts = (binding_counts){0};
    const native_metal_graphics *graphics = pipeline->metal_graphics;
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        uint32_t table = graphics->stages[stage].tables[i];
        if (table == DOE_COMPILER_TEXT_TABLE_BUFFER) ++counts->buffers;
        else if (table == DOE_COMPILER_TEXT_TABLE_TEXTURE) ++counts->textures;
        else if (table == DOE_COMPILER_TEXT_TABLE_SAMPLER) ++counts->samplers;
        else if (table) return false;
    }
    if (!stage) for (size_t i = 0; i < pipeline->vertex_count; ++i)
        if (graphics->vertex_slots[i] != UINT32_MAX) ++counts->buffers;
    return counts->buffers <= DOE_MTL_BUFFER_SLOTS && counts->textures <= DOE_MTL_TEXTURE_SLOTS &&
        counts->samplers <= DOE_MTL_SAMPLER_SLOTS;
}
static native_result reserve(native_metal_render_binding_cache *cache) {
    if (cache->count == cache->capacity) {
        size_t next = cache->capacity ? cache->capacity * 2 : 8;
        if (next < cache->capacity || next > SIZE_MAX / sizeof(*cache->states)) return NATIVE_OUT_OF_MEMORY;
        binding_state *states = realloc(cache->states, next * sizeof(*states));
        if (!states) return NATIVE_OUT_OF_MEMORY;
        cache->states = states; cache->capacity = next;
    }
    if (!cache->bucket_capacity || cache->count + 1 > cache->bucket_capacity / 2) {
        size_t next = cache->bucket_capacity ? cache->bucket_capacity * 2 : 16;
        if (next < cache->bucket_capacity || next > SIZE_MAX / sizeof(*cache->buckets)) return NATIVE_OUT_OF_MEMORY;
        binding_bucket *buckets = calloc(next, sizeof(*buckets));
        if (!buckets) return NATIVE_OUT_OF_MEMORY;
        for (size_t i = 0; i < cache->bucket_capacity; ++i) if (cache->buckets[i].index) {
            size_t at = (size_t)cache->buckets[i].hash & (next - 1);
            while (buckets[at].index) at = (at + 1) & (next - 1);
            buckets[at] = cache->buckets[i];
        }
        free(cache->buckets); cache->buckets = buckets; cache->bucket_capacity = next;
    }
    return NATIVE_OK;
}
native_result native_metal_render_bindings_intern(native_metal_render_binding_cache **out,
    const doe_resource_render_packet *packet) {
    if (!out || !packet || !packet->pipeline) return NATIVE_INVALID_ARGUMENT;
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; ++i)
        if (packet->offsets.counts[i] > DOE_RESOURCE_MAX_DYNAMIC_OFFSETS) return NATIVE_INVALID_ARGUMENT;
    native_metal_render_binding_cache *cache = *out;
    if (!cache) { cache = calloc(1, sizeof(*cache)); if (!cache) return NATIVE_OUT_OF_MEMORY; *out = cache; }
    if (cache->prepared) return NATIVE_INVALID_STATE;
    uint64_t hash = hash_packet(packet);
    if (cache->bucket_capacity && cache->buckets[bucket_for(cache, hash, packet)].index) return NATIVE_OK;
    native_pipeline *pipeline = doe_resource_render_pipeline_native(packet->pipeline);
    binding_counts counts[2], totals = cache->totals;
    for (size_t stage = 0; stage < 2; ++stage) {
        if (!stage_counts(pipeline, stage, &counts[stage])) return NATIVE_UNSUPPORTED;
        if (counts[stage].buffers > SIZE_MAX - totals.buffers || counts[stage].textures > SIZE_MAX - totals.textures ||
            counts[stage].samplers > SIZE_MAX - totals.samplers) return NATIVE_OUT_OF_MEMORY;
        totals.buffers += counts[stage].buffers; totals.textures += counts[stage].textures; totals.samplers += counts[stage].samplers;
    }
    native_result status = reserve(cache);
    if (status != NATIVE_OK) return status;
    binding_state *state = &cache->states[cache->count];
    *state = (binding_state){.pipeline=packet->pipeline,.offsets=packet->offsets,.counts={counts[0],counts[1]}};
    memcpy(state->groups, packet->groups, sizeof(state->groups));
    memcpy(state->vertices, packet->vertices, sizeof(state->vertices));
    cache->buckets[bucket_for(cache, hash, packet)] = (binding_bucket){hash,++cache->count};
    cache->totals = totals;
    return NATIVE_OK;
}
native_failure native_metal_render_bindings_prepare(native_metal_render_binding_cache *cache, doe_mtl_commands *commands) {
    if (!cache) return native_failure_status(NATIVE_OK);
    if (cache->prepared) return native_failure_status(NATIVE_INVALID_STATE);
    cache->prepared = true;
    if (cache->totals.buffers > SIZE_MAX / sizeof(*cache->buffers) || cache->totals.textures > SIZE_MAX / sizeof(*cache->textures) ||
        cache->totals.samplers > SIZE_MAX / sizeof(*cache->samplers)) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    cache->buffers = cache->totals.buffers ? calloc(cache->totals.buffers, sizeof(*cache->buffers)) : NULL;
    cache->textures = cache->totals.textures ? calloc(cache->totals.textures, sizeof(*cache->textures)) : NULL;
    cache->samplers = cache->totals.samplers ? calloc(cache->totals.samplers, sizeof(*cache->samplers)) : NULL;
    if ((cache->totals.buffers && !cache->buffers) || (cache->totals.textures && !cache->textures) ||
        (cache->totals.samplers && !cache->samplers)) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    binding_counts at = {0};
    for (size_t i = 0; i < cache->count; ++i) {
        binding_state *state = &cache->states[i];
        native_pipeline *pipeline = doe_resource_render_pipeline_native(state->pipeline);
        doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
        if (native_resolve_resources(pipeline, doe_resource_render_pipeline_get_layout(state->pipeline),
            state->groups, &state->offsets, resolved) != DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        for (size_t stage = 0; stage < 2; ++stage) {
            binding_counts counts = state->counts[stage];
            native_metal_binding_storage storage = {.buffers=cache->buffers ? cache->buffers + at.buffers : NULL,
                .textures=cache->textures ? cache->textures + at.textures : NULL,.samplers=cache->samplers ? cache->samplers + at.samplers : NULL,
                .buffer_capacity=counts.buffers,.texture_capacity=counts.textures,.sampler_capacity=counts.samplers};
            native_failure failure = native_metal_binding_snapshot_into(commands, pipeline, &pipeline->metal_graphics->stages[stage], resolved, &storage);
            if (failure.status != NATIVE_OK) return failure;
            if (!stage) for (size_t vertex = 0; vertex < pipeline->vertex_count; ++vertex) {
                uint32_t slot = pipeline->metal_graphics->vertex_slots[vertex];
                if (slot == UINT32_MAX) continue;
                native_buffer *buffer = doe_resource_buffer_native(state->vertices[vertex].buffer);
                if (!buffer || storage.bindings.buffer_count >= storage.buffer_capacity) return native_failure_status(NATIVE_INVALID_ARGUMENT);
                storage.buffers[storage.bindings.buffer_count++] = (doe_mtl_buffer_binding){slot,buffer->handle.metal,
                    state->vertices[vertex].offset,state->vertices[vertex].size};
            }
            if (stage) state->view.fragment = storage.bindings; else state->view.vertex = storage.bindings;
            at.buffers += counts.buffers; at.textures += counts.textures; at.samplers += counts.samplers;
        }
    }
    return native_failure_status(NATIVE_OK);
}
const native_metal_render_binding_view *native_metal_render_bindings_find(
    const native_metal_render_binding_cache *cache, const doe_resource_render_packet *packet) {
    if (!cache || !cache->bucket_capacity || !packet) return NULL;
    const binding_bucket *bucket = &cache->buckets[bucket_for(cache, hash_packet(packet), packet)];
    return bucket->index ? &cache->states[bucket->index - 1].view : NULL;
}
native_metal_render_binding_stats native_metal_render_bindings_stats(const native_metal_render_binding_cache *cache) {
    return cache ? (native_metal_render_binding_stats){cache->count,cache->totals.buffers,cache->totals.textures,cache->totals.samplers} :
        (native_metal_render_binding_stats){0};
}
void native_metal_render_bindings_release(native_metal_render_binding_cache *cache) {
    if (!cache) return;
    free(cache->samplers); free(cache->textures); free(cache->buffers); free(cache->buckets); free(cache->states); free(cache);
}
