/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <math.h>
#include <stdio.h>

typedef struct mesh_fixture {
    doe_dx_context *context;
    doe_dx_commands *work;
    doe_dx_pipeline *pipeline;
    doe_dx_texture *color, *depth;
    doe_dx_buffer *vertices, *instances, *indices;
    doe_dx_buffer_binding bindings[2];
    doe_dx_index_binding index;
    doe_dx_mesh_draw_desc draw;
} mesh_fixture;

static doe_dx_mesh_pipeline_desc description(doe_dx_vertex_layout *layouts, doe_dx_vertex_attribute *attributes) {
    static const unsigned char bytecode = 1;
    doe_dx_mesh_pipeline_desc desc = {0};
    desc.graphics = (doe_dx_graphics_pipeline_desc){&bytecode, 1,
        {{&bytecode, 1, NULL, 0}, NULL, 0, NULL, 0}, DOE_DX_BGRA8_UNORM};
    layouts[0] = (doe_dx_vertex_layout){12, DOE_DX_PER_VERTEX};
    layouts[1] = (doe_dx_vertex_layout){16, DOE_DX_PER_INSTANCE};
    attributes[0] = (doe_dx_vertex_attribute){0, 0, 0, DOE_DX_FLOAT32X3};
    attributes[1] = (doe_dx_vertex_attribute){1, 1, 0, DOE_DX_FLOAT32X4};
    desc.vertex_buffers = layouts;
    desc.vertex_buffer_count = 2;
    desc.attributes = attributes;
    desc.attribute_count = 2;
    desc.depth_enabled = true;
    desc.depth_write = true;
    desc.depth_compare = DOE_DX_LESS;
    return desc;
}

