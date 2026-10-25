/* SPDX-License-Identifier: MIT
 * Sparse binding numbers up to maxBindingsPerBindGroup and WebGPU binding
 * slot limits. Host ownership only; native execution is tested separately. */
#include "fixture.h"

static doe_resource_layout_entry storage_entry(uint32_t binding, bool dynamic)
{
    return (doe_resource_layout_entry){.binding = binding, .visibility = WGPUShaderStage_Compute,
        .kind = DOE_BIND_BUFFER, .buffer_type = WGPUBufferBindingType_Storage, .dynamic_offset = dynamic};
}

static void test_sparse_bindings(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token tokens[3] = {{0}, {0}, {0}};
    doe_resource_buffer *buffers[3];
    for (size_t i = 0; i < 3; i++) buffers[i] = buffer(&f, &tokens[i], 256, WGPUBufferUsage_Storage);
    const uint32_t numbers[3] = {999, 0, 500};
    doe_resource_layout_entry entries[3];
    doe_resource_binding bindings[3];
    for (size_t i = 0; i < 3; i++) {
        entries[i] = storage_entry(numbers[i], i == 0);
        bindings[i] = (doe_resource_binding){.binding = numbers[i], .kind = DOE_BIND_BUFFER,
            .resource.buffer = {buffers[i], 0, 64}};
    }
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, entries, 3, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding_limits limits = {256, 256, 65536, 1u << 27};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, bindings, 3, &limits, &group) == DOE_RESOURCE_OK);
    /* Whole-group resolution is in ascending binding order. */
    doe_resource_binding resolved[3];
    const uint32_t misaligned = 192;
    assert(doe_resource_bind_group_resolve(group, &misaligned, 1, resolved, 3) == DOE_RESOURCE_ALIGNMENT);
    const uint32_t aligned = 0;
    assert(doe_resource_bind_group_resolve(group, &aligned, 1, resolved, 3) == DOE_RESOURCE_OK);
    assert(resolved[0].binding == 0 && resolved[0].resource.buffer.buffer == buffers[1]);
    assert(resolved[1].binding == 500 && resolved[1].resource.buffer.buffer == buffers[2]);
    assert(resolved[2].binding == 999 && resolved[2].resource.buffer.buffer == buffers[0]);
    doe_resource_binding one;
    assert(doe_resource_bind_group_resolve_binding(group, &aligned, 1, 999, &one) == DOE_RESOURCE_OK);
    assert(one.resource.buffer.buffer == buffers[0] && one.resource.buffer.offset == 0);
    assert(doe_resource_bind_group_resolve_binding(group, &aligned, 1, 500, &one) == DOE_RESOURCE_OK);
    assert(one.resource.buffer.buffer == buffers[2]);
    assert(doe_resource_bind_group_resolve_binding(group, &aligned, 1, 998, &one) == DOE_RESOURCE_LAYOUT);
    assert(doe_resource_bind_group_resolve_binding(group, NULL, 0, 0, &one) == DOE_RESOURCE_LAYOUT);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    for (size_t i = 0; i < 3; i++) doe_resource_buffer_release(buffers[i]);
    empty(&f);
}

static void test_binding_numbers(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout *layout = (void *)(uintptr_t)1;
    doe_resource_layout_entry entries[2] = {storage_entry(DOE_RESOURCE_MAX_BINDING_NUMBER, false),
        storage_entry(3, false)};
    assert(doe_resource_layout_create(&c, entries, 1, false, &layout) == DOE_RESOURCE_RANGE);
    entries[0].binding = 3;
    assert(doe_resource_layout_create(&c, entries, 2, false, &layout) == DOE_RESOURCE_LAYOUT);
    assert(layout == (void *)(uintptr_t)1 && !f.allocations);
}

static doe_resource_status layout_status(doe_resource_config *c, const doe_resource_layout_entry *entries,
    size_t count)
{
    doe_resource_layout *layout = NULL;
    doe_resource_status status = doe_resource_layout_create(c, entries, count, false, &layout);
    if (status == DOE_RESOURCE_OK) doe_resource_layout_release(layout);
    return status;
}

