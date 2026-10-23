/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static void check_dispatch(fixture *f, doe_resource_layout *const *layouts,
    const doe_resource_dispatch_group *groups, size_t count,
    doe_resource_buffer *indirect, doe_resource_status expected)
{
    doe_resource_config c = config(f);
    doe_resource_pipeline_layout *layout;
    doe_resource_pipeline *pipeline;
    native_token pipeline_token = {0};
    uint32_t workgroup[] = {1, 1, 1};
    assert(doe_resource_pipeline_layout_create(&c, layouts, count, 0, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_create(&c, layout, native(f, &pipeline_token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    size_t allocations = f->allocations;
    doe_resource_status status = indirect ?
        doe_resource_encoder_dispatch_indirect(encoder, pipeline, groups, count, indirect, 0) :
        doe_resource_encoder_dispatch_groups(encoder, pipeline, groups, count, 1, 1, 1);
    assert(status == expected);
    doe_resource_commands *commands = (void *)(uintptr_t)1;
    if (expected == DOE_RESOURCE_OK) {
        assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
    } else {
        assert(f->allocations == allocations);
        assert(doe_resource_encoder_finish(encoder, &commands) == expected);
        assert(commands == (void *)(uintptr_t)1);
    }
    doe_resource_encoder_release(encoder);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    assert(pipeline_token.releases == 1);
}

static void test_buffer_scope(void)
{
    for (unsigned scenario = 0; scenario < 9; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[2] = {{0}, {0}};
        doe_resource_buffer *buffers[2];
        for (size_t i = 0; i < 2; i++) buffers[i] = buffer(&f, &tokens[i], 256,
            WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform | WGPUBufferUsage_Indirect);
        doe_resource_layout_entry entries[2] = {
            {.binding = 0, .visibility = WGPUShaderStage_Compute, .kind = DOE_BIND_BUFFER,
                .buffer_type = WGPUBufferBindingType_Storage},
            {.binding = 1, .visibility = WGPUShaderStage_Compute, .kind = DOE_BIND_BUFFER,
                .buffer_type = WGPUBufferBindingType_Storage}
        };
        doe_resource_binding bindings[2] = {
            {.binding = 0, .kind = DOE_BIND_BUFFER, .resource.buffer = {buffers[0], 0, 64}},
            {.binding = 1, .kind = DOE_BIND_BUFFER, .resource.buffer = {buffers[0], 0, 64}}
        };
        doe_resource_status expected = DOE_RESOURCE_OK;
        if (scenario == 0) expected = DOE_RESOURCE_ALIAS;
        if (scenario == 1 || scenario == 2 || scenario == 3) bindings[1].resource.buffer.offset = 64;
        if (scenario == 2 || scenario == 4) entries[1].buffer_type = WGPUBufferBindingType_ReadOnlyStorage;
        if (scenario == 3 || scenario == 4) entries[0].buffer_type = WGPUBufferBindingType_Uniform;
        if (scenario == 2 || scenario == 3 || scenario == 7) expected = DOE_RESOURCE_USAGE;
        if (scenario == 5) bindings[1].resource.buffer.buffer = buffers[1];
        if (scenario == 6) {
            entries[1].dynamic_offset = true;
            expected = DOE_RESOURCE_OK;
        }
        if (scenario == 7 || scenario == 8) {
            bindings[1].resource.buffer.offset = 64;
            if (scenario == 8) entries[0].buffer_type = entries[1].buffer_type = WGPUBufferBindingType_ReadOnlyStorage;
        }
        doe_resource_layout *layout;
        assert(doe_resource_layout_create(&c, entries, 2, false, &layout) == DOE_RESOURCE_OK);
        doe_resource_binding_limits limits = {16, 16, 256, 256};
        doe_resource_bind_group *group;
        assert(doe_resource_bind_group_create(layout, bindings, 2, &limits, &group) == DOE_RESOURCE_OK);
        uint32_t offset = 64;
        doe_resource_dispatch_group dispatch = {group, scenario == 6 ? &offset : NULL, scenario == 6 ? 1 : 0};
        check_dispatch(&f, &layout, &dispatch, 1, scenario >= 7 ? buffers[0] : NULL, expected);
        if (scenario == 6) {
            offset = 32;
            check_dispatch(&f, &layout, &dispatch, 1, NULL, DOE_RESOURCE_ALIAS);
        }
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        for (size_t i = 0; i < 2; i++) {
            doe_resource_buffer_release(buffers[i]);
            assert(tokens[i].releases == 1);
        }
        empty(&f);
    }
}

static void test_cross_group_scope(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0};
    doe_resource_buffer *storage = buffer(&f, &token, 256, WGPUBufferUsage_Storage);
    doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Compute,
        .buffer_type = WGPUBufferBindingType_Storage, .dynamic_offset = true};
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.kind = DOE_BIND_BUFFER, .resource.buffer = {storage, 0, 64}};
    doe_resource_binding_limits limits = {16, 16, 256, 256};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
    uint32_t offsets[2] = {0, 64};
    doe_resource_dispatch_group groups[2] = {{group, &offsets[0], 1}, {group, &offsets[1], 1}};
    doe_resource_layout *layouts[2] = {layout, layout};
    check_dispatch(&f, layouts, groups, 2, NULL, DOE_RESOURCE_OK);
    offsets[1] = 16;
    check_dispatch(&f, layouts, groups, 2, NULL, DOE_RESOURCE_ALIAS);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_buffer_release(storage);
    assert(token.releases == 1);
    empty(&f);
}

static void test_texture_scope(void)
{
    for (unsigned scenario = 0; scenario < 12; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token image_tokens[2] = {{0}, {0}}, view_tokens[2] = {{0}, {0}};
        doe_resource_texture_desc desc = texture_desc();
        desc.depth_or_layers = 2;
        desc.mip_levels = 2;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
        doe_resource_texture_limits texture_limit = texture_limits();
        doe_resource_texture *images[2];
        doe_resource_view *views[2];
        for (size_t i = 0; i < 2; i++)
            assert(doe_resource_texture_create(&c, &desc, &texture_limit,
                native(&f, &image_tokens[i]), &images[i]) == DOE_RESOURCE_OK);
        doe_resource_view_desc view_desc = {.dimension = WGPUTextureViewDimension_2D, .mip_count = 1, .layer_count = 1};
        if (scenario == 8) view_desc.base_layer = 1;
        assert(doe_resource_view_create(images[0], &view_desc, native(&f, &view_tokens[0]), &views[0]) == DOE_RESOURCE_OK);
        if (scenario == 3) view_desc.base_mip = 1;
        if (scenario == 4) view_desc.base_layer = 1;
        if (scenario == 8) {
            view_desc.dimension = WGPUTextureViewDimension_2DArray;
            view_desc.base_layer = 0;
            view_desc.layer_count = 2;
        }
        assert(doe_resource_view_create(images[scenario == 5 || scenario == 7 ? 1 : 0], &view_desc,
            native(&f, &view_tokens[1]), &views[1]) == DOE_RESOURCE_OK);
        doe_resource_layout_entry entries[2] = {
            {.binding = 0, .kind = DOE_BIND_STORAGE_TEXTURE, .visibility = WGPUShaderStage_Compute,
                .storage_format = WGPUTextureFormat_RGBA8Unorm, .storage_access = WGPUStorageTextureAccess_WriteOnly},
            {.binding = 1, .kind = DOE_BIND_TEXTURE, .visibility = WGPUShaderStage_Compute,
                .sample_type = WGPUTextureSampleType_Float}
        };
        doe_resource_binding bindings[2] = {
            {.binding = 0, .kind = DOE_BIND_STORAGE_TEXTURE, .resource.view = views[0]},
            {.binding = 1, .kind = DOE_BIND_TEXTURE, .resource.view = views[1]}
        };
        doe_resource_status expected = scenario == 0 || scenario >= 6 ? DOE_RESOURCE_USAGE : DOE_RESOURCE_OK;
        if (scenario == 1) {
            entries[1] = entries[0];
            entries[1].binding = 1;
            bindings[1].kind = DOE_BIND_STORAGE_TEXTURE;
            expected = DOE_RESOURCE_ALIAS;
        }
        if (scenario == 2) entries[0].storage_access = WGPUStorageTextureAccess_ReadOnly;
        if (scenario >= 9) {
            entries[1] = entries[0];
            entries[1].binding = 1;
            entries[1].visibility = scenario == 9 ? 0 : scenario == 10 ? WGPUShaderStage_Fragment : WGPUShaderStage_Compute;
            bindings[1].kind = DOE_BIND_STORAGE_TEXTURE;
            expected = scenario == 11 ? DOE_RESOURCE_ALIAS : DOE_RESOURCE_OK;
        }
        doe_resource_external_texture *external = NULL;
        if (scenario == 8) entries[1].view_dimension = WGPUTextureViewDimension_2DArray;
        if (scenario == 6 || scenario == 7) {
            assert(doe_resource_external_texture_create(&c, views[1], scenario == 7 ? views[0] : NULL, &external) == DOE_RESOURCE_OK);
            entries[1].kind = bindings[1].kind = DOE_BIND_EXTERNAL_TEXTURE;
            bindings[1].resource.external_texture = external;
        }
        doe_resource_layout *layout;
        assert(doe_resource_layout_create(&c, entries, 2, false, &layout) == DOE_RESOURCE_OK);
        doe_resource_binding_limits limits = {16, 16, 256, 256};
        doe_resource_bind_group *group;
        assert(doe_resource_bind_group_create(layout, bindings, 2, &limits, &group) == DOE_RESOURCE_OK);
        doe_resource_dispatch_group dispatch = {group, NULL, 0};
        check_dispatch(&f, &layout, &dispatch, 1, NULL, expected);
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        doe_resource_external_texture_release(external);
        for (size_t i = 0; i < 2; i++) doe_resource_view_release(views[i]);
        for (size_t i = 0; i < 2; i++) {
            doe_resource_texture_release(images[i]);
            assert(image_tokens[i].releases == 1 && view_tokens[i].releases == 1);
        }
        empty(&f);
    }
}

static void test_empty_layout_holes(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout *layouts[3];
    assert(doe_resource_layout_create(&c, NULL, 0, true, &layouts[0]) == DOE_RESOURCE_OK);
    assert(doe_resource_layout_create(&c, NULL, 0, true, &layouts[1]) == DOE_RESOURCE_OK);
    doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Compute,
        .buffer_type = WGPUBufferBindingType_Storage, .dynamic_offset = true};
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layouts[2]) == DOE_RESOURCE_OK);
    native_token token = {0}, pipeline_token = {0};
    doe_resource_buffer *storage = buffer(&f, &token, 256, WGPUBufferUsage_Storage);
    doe_resource_binding binding = {.kind = DOE_BIND_BUFFER, .resource.buffer = {storage, 0, 64}};
    doe_resource_binding_limits limits = {16, 16, 256, 256};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layouts[2], &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, layouts, 3, 0, &layout) == DOE_RESOURCE_OK);
    assert(!doe_resource_pipeline_layout_group_required(layout, 0));
    assert(!doe_resource_pipeline_layout_group_required(layout, 1));
    assert(doe_resource_pipeline_layout_group_required(layout, 2));
    assert(!doe_resource_pipeline_layout_group_required(layout, 3));
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[] = {1, 1, 1}, offset = 64;
    assert(doe_resource_pipeline_create(&c, layout, native(&f, &pipeline_token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    /* Empty slots ignore stale bindings/offset storage, never copy it. */
    doe_resource_dispatch_group groups[3] = {{NULL, NULL, SIZE_MAX}, {group, NULL, SIZE_MAX}, {group, &offset, 1}};
    doe_resource_encoder *encoder;
    doe_resource_commands *commands;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_dispatch_groups(encoder, pipeline, groups, 3, 1, 1, 1) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 1 && !records[0].value.dispatch.groups[0] && !records[0].value.dispatch.groups[1]);
    assert(records[0].value.dispatch.dynamic_offsets->counts[0] == 0 &&
        records[0].value.dispatch.dynamic_offsets->counts[1] == 0);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    doe_resource_encoder_release(encoder);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    doe_resource_bind_group_release(group);
    for (size_t i = 0; i < 3; i++) doe_resource_layout_release(layouts[i]);
    doe_resource_buffer_release(storage);
    assert(token.releases == 1 && pipeline_token.releases == 1);
    empty(&f);
}

static void test_binding_visibility(void)
{
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        fixture f = fresh();
        native_token token = {0};
        doe_resource_buffer *storage = buffer(&f, &token, 256, WGPUBufferUsage_Storage);
        doe_resource_layout_entry entries[2] = {
            {.binding = 0, .kind = DOE_BIND_BUFFER, .buffer_type = WGPUBufferBindingType_Storage,
                .visibility = WGPUShaderStage_Compute},
            {.binding = 1, .kind = DOE_BIND_BUFFER, .buffer_type = WGPUBufferBindingType_Storage,
                .visibility = scenario == 0 ? 0 : scenario == 1 ? WGPUShaderStage_Fragment : WGPUShaderStage_Compute}
        };
        if (scenario == 3) {
            entries[0].visibility = WGPUShaderStage_Compute | WGPUShaderStage_Fragment;
            entries[1].visibility = WGPUShaderStage_Fragment;
        }
        if (scenario == 4) {
            entries[0].visibility = WGPUShaderStage_Compute;
            entries[1].visibility = WGPUShaderStage_Fragment;
        }
        doe_resource_binding bindings[2] = {
            {.binding = 0, .kind = DOE_BIND_BUFFER, .resource.buffer = {storage, 0, 64}},
            {.binding = 1, .kind = DOE_BIND_BUFFER, .resource.buffer = {storage, 0, 64}}
        };
        doe_resource_config c = config(&f);
        doe_resource_layout *layout;
        assert(doe_resource_layout_create(&c, entries, 2, false, &layout) == DOE_RESOURCE_OK);
        doe_resource_binding_limits limits = {16, 16, 256, 256};
        doe_resource_bind_group *group;
        assert(doe_resource_bind_group_create(layout, bindings, 2, &limits, &group) == DOE_RESOURCE_OK);
        doe_resource_dispatch_group input = {group, NULL, 0};
        check_dispatch(&f, &layout, &input, 1, NULL,
            scenario == 2 || scenario == 3 ? DOE_RESOURCE_ALIAS : DOE_RESOURCE_OK);
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        doe_resource_buffer_release(storage);
        empty(&f);
    }
}