static void open_fixture(mesh_fixture *fixture) {
    assert(doe_dx_context_create(NULL, &fixture->context, NULL) == DOE_DX_OK);
    doe_dx_vertex_layout layouts[2];
    doe_dx_vertex_attribute attributes[2];
    doe_dx_mesh_pipeline_desc desc = description(layouts, attributes);
    assert(doe_dx_pipeline_create_mesh(fixture->context, &desc, &fixture->pipeline, NULL) == DOE_DX_OK);
    /* Construction copies layout requirements; caller descriptors may disappear. */
    layouts[0].stride = 2048;
    assert(fixture->pipeline->vertex_buffers[0].stride == 12);
    doe_dx_texture_desc color = {64, 2, DOE_DX_BGRA8_UNORM, DOE_DX_TEXTURE_RENDER_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC};
    doe_dx_texture_desc depth = {64, 2, DOE_DX_D32_FLOAT, DOE_DX_TEXTURE_DEPTH_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC};
    assert(doe_dx_texture_create(fixture->context, &color, &fixture->color, NULL) == DOE_DX_OK);
    assert(doe_dx_texture_create(fixture->context, &depth, &fixture->depth, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(fixture->context, 72, DOE_DX_DEFAULT, &fixture->vertices, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(fixture->context, 32, DOE_DX_DEFAULT, &fixture->instances, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(fixture->context, 12, DOE_DX_DEFAULT, &fixture->indices, NULL) == DOE_DX_OK);
    doe_dx_buffer *upload = NULL;
    doe_dx_commands *init = NULL;
    assert(doe_dx_buffer_create(fixture->context, 72, DOE_DX_UPLOAD, &upload, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(fixture->context, &init, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(init, upload, 0, fixture->vertices, 0, 72, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(init, upload, 0, fixture->instances, 0, 32, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(init, upload, 0, fixture->indices, 0, 12, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(init, NULL) == DOE_DX_OK);
    doe_dx_commands_release(init);
    doe_dx_buffer_release(upload);
    mock.completed = mock.last_signal;
    assert(doe_dx_context_poll(fixture->context, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(fixture->context, &fixture->work, NULL) == DOE_DX_OK);
    fixture->bindings[0] = (doe_dx_buffer_binding){fixture->vertices, 0, 72};
    fixture->bindings[1] = (doe_dx_buffer_binding){fixture->instances, 0, 32};
    fixture->index = (doe_dx_index_binding){{fixture->indices, 0, 12}, DOE_DX_INDEX_UINT16, 0, 3};
    fixture->draw = (doe_dx_mesh_draw_desc){{fixture->color, true, {1, 1, 1, 1}, 3, 1, 0, 0},
        fixture->bindings, 2, NULL, fixture->depth, true, 1};
}

static doe_dx_status draw(mesh_fixture *fixture) {
    doe_dx_compute_resources resources = {0};
    return doe_dx_commands_draw_mesh(fixture->work, fixture->pipeline, &resources, &fixture->draw, NULL);
}
static void release_objects(mesh_fixture *fixture) {
    doe_dx_pipeline_release(fixture->pipeline);
    doe_dx_texture_release(fixture->color);
    doe_dx_texture_release(fixture->depth);
    doe_dx_buffer_release(fixture->vertices);
    doe_dx_buffer_release(fixture->instances);
    doe_dx_buffer_release(fixture->indices);
}
static void close_fixture(mesh_fixture *fixture) {
    doe_dx_commands_release(fixture->work);
    release_objects(fixture);
    assert(doe_dx_context_destroy(fixture->context) == DOE_DX_OK);
    mock_reset();
}

static void layout_validation(void) {
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    doe_dx_vertex_layout layouts[2];
    doe_dx_vertex_attribute attributes[2];
    doe_dx_mesh_pipeline_desc desc = description(layouts, attributes);
    doe_dx_pipeline *out = NULL;
    desc.vertex_buffer_count = 9;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_RANGE && !out);
    desc.vertex_buffer_count = 2;
    desc.attribute_count = 17;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_RANGE && !out);
    desc.attribute_count = 2;
    layouts[0].stride = 10;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    layouts[0].stride = 12;
    attributes[1].shader_location = 0;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    attributes[1].shader_location = 1;
    attributes[1].buffer_slot = 2;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    attributes[1].buffer_slot = 1;
    attributes[1].offset = 4;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_RANGE);
    attributes[1].offset = 0;
    attributes[1].format = (doe_dx_vertex_format)99;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    attributes[1].format = DOE_DX_FLOAT32X4;
    desc.attribute_count = 1;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    desc.attribute_count = 2;
    desc.depth_compare = (doe_dx_compare)99;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    desc.depth_compare = DOE_DX_LESS;
    desc.depth_enabled = false;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT);
    desc.depth_enabled = true;
    mock.failure = MOCK_GRAPHICS;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_NATIVE_FAILURE && !out);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_pipeline_create_mesh(context, &desc, &out, NULL) == DOE_DX_OUT_OF_MEMORY && !out);
    mock.allocation_failure = 0;
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void direct_spans(void) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    fixture.draw.draw.first_vertex = 4;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.draw.draw.first_vertex = 3;
    fixture.draw.draw.first_instance = 2;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.draw.draw.first_instance = 1;
    fixture.bindings[0].offset = 2;
    fixture.bindings[0].size = 70;
    assert(draw(&fixture) == DOE_DX_ALIGNMENT);
    fixture.bindings[0].offset = 4;
    fixture.bindings[0].size = 72;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.bindings[0].offset = 0;
    fixture.bindings[0].size = 71;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.bindings[0].size = 72;
    assert(!fixture.color->owner && !fixture.depth->owner && !mock.draws);
    assert(draw(&fixture) == DOE_DX_OK);
    assert(mock.vertex_count == 2 && !mock.indexed && mock.geometry.depth == fixture.depth->native);
    assert(mock.geometry.vertices[0].size == 72 && mock.draw.first_vertex == 3 && mock.draw.first_instance == 1);
    close_fixture(&fixture);
}

static void index_spans(void) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    fixture.draw.indices = &fixture.index;
    assert(draw(&fixture) == DOE_DX_INVALID_ARGUMENT);
    fixture.draw.draw.vertex_count = 0;
    fixture.index.first = 4;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.index.first = 3;
    fixture.index.format = DOE_DX_INDEX_UINT32;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.index.first = 0;
    fixture.index.binding.offset = 2;
    fixture.index.binding.size = 8;
    assert(draw(&fixture) == DOE_DX_ALIGNMENT);
    fixture.index.binding.offset = 0;
    fixture.index.binding.size = 12;
    fixture.index.format = (doe_dx_index_format)99;
    assert(draw(&fixture) == DOE_DX_INVALID_ARGUMENT);
    fixture.index.format = DOE_DX_INDEX_UINT32;
    assert(draw(&fixture) == DOE_DX_OK);
    assert(mock.indexed && mock.indices.format == DOE_DX_INDEX_UINT32 && mock.indices.count == 3);
    fixture.index.format = DOE_DX_INDEX_UINT16;
    fixture.index.first = 3;
    assert(draw(&fixture) == DOE_DX_OK);
    assert(mock.indices.first == 3 && mock.geometry.indices.size == 12);
    close_fixture(&fixture);
}

static void geometry_alias_and_limits(void) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    fixture.bindings[1].buffer = fixture.vertices;
    assert(draw(&fixture) == DOE_DX_UNSUPPORTED);
    fixture.bindings[1].buffer = fixture.instances;
    fixture.draw.indices = &fixture.index;
    fixture.draw.draw.vertex_count = 0;
    fixture.index.binding.buffer = fixture.vertices;
    assert(draw(&fixture) == DOE_DX_UNSUPPORTED);
    fixture.index.binding.buffer = fixture.indices;
    fixture.draw.vertex_count = 1;
    assert(draw(&fixture) == DOE_DX_INVALID_ARGUMENT);
    fixture.draw.vertex_count = 2;
    fixture.draw.draw.first_instance = UINT32_MAX;
    fixture.draw.draw.instance_count = 2;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.draw.draw.first_instance = 0;
    fixture.draw.draw.instance_count = 1;
    fixture.vertices->references = UINT32_MAX;
    assert(draw(&fixture) == DOE_DX_RANGE);
    fixture.vertices->references = 1;
    assert(!fixture.work->buffer_count && !mock.draws);
    close_fixture(&fixture);
}

static void depth_validation_and_rollback(void) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    fixture.draw.clear_depth = false;
    assert(draw(&fixture) == DOE_DX_UNINITIALIZED);
    fixture.draw.clear_depth = true;
    fixture.draw.depth_clear = NAN;
    assert(draw(&fixture) == DOE_DX_INVALID_ARGUMENT);
    fixture.draw.depth_clear = 1;
    fixture.draw.depth = NULL;
    assert(draw(&fixture) == DOE_DX_INVALID_ARGUMENT);
    fixture.draw.depth = fixture.color;
    assert(draw(&fixture) == DOE_DX_INVALID_ARGUMENT);
    fixture.draw.depth = fixture.depth;
    mock.failure = MOCK_DRAW;
    assert(draw(&fixture) == DOE_DX_NATIVE_FAILURE);
    assert(!fixture.depth->owner && !fixture.color->owner && !fixture.vertices->owner && !mock.draws);
    mock.failure = MOCK_NONE;
    assert(draw(&fixture) == DOE_DX_OK);
    fixture.draw.clear_depth = false;
    fixture.draw.draw.clear = false;
    assert(draw(&fixture) == DOE_DX_OK);
    assert(!fixture.depth->initialized && !fixture.color->initialized);
    doe_dx_commands_release(fixture.work);
    assert(!fixture.depth->initialized && !fixture.depth->owner);
    assert(doe_dx_commands_create(fixture.context, &fixture.work, NULL) == DOE_DX_OK);
    assert(draw(&fixture) == DOE_DX_UNINITIALIZED);
    close_fixture(&fixture);
}

