/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <math.h>
#include <stdio.h>

static doe_dx_context *open_context(void) {
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    return context;
}
static doe_dx_commands *commands(doe_dx_context *context) {
    doe_dx_commands *work = NULL;
    assert(doe_dx_commands_create(context, &work, NULL) == DOE_DX_OK);
    return work;
}
static doe_dx_texture *image(doe_dx_context *context, uint32_t usage) {
    doe_dx_texture_desc desc = {64, 2, DOE_DX_RGBA8_UNORM, usage};
    doe_dx_texture *texture = NULL;
    assert(doe_dx_texture_create(context, &desc, &texture, NULL) == DOE_DX_OK);
    return texture;
}
static doe_dx_buffer *buffer(doe_dx_context *context, uint64_t size, doe_dx_memory memory) {
    doe_dx_buffer *buffer = NULL;
    assert(doe_dx_buffer_create(context, size, memory, &buffer, NULL) == DOE_DX_OK);
    return buffer;
}
static doe_dx_timestamps *timestamps(doe_dx_context *context, uint32_t count) {
    doe_dx_timestamps *heap = NULL;
    assert(doe_dx_timestamps_create(context, count, &heap, NULL) == DOE_DX_OK);
    return heap;
}
static doe_dx_pipeline *graphics(doe_dx_context *context, bool textured) {
    unsigned char bytecode = 1;
    doe_dx_texture_binding_layout layout = {0, 0, DOE_DX_RGBA8_UNORM, DOE_DX_READ_ONLY};
    doe_dx_graphics_pipeline_desc desc = {
        &bytecode, 1, {{&bytecode, 1, NULL, 0}, textured ? &layout : NULL, textured ? 1u : 0u, NULL, 0},
        DOE_DX_RGBA8_UNORM
    };
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create_graphics(context, &desc, &pipeline, NULL) == DOE_DX_OK);
    return pipeline;
}
static doe_dx_draw_desc draw_to(doe_dx_texture *color) {
    return (doe_dx_draw_desc){color, true, {1, 0, 0, 1}, 3, 1, 0, 0};
}
static void close_context(doe_dx_context *context) {
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void graphics_construction(void) {
    doe_dx_context *context = open_context();
    unsigned char bytecode = 1;
    doe_dx_graphics_pipeline_desc desc = {&bytecode, 1, {{&bytecode, 1, NULL, 0}, NULL, 0, NULL, 0}, DOE_DX_RGBA8_UNORM};
    doe_dx_pipeline *out = NULL;
    mock.failure = MOCK_GRAPHICS;
    assert(doe_dx_pipeline_create_graphics(context, &desc, &out, NULL) == DOE_DX_NATIVE_FAILURE && !out);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_pipeline_create_graphics(context, &desc, &out, NULL) == DOE_DX_OUT_OF_MEMORY && !out);
    mock.allocation_failure = 0;
    desc.color_format = DOE_DX_R32_UINT;
    assert(doe_dx_pipeline_create_graphics(context, &desc, &out, NULL) == DOE_DX_UNSUPPORTED && !out);
    desc.color_format = DOE_DX_RGBA8_UNORM;
    desc.vertex_bytecode_size = 0;
    assert(doe_dx_pipeline_create_graphics(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT && !out);
    doe_dx_texture_desc texture_desc = {64, 2, DOE_DX_R32_FLOAT, DOE_DX_TEXTURE_RENDER_ATTACHMENT};
    doe_dx_texture *texture = NULL;
    assert(doe_dx_texture_create(context, &texture_desc, &texture, NULL) == DOE_DX_UNSUPPORTED && !texture);
    close_context(context);
}

static void draw_ownership(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *color = image(context, DOE_DX_TEXTURE_RENDER_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC);
    doe_dx_buffer *readback = buffer(context, 512, DOE_DX_READBACK);
    doe_dx_pipeline *pipeline = graphics(context, false);
    doe_dx_commands *work = commands(context);
    doe_dx_compute_resources resources = {0};
    doe_dx_draw_desc draw = draw_to(color);
    draw.clear = false;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_UNINITIALIZED);
    draw.clear = true;
    mock.failure = MOCK_DRAW;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!color->owner && !work->texture_count && !work->pipeline_count && !mock.draws);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_OK);
    assert(mock.draw_color == color->native && mock.draw.vertex_count == 3 && mock.draw.clear);
    assert(!color->initialized);
    draw.clear = false;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_read_texture(work, color, readback, (doe_dx_texture_layout){0}, NULL) == DOE_DX_OK);
    doe_dx_texture_release(color);
    doe_dx_pipeline_release(pipeline);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(work);
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.textures == 1 && mock.pipelines == 1);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_PENDING);
    mock.completed = 1;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
    assert(!mock.textures && !mock.pipelines && readback->initialized);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void draw_validation_and_bindings(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *color = image(context, 31);
    doe_dx_texture *sampled = image(context, DOE_DX_TEXTURE_COPY_DST | DOE_DX_TEXTURE_SAMPLED);
    doe_dx_buffer *upload = buffer(context, 512, DOE_DX_UPLOAD);
    doe_dx_pipeline *pipeline = graphics(context, true);
    doe_dx_commands *work = commands(context);
    doe_dx_draw_desc draw = draw_to(color);
    doe_dx_texture *binding = sampled;
    doe_dx_compute_resources resources = {NULL, 0, &binding, 1};
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 1, 1, 1, NULL) == DOE_DX_INVALID_ARGUMENT);
    draw.clear_color[0] = NAN;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_INVALID_ARGUMENT);
    draw.clear_color[0] = 1;
    draw.first_vertex = UINT32_MAX;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_RANGE);
    draw.first_vertex = 0;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_UNINITIALIZED);
    assert(!mock.draws && !color->owner);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, sampled, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, color, NULL) == DOE_DX_OK);
    binding = color;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_UNSUPPORTED);
    binding = sampled;
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_OK);
    assert(mock.texture_bindings[0] == sampled->native && mock.draw_color == color->native);
    doe_dx_commands_release(work);
    assert(!color->initialized && !sampled->initialized);
    doe_dx_pipeline_release(pipeline);
    doe_dx_texture_release(color);
    doe_dx_texture_release(sampled);
    doe_dx_buffer_release(upload);
    close_context(context);
}