static void test_nullable_layout_slots(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout *explicit_empty;
    assert(doe_resource_layout_create(&c, NULL, 0, false, &explicit_empty) == DOE_RESOURCE_OK);
    doe_resource_layout *groups[2] = {NULL, explicit_empty};
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, groups, 2, 0, &layout) == DOE_RESOURCE_OK);
    assert(!doe_resource_pipeline_layout_group_required(layout, 0));
    assert(!doe_resource_pipeline_layout_group_required(layout, 1));
    doe_resource_layout *out = (void *)(uintptr_t)1;
    size_t allocations = f.allocations;
    assert(!doe_resource_pipeline_layout_group(layout, 1));
    assert(f.allocations == allocations);
    f.fail_at = f.allocations;
    out = (void *)(uintptr_t)1;
    assert(doe_resource_pipeline_layout_get_group(layout, 3, &out) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(out == (void *)(uintptr_t)1 && !doe_resource_pipeline_layout_group(layout, 3));
    f.fail_at = SIZE_MAX;
    assert(doe_resource_pipeline_layout_get_group(layout, 3, &out) == DOE_RESOURCE_OK);
    doe_resource_layout *materialized = out;
    assert(doe_resource_layout_device(out) == &f);
    size_t entries = SIZE_MAX;
    assert(doe_resource_layout_entries(out, &entries) && entries == 0);
    allocations = f.allocations;
    assert(doe_resource_pipeline_layout_get_group(layout, 0, &out) == DOE_RESOURCE_OK && out == materialized);
    assert(doe_resource_pipeline_layout_get_group(layout, 1, &out) == DOE_RESOURCE_OK && out == materialized);
    assert(materialized != explicit_empty && f.allocations == allocations);
    assert(doe_resource_pipeline_layout_group(layout, 2) == materialized);
    out = (void *)(uintptr_t)1;
    assert(doe_resource_pipeline_layout_get_group(layout, DOE_RESOURCE_MAX_GROUPS, &out) == DOE_RESOURCE_RANGE);
    assert(out == (void *)(uintptr_t)1);
    doe_resource_layout_retain(materialized);
    native_token token = {0};
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[] = {1, 1, 1};
    assert(doe_resource_pipeline_create(&c, layout, native(&f, &token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    native_token render_token = {0};
    doe_resource_render_pipeline_desc render_desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    doe_resource_render_pipeline *render_pipeline;
    assert(doe_resource_render_pipeline_create(&c, layout, &render_desc,
        native(&f, &render_token), &render_pipeline) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_release(render_pipeline);
    assert(render_token.releases == 1);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group inputs[2] = {{NULL, NULL, SIZE_MAX}, {NULL, NULL, SIZE_MAX}};
    assert(doe_resource_encoder_dispatch_groups(encoder, pipeline, inputs, 2, 1, 1, 1) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    doe_resource_layout_release(explicit_empty);
    assert(doe_resource_layout_device(materialized) == &f); /* Caller owns its explicit retain. */
    doe_resource_layout_release(materialized);
    assert(token.releases == 1);
    empty(&f);
}

static void test_automatic_empty_reuse(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout *groups[3];
    doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .buffer_type = WGPUBufferBindingType_Uniform};
    assert(doe_resource_layout_create(&c, NULL, 0, true, &groups[0]) == DOE_RESOURCE_OK);
    assert(doe_resource_layout_create(&c, NULL, 0, true, &groups[1]) == DOE_RESOURCE_OK);
    assert(doe_resource_layout_create(&c, &entry, 1, true, &groups[2]) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *automatic;
    assert(doe_resource_pipeline_layout_create(&c, groups, 3, 0, &automatic) == DOE_RESOURCE_OK);
    assert(!doe_resource_pipeline_layout_group(automatic, 0));
    assert(!doe_resource_pipeline_layout_group(automatic, 1));
    native_token first_token = {0};
    uint32_t workgroup[] = {1, 1, 1};
    doe_resource_pipeline *first;
    assert(doe_resource_pipeline_create(&c, automatic, native(&f, &first_token), workgroup, &first) == DOE_RESOURCE_OK);
    doe_resource_layout *hole;
    assert(doe_resource_pipeline_layout_get_group(automatic, 0, &hole) == DOE_RESOURCE_OK);
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        doe_resource_layout *slot = scenario == 0 ? NULL : scenario == 1 ? hole : scenario == 2 ? groups[0] : groups[2];
        doe_resource_pipeline_layout *explicit;
        assert(doe_resource_pipeline_layout_create(&c, &slot, 1, 0, &explicit) == DOE_RESOURCE_OK);
        for (unsigned repeat = 0; repeat < 2; repeat++) {
            native_token token = {0}, render_token = {0};
            doe_resource_pipeline *pipeline = (void *)(uintptr_t)1;
            doe_resource_status expected = scenario == 3 ? DOE_RESOURCE_LAYOUT : DOE_RESOURCE_OK;
            assert(doe_resource_pipeline_create(&c, explicit, native(&f, &token), workgroup, &pipeline) == expected);
            doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
            doe_resource_render_pipeline *render = (void *)(uintptr_t)1;
            assert(doe_resource_render_pipeline_create(&c, explicit, &desc, native(&f, &render_token), &render) == expected);
            if (expected == DOE_RESOURCE_OK) {
                doe_resource_pipeline_release(pipeline);
                doe_resource_render_pipeline_release(render);
                assert(token.releases == 1 && render_token.releases == 1);
            } else assert(pipeline == (void *)(uintptr_t)1 && render == (void *)(uintptr_t)1 &&
                !token.releases && !render_token.releases);
        }
        doe_resource_pipeline_layout_release(explicit);
    }
    doe_resource_pipeline_release(first);
    doe_resource_pipeline_layout_release(automatic);
    for (size_t i = 0; i < 3; i++) doe_resource_layout_release(groups[i]);
    assert(first_token.releases == 1);
    empty(&f);
}

int main(void)
{
    test_buffer_scope();
    test_cross_group_scope();
    test_texture_scope();
    test_empty_layout_holes();
    test_binding_visibility();
    test_nullable_layout_slots();
    test_automatic_empty_reuse();
    puts("dispatch usage owner: 7 host suites passed; no GPU execution claimed");
    return 0;
}
