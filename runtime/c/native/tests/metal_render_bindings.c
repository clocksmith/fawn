/* SPDX-License-Identifier: MIT
 * Host-only immutable span ownership/control: no GPU execution is emulated. */
#include "../src/metal_render_bindings.h"
#include "../src/metal_graphics_internal.h"
#include "../../resources/tests/fixture.h"

static size_t cache_allocations, fail_at = SIZE_MAX;
void *metal_test_calloc(size_t count, size_t size) {
    return cache_allocations++ == fail_at ? NULL : calloc(count, size);
}
void *metal_test_realloc(void *memory, size_t size) {
    return cache_allocations++ == fail_at ? NULL : realloc(memory, size);
}
static void borrowed_release(void *userdata, void *handle) { (void)userdata; (void)handle; }
static doe_resource_render_pipeline *pipeline_new(fixture *f, doe_resource_pipeline_layout *layout, native_pipeline *handle) {
    doe_resource_config c = config(f);
    doe_resource_render_pipeline_desc desc = {.samples=1,.color_format=WGPUTextureFormat_RGBA8Unorm,
        .vertex_count=handle->vertex_count,.vertices={{16,4,WGPUVertexStepMode_Vertex}}};
    doe_resource_render_pipeline *pipeline = NULL;
    assert(doe_resource_render_pipeline_create(&c, layout, &desc,
        (doe_resource_native){handle,NULL,borrowed_release}, &pipeline) == DOE_RESOURCE_OK);
    return pipeline;
}
static void dynamic_group_keys(void) {
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout_entry entry = {.binding=3,.visibility=WGPUShaderStage_Vertex,
        .kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform,.dynamic_offset=true,.minimum_size=4};
    doe_resource_layout *group_layout = NULL;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &group_layout) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *layout = NULL;
    assert(doe_resource_pipeline_layout_create(&c, &group_layout, 1, 0, &layout) == DOE_RESOURCE_OK);
    native_metal_graphics graphics = {0};
    graphics.stages[0].tables[0] = DOE_COMPILER_TEXT_TABLE_BUFFER;
    graphics.stages[0].slots[0] = 7;
    native_pipeline handle = {.metal_graphics=&graphics,.binding_count=1,
        .bindings={{.group=0,.binding=3,.kind=DOE_BIND_BUFFER,.buffer_type=WGPUBufferBindingType_Uniform}}};
    doe_resource_render_pipeline *pipeline = pipeline_new(&f, layout, &handle);
    native_buffer native = {0};
    doe_resource_buffer *buffer = NULL;
    doe_resource_buffer_desc desc = {256,WGPUBufferUsage_Uniform,false};
    assert(doe_resource_buffer_create(&c, &desc, (doe_resource_native){&native,NULL,borrowed_release}, &buffer) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.binding=3,.kind=DOE_BIND_BUFFER,.resource.buffer={buffer,0,64}};
    doe_resource_binding_limits limits = {16,16,4096,4096};
    doe_resource_bind_group *groups[2] = {0};
    for (size_t i = 0; i < 2; ++i)
        assert(doe_resource_bind_group_create(group_layout, &binding, 1, &limits, &groups[i]) == DOE_RESOURCE_OK);
    doe_resource_render_packet packets[3] = {{.pipeline=pipeline,.groups={groups[0]},.offsets={.counts={1}}}};
    packets[1] = packets[0]; packets[1].offsets.values[0][0] = 16;
    packets[2] = packets[0]; packets[2].groups[0] = groups[1];
    native_metal_render_binding_cache *cache = NULL;
    for (size_t i = 0; i < 3; ++i) assert(native_metal_render_bindings_intern(&cache, &packets[i]) == NATIVE_OK);
    assert(native_metal_render_bindings_stats(cache).states == 3);
    assert(native_metal_render_bindings_prepare(cache, NULL).status == NATIVE_OK);
    for (size_t i = 0; i < 3; ++i) {
        const native_metal_render_binding_view *view = native_metal_render_bindings_find(cache, &packets[i]);
        assert(view && view->vertex.buffer_count == 1 && view->vertex.buffers[0].slot == 7);
        assert(view->vertex.buffers[0].offset == (i == 1 ? 16u : 0u));
        assert(view->vertex.buffers[0].size == 64);
        if (i) assert(view != native_metal_render_bindings_find(cache, &packets[0]));
    }
    native_metal_render_bindings_release(cache);
    doe_resource_bind_group_release(groups[1]); doe_resource_bind_group_release(groups[0]);
    doe_resource_buffer_release(buffer); doe_resource_render_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout); doe_resource_layout_release(group_layout);
    empty(&f);
}
int main(void) {
    dynamic_group_keys();
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_pipeline_layout *layout = NULL;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    native_metal_graphics graphics = {0}, empty_graphics = {0};
    for (size_t i = 0; i < DOE_RESOURCE_MAX_VERTEX_BUFFERS; ++i) {
        graphics.vertex_slots[i] = UINT32_MAX;
        empty_graphics.vertex_slots[i] = UINT32_MAX;
    }
    graphics.vertex_slots[0] = 3;
    native_pipeline handle = {.vertex_count=1,.metal_graphics=&graphics};
    native_pipeline empty_handle = {.metal_graphics=&empty_graphics};
    doe_resource_render_pipeline *pipeline = pipeline_new(&f, layout, &handle);
    doe_resource_render_pipeline *empty_pipeline = pipeline_new(&f, layout, &empty_handle);
    native_buffer native = {0}, other_native = {0};
    doe_resource_buffer *buffer = NULL, *other = NULL;
    doe_resource_buffer_desc desc = {4096,WGPUBufferUsage_Vertex,false};
    assert(doe_resource_buffer_create(&c, &desc, (doe_resource_native){&native,NULL,borrowed_release}, &buffer) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_create(&c, &desc, (doe_resource_native){&other_native,NULL,borrowed_release}, &other) == DOE_RESOURCE_OK);
    doe_resource_render_packet packet = {.pipeline=pipeline,.vertices={{buffer,0,64}}};
    native_metal_render_binding_cache *cache = NULL;
    fail_at = cache_allocations;
    assert(native_metal_render_bindings_intern(&cache, &packet) == NATIVE_OUT_OF_MEMORY && !cache);
    fail_at = SIZE_MAX;
    assert(native_metal_render_bindings_intern(&cache, &packet) == NATIVE_OK);
    size_t allocations = cache_allocations;
    for (uint32_t i = 0; i < 200000; ++i) {
        packet.count = i; packet.first = i; packet.immediates.words[0] = i;
        packet.offsets.values[0][31] = i; /* Inactive padding is not part of the binding state. */
        assert(native_metal_render_bindings_intern(&cache, &packet) == NATIVE_OK);
    }
    native_metal_render_binding_stats stats = native_metal_render_bindings_stats(cache);
    assert(stats.states == 1 && stats.buffers == 1 && !stats.textures && !stats.samplers);
    assert(cache_allocations == allocations); /* Neither keys nor occupied spans grow with repeated draws. */
    doe_resource_render_packet changed = packet;
    changed.vertices[0].offset = 16; changed.vertices[0].size = 48;
    assert(native_metal_render_bindings_intern(&cache, &changed) == NATIVE_OK);
    doe_resource_render_packet replaced = packet;
    replaced.vertices[0].buffer = other;
    assert(native_metal_render_bindings_intern(&cache, &replaced) == NATIVE_OK);
    doe_resource_render_packet removed = {.pipeline=empty_pipeline};
    assert(native_metal_render_bindings_intern(&cache, &removed) == NATIVE_OK);
    /* Force bucket collisions/growth with distinct aligned ranges. */
    for (uint32_t i = 1; i <= 64; ++i) {
        doe_resource_render_packet range = packet;
        range.vertices[0].offset = (uint64_t)i * 32;
        assert(native_metal_render_bindings_intern(&cache, &range) == NATIVE_OK);
    }
    stats = native_metal_render_bindings_stats(cache);
    assert(stats.states == 68 && stats.buffers == 67);
    assert(native_metal_render_bindings_prepare(cache, NULL).status == NATIVE_OK);
    const native_metal_render_binding_view *view = native_metal_render_bindings_find(cache, &packet);
    assert(view && view->vertex.buffer_count == 1 && view->vertex.buffers[0].slot == 3);
    assert(view->vertex.buffers[0].offset == 0 && view->vertex.buffers[0].size == 64);
    allocations = cache_allocations;
    for (uint32_t i = 0; i < 200000; ++i) {
        packet.count = i;
        const native_metal_render_binding_view *repeat = native_metal_render_bindings_find(cache, &packet);
        assert(repeat == view && repeat->vertex.buffers == view->vertex.buffers);
    }
    assert(cache_allocations == allocations);
    const native_metal_render_binding_view *different = native_metal_render_bindings_find(cache, &changed);
    assert(different != view && different->vertex.buffers != view->vertex.buffers);
    assert(different->vertex.buffers[0].offset == 16 && different->vertex.buffers[0].size == 48);
    assert(native_metal_render_bindings_find(cache, &replaced) != view);
    assert(!native_metal_render_bindings_find(cache, &removed)->vertex.buffer_count);
    assert(native_metal_render_bindings_intern(&cache, &packet) == NATIVE_INVALID_STATE);
    native_metal_render_bindings_release(cache);
    doe_resource_buffer_release(other); doe_resource_buffer_release(buffer);
    doe_resource_render_pipeline_release(empty_pipeline); doe_resource_render_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    empty(&f);
    puts("PASS Metal distinct binding states: 200000 repeated draws add zero allocations, shared immutable spans, ranges/identity/removal and hash growth");
}