static void timestamp_construction_and_frequency(void) {
    doe_dx_context *context = open_context();
    doe_dx_timestamps *out = NULL;
    assert(doe_dx_timestamps_create(context, 0, &out, NULL) == DOE_DX_INVALID_ARGUMENT && !out);
    assert(doe_dx_timestamps_create(context, 65, &out, NULL) == DOE_DX_RANGE && !out);
    mock.failure = MOCK_TIMESTAMPS;
    assert(doe_dx_timestamps_create(context, 2, &out, NULL) == DOE_DX_NATIVE_FAILURE && !out);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_timestamps_create(context, 2, &out, NULL) == DOE_DX_OUT_OF_MEMORY && !out);
    mock.allocation_failure = 0;
    uint64_t frequency = 77;
    mock.failure = MOCK_FREQUENCY;
    assert(doe_dx_timestamp_frequency(context, &frequency, NULL) == DOE_DX_NATIVE_FAILURE && frequency == 77);
    mock.failure = MOCK_NONE;
    mock.frequency = 0;
    assert(doe_dx_timestamp_frequency(context, &frequency, NULL) == DOE_DX_UNAVAILABLE && frequency == 77);
    mock.frequency = 1000;
    assert(doe_dx_timestamp_frequency(context, &frequency, NULL) == DOE_DX_OK && frequency == 1000);
    close_context(context);
}