static void test_slot_limits(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout_entry entries[17];
    /* maxStorageBuffersPerShaderStage default 8. */
    for (uint32_t i = 0; i < 9; i++) entries[i] = storage_entry(i, false);
    assert(layout_status(&c, entries, 8) == DOE_RESOURCE_OK);
    assert(layout_status(&c, entries, 9) == DOE_RESOURCE_RANGE);
    c.slot_limits.max_storage_buffers = 9;
    assert(layout_status(&c, entries, 9) == DOE_RESOURCE_OK);
    /* Per stage: nine buffers split across stages fit the default. */
    c.slot_limits.max_storage_buffers = 0;
    entries[8].visibility = WGPUShaderStage_Fragment;
    entries[8].buffer_type = WGPUBufferBindingType_ReadOnlyStorage;
    assert(layout_status(&c, entries, 9) == DOE_RESOURCE_OK);
    /* maxDynamicStorageBuffersPerPipelineLayout default 4. */
    for (uint32_t i = 0; i < 5; i++) entries[i] = storage_entry(i, true);
    assert(layout_status(&c, entries, 4) == DOE_RESOURCE_OK);
    assert(layout_status(&c, entries, 5) == DOE_RESOURCE_RANGE);
    c.slot_limits.max_dynamic_storage_buffers = 5;
    assert(layout_status(&c, entries, 5) == DOE_RESOURCE_OK);
    /* Dynamic uniform default 8, uniform buffers per stage default 12. */
    for (uint32_t i = 0; i < 13; i++)
        entries[i] = (doe_resource_layout_entry){.binding = i, .visibility = WGPUShaderStage_Vertex,
            .kind = DOE_BIND_BUFFER, .buffer_type = WGPUBufferBindingType_Uniform, .dynamic_offset = i < 9};
    assert(layout_status(&c, entries, 8) == DOE_RESOURCE_OK);
    assert(layout_status(&c, entries, 9) == DOE_RESOURCE_RANGE);
    c.slot_limits.max_dynamic_uniform_buffers = 9;
    assert(layout_status(&c, entries, 12) == DOE_RESOURCE_OK);
    assert(layout_status(&c, entries, 13) == DOE_RESOURCE_RANGE);
    /* An external texture counts 4 sampled textures, 1 sampler, 1 uniform buffer. */
    for (uint32_t i = 0; i < 5; i++)
        entries[i] = (doe_resource_layout_entry){.binding = i, .visibility = WGPUShaderStage_Fragment,
            .kind = DOE_BIND_EXTERNAL_TEXTURE};
    assert(layout_status(&c, entries, 4) == DOE_RESOURCE_OK);
    assert(layout_status(&c, entries, 5) == DOE_RESOURCE_RANGE);
    empty(&f);
}

