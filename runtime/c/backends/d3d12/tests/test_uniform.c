/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static const unsigned char bytecode[] = {1}; /* Only the native-call seam sees this. */

static doe_dx_context *context(void) {
    doe_dx_context *owner = NULL;
    assert(doe_dx_context_create(NULL, &owner, NULL) == DOE_DX_OK);
    return owner;
}

static doe_dx_commands *commands(doe_dx_context *owner) {
    doe_dx_commands *work = NULL;
    assert(doe_dx_commands_create(owner, &work, NULL) == DOE_DX_OK);
    return work;
}

static doe_dx_buffer *buffer(doe_dx_context *owner, uint64_t size, doe_dx_memory memory) {
    doe_dx_buffer *result = NULL;
    assert(doe_dx_buffer_create(owner, size, memory, &result, NULL) == DOE_DX_OK);
    return result;
}

static void finish(doe_dx_context *owner) {
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}

static void register_namespaces(void) {
    doe_dx_context *owner = context();
    doe_dx_binding_layout layouts[2] = {
        {0, 0, 0, DOE_DX_READ_ONLY, DOE_DX_UNIFORM},
        {0, 0, 4, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED}
    };
    doe_dx_pipeline_desc description = {bytecode, sizeof(bytecode), layouts, 2};
    doe_dx_pipeline *shader = NULL;
    /* b0 and t0 do not collide, while two b0 declarations in one space do. */
    assert(doe_dx_pipeline_create(owner, &description, &shader, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_descriptor_kind(&shader->bindings[0]) == DOE_DX_DESCRIPTOR_CBV);
    assert(doe_dx_buffer_descriptor_kind(&shader->bindings[1]) == DOE_DX_DESCRIPTOR_SRV);
    doe_dx_pipeline_release(shader);
    doe_dx_pipeline *unchanged = (doe_dx_pipeline *)(uintptr_t)1;
    layouts[1] = layouts[0];
    assert(doe_dx_pipeline_create(owner, &description, &unchanged, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(unchanged == (doe_dx_pipeline *)(uintptr_t)1 && !mock.pipelines);
    layouts[1].register_space = 1;
    assert(doe_dx_pipeline_create(owner, &description, &shader, NULL) == DOE_DX_OK);
    doe_dx_pipeline_release(shader);
    description.binding_count = 1;
    layouts[0].access = DOE_DX_READ_WRITE;
    assert(doe_dx_pipeline_create(owner, &description, &unchanged, NULL) == DOE_DX_INVALID_ARGUMENT);
    layouts[0].access = DOE_DX_READ_ONLY;
    layouts[0].stride = 4;
    assert(doe_dx_pipeline_create(owner, &description, &unchanged, NULL) == DOE_DX_INVALID_ARGUMENT);
    layouts[0].stride = 0;
    layouts[0].view = DOE_DX_STRUCTURED;
    assert(doe_dx_pipeline_create(owner, &description, &unchanged, NULL) == DOE_DX_INVALID_ARGUMENT);
    layouts[0].view = (doe_dx_buffer_view)17;
    assert(doe_dx_pipeline_create(owner, &description, &unchanged, NULL) == DOE_DX_INVALID_ARGUMENT);
    finish(owner);
}

static void binding_ranges_and_lifetime(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, 131072, DOE_DX_UPLOAD);
    doe_dx_buffer *uniform = buffer(owner, 131072, DOE_DX_DEFAULT);
    doe_dx_binding_layout layout = {2, 3, 0, DOE_DX_READ_ONLY, DOE_DX_UNIFORM};
    doe_dx_pipeline_desc description = {bytecode, sizeof(bytecode), &layout, 1};
    doe_dx_pipeline *shader = NULL;
    assert(doe_dx_pipeline_create(owner, &description, &shader, NULL) == DOE_DX_OK);
    layout.view = DOE_DX_STRUCTURED; /* Constructor copied its immutable layout. */
    assert(shader->bindings[0].view == DOE_DX_UNIFORM);
    doe_dx_commands *work = commands(owner);
    doe_dx_buffer_binding binding = {uniform, 256, 256};
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_copy(work, upload, 0, uniform, 0, 131072, NULL) == DOE_DX_OK);
    assert(!uniform->initialized);
    binding.offset = 4;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_ALIGNMENT);
    binding.offset = 256;
    binding.size = 4;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_ALIGNMENT);
    binding.size = 65792;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_RANGE);
    binding.size = 0;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_RANGE);
    binding.size = 65536;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_OK);
    assert(mock.bindings[0].offset == 256 && mock.bindings[0].size == 65536);
    assert(!uniform->initialized && work->buffer_count == 2);
    doe_dx_pipeline_release(shader);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(uniform);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    assert(mock.buffers == 2 && mock.pipelines == 1);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(!mock.buffers && !mock.pipelines && !mock.commands);
    finish(owner);
}