static void timestamp_publication_and_cancel(void) {
    doe_dx_context *context = open_context();
    doe_dx_timestamps *heap = timestamps(context, 2);
    doe_dx_buffer *readback = buffer(context, 16, DOE_DX_READBACK);
    doe_dx_commands *work = commands(context), *other = commands(context);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 2, readback, 0, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_timestamp(work, heap, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_timestamp(other, heap, 1, NULL) == DOE_DX_BUSY);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 2, readback, 0, NULL) == DOE_DX_UNINITIALIZED);
    doe_dx_commands_release(work);
    assert(!heap->initialized && !heap->owner);
    assert(doe_dx_commands_timestamp(other, heap, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_timestamp(other, heap, 1, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_resolve_timestamps(other, heap, 0, 2, readback, 0, NULL) == DOE_DX_OK);
    assert(mock.timestamp_heap == heap->native && mock.resolve_buffer == readback->native);
    assert(mock.resolve_first == 0 && mock.resolve_count == 2 && mock.resolve_offset == 0);
    assert(!heap->initialized && !readback->initialized);
    assert(doe_dx_commands_submit(other, NULL) == DOE_DX_OK);
    doe_dx_commands_release(other);
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_PENDING && !heap->initialized);
    mock.completed = 1;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
    assert(heap->initialized == 3 && readback->initialized);
    doe_dx_timestamps_release(heap);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void timestamp_ranges_and_mask(void) {
    doe_dx_context *context = open_context();
    doe_dx_timestamps *heap = timestamps(context, 64);
    doe_dx_buffer *readback = buffer(context, 512, DOE_DX_READBACK);
    doe_dx_buffer *upload = buffer(context, 512, DOE_DX_UPLOAD);
    doe_dx_commands *work = commands(context);
    assert(doe_dx_commands_timestamp(work, heap, 64, NULL) == DOE_DX_RANGE);
    for (uint32_t index = 0; index < 64; index++)
        assert(doe_dx_commands_timestamp(work, heap, index, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 64, 1, readback, 0, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 0, readback, 0, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 64, readback, 4, NULL) == DOE_DX_ALIGNMENT);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 64, readback, 8, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 64, upload, 0, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 63, 1, readback, 504, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 512, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 63, 1, readback, 504, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 64, readback, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    mock.completed = 1;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK && heap->initialized == UINT64_MAX);
    doe_dx_commands_release(work);
    work = commands(context);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 1, 63, readback, 8, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    doe_dx_timestamps_release(heap);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void timestamp_unknown_and_loss(void) {
    doe_dx_context *context = open_context();
    doe_dx_timestamps *heap = timestamps(context, 1);
    doe_dx_buffer *readback = buffer(context, 8, DOE_DX_READBACK);
    doe_dx_commands *work = commands(context);
    assert(doe_dx_commands_timestamp(work, heap, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_resolve_timestamps(work, heap, 0, 1, readback, 0, NULL) == DOE_DX_OK);
    doe_dx_timestamps_release(heap);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(work);
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.timestamps == 1 && !readback->initialized);
    mock.lost = true;
    uint64_t frequency = 17;
    assert(doe_dx_timestamp_frequency(context, &frequency, NULL) == DOE_DX_DEVICE_LOST && frequency == 17);
    assert(!mock.timestamps && !mock.commands && !readback->initialized && !readback->owner);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void composed_budgets(void) {
    doe_dx_context *context = open_context();
    doe_dx_timestamps *heaps[64];
    for (size_t index = 0; index < 64; index++) heaps[index] = timestamps(context, 1);
    doe_dx_buffer *readback = buffer(context, 8, DOE_DX_READBACK);
    doe_dx_texture *color = image(context, DOE_DX_TEXTURE_RENDER_ATTACHMENT);
    doe_dx_pipeline *pipeline = graphics(context, false);
    doe_dx_commands *work = commands(context);
    heaps[0]->references = UINT32_MAX;
    assert(doe_dx_timestamps_retain(heaps[0]) == DOE_DX_RANGE);
    assert(doe_dx_commands_timestamp(work, heaps[0], 0, NULL) == DOE_DX_RANGE);
    heaps[0]->references = 1;
    for (size_t index = 0; index < 64; index++)
        assert(doe_dx_commands_timestamp(work, heaps[index], 0, NULL) == DOE_DX_OK);
    doe_dx_compute_resources resources = {0};
    doe_dx_draw_desc draw = draw_to(color);
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_resolve_timestamps(work, heaps[0], 0, 1, readback, 0, NULL) == DOE_DX_RANGE);
    assert(!color->owner && !readback->owner && !mock.draws);
    work->operation_count = 65536;
    assert(doe_dx_commands_timestamp(work, heaps[0], 0, NULL) == DOE_DX_RANGE);
    doe_dx_commands_release(work);
    for (size_t index = 0; index < 64; index++) doe_dx_timestamps_release(heaps[index]);
    doe_dx_pipeline_release(pipeline);
    doe_dx_texture_release(color);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void device_kind_and_close_failure(void) {
    doe_dx_context *context = open_context(), *foreign = open_context();
    doe_dx_texture *color = image(context, DOE_DX_TEXTURE_RENDER_ATTACHMENT);
    doe_dx_texture *foreign_color = image(foreign, DOE_DX_TEXTURE_RENDER_ATTACHMENT);
    doe_dx_timestamps *heap = timestamps(context, 1), *foreign_heap = timestamps(foreign, 1);
    doe_dx_pipeline *pipeline = graphics(context, false), *foreign_pipeline = graphics(foreign, false);
    doe_dx_commands *work = commands(context);
    doe_dx_compute_resources resources = {0};
    doe_dx_draw_desc draw = draw_to(foreign_color);
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_DEVICE_MISMATCH);
    draw.color = color;
    assert(doe_dx_commands_draw(work, foreign_pipeline, &resources, &draw, NULL) == DOE_DX_DEVICE_MISMATCH);
    assert(doe_dx_commands_timestamp(work, foreign_heap, 0, NULL) == DOE_DX_DEVICE_MISMATCH);
    unsigned char bytecode = 1;
    doe_dx_pipeline_desc desc = {&bytecode, 1, NULL, 0};
    doe_dx_pipeline *compute = NULL;
    assert(doe_dx_pipeline_create(context, &desc, &compute, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_draw(work, compute, &resources, &draw, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(!mock.draws && !mock.timestamp_writes && !color->owner);
    assert(doe_dx_commands_draw(work, pipeline, &resources, &draw, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_timestamp(work, heap, 0, NULL) == DOE_DX_OK);
    mock.failure = MOCK_FINISH;
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!mock.executes && !color->initialized && !heap->initialized);
    doe_dx_commands_release(work);
    assert(!color->owner && !heap->owner && !color->initialized && !heap->initialized);
    doe_dx_pipeline_release(compute);
    doe_dx_pipeline_release(pipeline);
    doe_dx_pipeline_release(foreign_pipeline);
    doe_dx_texture_release(color);
    doe_dx_texture_release(foreign_color);
    doe_dx_timestamps_release(heap);
    doe_dx_timestamps_release(foreign_heap);
    assert(doe_dx_context_destroy(foreign) == DOE_DX_OK);
    close_context(context);
}

int main(void) {
    mock_reset();
    graphics_construction();
    draw_ownership();
    draw_validation_and_bindings();
    timestamp_construction_and_frequency();
    timestamp_publication_and_cancel();
    timestamp_ranges_and_mask();
    timestamp_unknown_and_loss();
    composed_budgets();
    device_kind_and_close_failure();
    puts("D3D12 C graphics/timestamps: 9 groups passed (no SDK or GPU execution)");
    return 0;
}