static void test_pipeline_layout_totals(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout_entry entries[5];
    for (uint32_t i = 0; i < 5; i++) entries[i] = storage_entry(i, false);
    doe_resource_layout *groups[3];
    for (size_t i = 0; i < 3; i++)
        assert(doe_resource_layout_create(&c, entries, i == 2 ? 1 : 5, false, &groups[i]) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *layout = (void *)(uintptr_t)1;
    /* 5 + 5 storage buffers in the compute stage exceed the default 8. */
    assert(doe_resource_pipeline_layout_create(&c, groups, 2, 0, &layout) == DOE_RESOURCE_RANGE);
    doe_resource_layout *pair[2] = {groups[0], groups[2]};
    assert(doe_resource_pipeline_layout_create(&c, pair, 2, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(layout);
    c.slot_limits.max_storage_buffers = 16;
    c.slot_limits.max_bind_groups = 2;
    assert(doe_resource_pipeline_layout_create(&c, groups, 3, 0, &layout) == DOE_RESOURCE_RANGE);
    assert(doe_resource_pipeline_layout_create(&c, groups, 2, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(layout);
    for (size_t i = 0; i < 3; i++) doe_resource_layout_release(groups[i]);
    empty(&f);
}

/* createPipelineLayout immediateSize: a multiple of 4 within
 * maxImmediateSize (zero config selects the WebGPU default 64). */
static void test_immediate_size(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_pipeline_layout *layout = NULL;
    const uint32_t valid[] = {0, 4, 64};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        assert(doe_resource_pipeline_layout_create(&c, NULL, 0, valid[i], &layout) == DOE_RESOURCE_OK);
        doe_resource_pipeline_layout_release(layout);
    }
    const uint32_t invalid[] = {1, 2, 3, 5, 68};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        assert(doe_resource_pipeline_layout_create(&c, NULL, 0, invalid[i], &layout) == DOE_RESOURCE_RANGE);
    c.max_immediate_size = 128;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 128, &layout) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(layout);
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 132, &layout) == DOE_RESOURCE_RANGE);
    empty(&f);
}

/* Immediate data is captured at each dispatch, not borrowed until submission. */
static void test_immediate_snapshots(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0};
    doe_resource_pipeline_layout *layout;
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[3] = {1,1,1};
    assert(doe_resource_pipeline_layout_create(&c,NULL,0,32,&layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_create(&c,layout,(doe_resource_native){&token,&f,native_release},
        workgroup,&pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_require_immediates(pipeline,UINT64_C(0xf000f)) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_require_immediates(pipeline,UINT64_C(1)<<40) == DOE_RESOURCE_LAYOUT);
    for (unsigned missing = 0; missing < 2; ++missing) {
        doe_resource_encoder *encoder;
        doe_resource_compute_pass *pass;
        assert(doe_resource_encoder_create(&c,&encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_compute(encoder,&pass) == DOE_RESOURCE_OK);
        assert(doe_resource_compute_pass_set_pipeline(pass,pipeline) == DOE_RESOURCE_OK);
        uint32_t value = 17;
        assert(doe_resource_compute_pass_set_immediates(pass,0,&value,4) == DOE_RESOURCE_OK);
        if (missing) {
            assert(doe_resource_compute_pass_dispatch(pass,1,1,1) == DOE_RESOURCE_STATE);
            assert(doe_resource_compute_pass_end(pass) != DOE_RESOURCE_OK);
        } else {
            value = 31;
            assert(doe_resource_compute_pass_set_immediates(pass,16,&value,4) == DOE_RESOURCE_OK);
            assert(doe_resource_compute_pass_dispatch(pass,1,1,1) == DOE_RESOURCE_OK);
            value = 99;
            assert(doe_resource_compute_pass_set_immediates(pass,0,&value,4) == DOE_RESOURCE_OK);
            assert(doe_resource_compute_pass_dispatch(pass,1,1,1) == DOE_RESOURCE_OK);
            assert(doe_resource_compute_pass_end(pass) == DOE_RESOURCE_OK);
            doe_resource_commands *commands;
            assert(doe_resource_encoder_finish(encoder,&commands) == DOE_RESOURCE_OK);
            size_t count;
            const doe_resource_command *records = doe_resource_commands_borrow(commands,&count);
            assert(count == 2);
            assert(records[0].value.dispatch.immediates.words[0] == 17);
            assert(records[1].value.dispatch.immediates.words[0] == 99);
            assert(records[0].value.dispatch.immediates.words[4] == 31);
            assert(records[0].value.dispatch.immediates.words[1] == 0);
            assert(records[0].value.dispatch.immediates.initialized_bytes == UINT64_C(0xf000f));
            doe_resource_commands_release(commands);
        }
        doe_resource_compute_pass_release(pass);
        doe_resource_encoder_release(encoder);
    }
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    assert(token.releases == 1);
    empty(&f);
}

static void test_full_group(void)
{
    /* maxBindingsPerBindGroup entries in one group, with usage collected
     * beyond the stack scratch capacity. */
    fixture f = fresh();
    doe_resource_config c = config(&f);
    c.slot_limits.max_storage_buffers = DOE_RESOURCE_MAX_BINDING_NUMBER;
    enum { COUNT = DOE_RESOURCE_MAX_BINDING_NUMBER };
    doe_resource_layout_entry *entries = calloc(COUNT, sizeof(*entries));
    doe_resource_binding *bindings = calloc(COUNT, sizeof(*bindings));
    assert(entries && bindings);
    native_token token = {0};
    doe_resource_buffer *storage = buffer(&f, &token, (uint64_t)COUNT * 256, WGPUBufferUsage_Storage);
    for (uint32_t i = 0; i < COUNT; i++) {
        entries[i] = storage_entry(COUNT - 1 - i, false);
        entries[i].buffer_type = WGPUBufferBindingType_ReadOnlyStorage;
        bindings[i] = (doe_resource_binding){.binding = COUNT - 1 - i, .kind = DOE_BIND_BUFFER,
            .resource.buffer = {storage, (uint64_t)(COUNT - 1 - i) * 256, 4}};
    }
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, entries, COUNT, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding_limits limits = {256, 256, 65536, 1u << 27};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, bindings, COUNT, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_binding last;
    assert(doe_resource_bind_group_resolve_binding(group, NULL, 0, COUNT - 1, &last) == DOE_RESOURCE_OK);
    assert(last.resource.buffer.offset == (uint64_t)(COUNT - 1) * 256);
    doe_resource_pipeline_layout *pipeline_layout;
    assert(doe_resource_pipeline_layout_create(&c, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    native_token pipeline_token = {0};
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[3] = {1, 1, 1};
    assert(doe_resource_pipeline_create(&c, pipeline_layout, native(&f, &pipeline_token), workgroup,
        &pipeline) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group dispatch = {group, NULL, 0};
    assert(doe_resource_encoder_dispatch_groups(encoder, pipeline, &dispatch, 1, 1, 1, 1) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_buffer_release(storage);
    free(entries);
    free(bindings);
    empty(&f);
}

int main(void)
{
    test_sparse_bindings();
    test_binding_numbers();
    test_slot_limits();
    test_pipeline_layout_totals();
    test_immediate_size();
    test_immediate_snapshots();
    test_full_group();
    puts("binding limit owner: 7 host suites passed; no native execution claimed");
    return 0;
}