static void depth_usage_and_copy(void) {
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    doe_dx_texture_desc desc = {64, 2, DOE_DX_D32_FLOAT, DOE_DX_TEXTURE_COPY_SRC};
    doe_dx_texture *depth = NULL;
    assert(doe_dx_texture_create(context, &desc, &depth, NULL) == DOE_DX_UNSUPPORTED && !depth);
    desc.usage |= DOE_DX_TEXTURE_DEPTH_ATTACHMENT | DOE_DX_TEXTURE_SAMPLED;
    assert(doe_dx_texture_create(context, &desc, &depth, NULL) == DOE_DX_UNSUPPORTED && !depth);
    desc.usage = DOE_DX_TEXTURE_DEPTH_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC | DOE_DX_TEXTURE_COPY_DST;
    assert(doe_dx_texture_create(context, &desc, &depth, NULL) == DOE_DX_OK);
    doe_dx_texture_footprint footprint = {0};
    assert(doe_dx_texture_copy_footprint(depth, (doe_dx_texture_layout){0}, &footprint) == DOE_DX_OK);
    assert(footprint.span == 512 && footprint.row_bytes == 256);
    doe_dx_commands *work = NULL;
    doe_dx_buffer *upload = NULL, *readback = NULL;
    assert(doe_dx_commands_create(context, &work, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context, 512, DOE_DX_UPLOAD, &upload, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context, 512, DOE_DX_READBACK, &readback, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, depth, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_read_texture(work, depth, readback, (doe_dx_texture_layout){0}, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    assert(!depth->initialized && !readback->initialized);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    doe_dx_texture_release(depth);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void completion_publication_and_exclusivity(void) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    assert(draw(&fixture) == DOE_DX_OK);
    assert(doe_dx_commands_submit(fixture.work, NULL) == DOE_DX_OK);
    doe_dx_commands_release(fixture.work);
    assert(doe_dx_commands_create(fixture.context, &fixture.work, NULL) == DOE_DX_OK);
    fixture.draw.clear_depth = false;
    fixture.draw.draw.clear = false;
    assert(draw(&fixture) == DOE_DX_BUSY);
    assert(!fixture.color->initialized && !fixture.depth->initialized);
    mock.completed = mock.last_signal;
    assert(doe_dx_context_poll(fixture.context, 0, NULL) == DOE_DX_OK);
    assert(fixture.color->initialized && fixture.depth->initialized);
    assert(draw(&fixture) == DOE_DX_OK);
    close_fixture(&fixture);
}

static void composed_budget_and_shader_alias(void) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    doe_dx_vertex_layout layouts[2];
    doe_dx_vertex_attribute attributes[2];
    doe_dx_mesh_pipeline_desc desc = description(layouts, attributes);
    doe_dx_binding_layout binding_layout = {0, 0, 12, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED};
    desc.graphics.fragment.buffers.bindings = &binding_layout;
    desc.graphics.fragment.buffers.binding_count = 1;
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create_mesh(fixture.context, &desc, &pipeline, NULL) == DOE_DX_OK);
    doe_dx_compute_resources resources = {fixture.bindings, 1, NULL, 0};
    assert(doe_dx_commands_draw_mesh(fixture.work, pipeline, &resources, &fixture.draw, NULL) == DOE_DX_UNSUPPORTED);
    doe_dx_pipeline_release(pipeline);
    doe_dx_timestamps *heaps[61] = {0};
    for (size_t index = 0; index < 61; index++) {
        assert(doe_dx_timestamps_create(fixture.context, 1, &heaps[index], NULL) == DOE_DX_OK);
        assert(doe_dx_commands_timestamp(fixture.work, heaps[index], 0, NULL) == DOE_DX_OK);
    }
    assert(draw(&fixture) == DOE_DX_RANGE);
    assert(!mock.draws && !fixture.color->owner && !fixture.depth->owner && !fixture.vertices->owner);
    for (size_t index = 0; index < 61; index++) doe_dx_timestamps_release(heaps[index]);
    close_fixture(&fixture);
}

static void unknown_and_terminal(bool terminal) {
    mesh_fixture fixture = {0};
    open_fixture(&fixture);
    fixture.draw.indices = &fixture.index;
    fixture.draw.draw.vertex_count = 0;
    assert(draw(&fixture) == DOE_DX_OK);
    release_objects(&fixture);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(fixture.work, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(fixture.work);
    assert(doe_dx_context_poll(fixture.context, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.buffers == 3 && mock.textures == 2 && mock.pipelines == 1);
    assert(doe_dx_context_destroy(fixture.context) == DOE_DX_BUSY);
    mock.failure = MOCK_NONE;
    if (terminal) {
        mock.lost = true;
        assert(doe_dx_context_poll(fixture.context, 0, NULL) == DOE_DX_DEVICE_LOST);
    } else {
        assert(doe_dx_context_poll(fixture.context, 0, NULL) == DOE_DX_PENDING);
        mock.completed = mock.last_signal;
        assert(doe_dx_context_poll(fixture.context, 0, NULL) == DOE_DX_OK);
    }
    assert(!mock.buffers && !mock.textures && !mock.pipelines && !mock.commands);
    assert(doe_dx_context_destroy(fixture.context) == DOE_DX_OK);
    mock_reset();
}

int main(void) {
    mock_reset();
    layout_validation();
    direct_spans();
    index_spans();
    geometry_alias_and_limits();
    depth_validation_and_rollback();
    depth_usage_and_copy();
    completion_publication_and_exclusivity();
    composed_budget_and_shader_alias();
    unknown_and_terminal(false);
    unknown_and_terminal(true);
    puts("D3D12 C mesh/depth: 10 groups passed (no SDK or GPU execution)");
    return 0;
}