static void shared_graphics_and_alias_rules(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, 256, DOE_DX_UPLOAD);
    doe_dx_buffer *uniform = buffer(owner, 256, DOE_DX_DEFAULT);
    doe_dx_texture *color = NULL;
    doe_dx_texture_desc color_description = {4, 4, DOE_DX_RGBA8_UNORM, DOE_DX_TEXTURE_RENDER_ATTACHMENT};
    assert(doe_dx_texture_create(owner, &color_description, &color, NULL) == DOE_DX_OK);
    doe_dx_binding_layout layout = {0, 0, 0, DOE_DX_READ_ONLY, DOE_DX_UNIFORM};
    doe_dx_graphics_pipeline_desc description = {0};
    description.vertex_bytecode = bytecode;
    description.vertex_bytecode_size = sizeof(bytecode);
    description.color_format = DOE_DX_RGBA8_UNORM;
    description.fragment.buffers = (doe_dx_pipeline_desc){bytecode, sizeof(bytecode), &layout, 1};
    doe_dx_pipeline *shader = NULL;
    assert(doe_dx_pipeline_create_graphics(owner, &description, &shader, NULL) == DOE_DX_OK);
    doe_dx_commands *work = commands(owner);
    assert(doe_dx_commands_copy(work, upload, 0, uniform, 0, 256, NULL) == DOE_DX_OK);
    doe_dx_buffer_binding binding = {uniform, 0, 256};
    doe_dx_compute_resources resources = {&binding, 1, NULL, 0};
    doe_dx_draw_desc draw = {color, true, {0, 0, 0, 1}, 3, 1, 0, 0};
    mock.failure = MOCK_DRAW;
    assert(doe_dx_commands_draw(work, shader, &resources, &draw, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!mock.draws && !work->pipeline_count && !work->texture_count);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_draw(work, shader, &resources, &draw, NULL) == DOE_DX_OK);
    assert(mock.binding_count == 1 && mock.bindings[0].buffer == uniform->native);
    doe_dx_pipeline_release(shader);
    doe_dx_texture_release(color);
    doe_dx_commands_release(work);
    doe_dx_binding_layout aliases[2] = {
        {0, 0, 0, DOE_DX_READ_ONLY, DOE_DX_UNIFORM},
        {0, 0, 4, DOE_DX_READ_WRITE, DOE_DX_STRUCTURED}
    };
    doe_dx_pipeline_desc alias_description = {bytecode, sizeof(bytecode), aliases, 2};
    assert(doe_dx_pipeline_create(owner, &alias_description, &shader, NULL) == DOE_DX_OK);
    work = commands(owner);
    assert(doe_dx_commands_copy(work, upload, 0, uniform, 0, 256, NULL) == DOE_DX_OK);
    doe_dx_buffer_binding alias_bindings[2] = {{uniform, 0, 256}, {uniform, 0, 256}};
    assert(doe_dx_commands_dispatch(work, shader, alias_bindings, 2, 1, 1, 1, NULL) == DOE_DX_UNSUPPORTED);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(shader);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(uniform);
    finish(owner);
}

int main(void) {
    mock_reset();
    register_namespaces();
    binding_ranges_and_lifetime();
    shared_graphics_and_alias_rules();
    puts("D3D12 C uniform bindings: 3 groups passed (no SDK or GPU execution)");
    return 0;
}
