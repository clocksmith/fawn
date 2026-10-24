/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include "../src/internal.h"

static void test_copy_admission(void)
{
    fixture f = fresh(), other = fresh();
    native_token a = {0}, b = {0};
    doe_resource_buffer *src = buffer(&f, &a, 32, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    doe_resource_buffer *dst = buffer(&f, &b, 16, WGPUBufferUsage_CopyDst);
    doe_resource_buffer_copy copy = {src, dst, 16, 0, UINT64_MAX};
    uint64_t size = 123;
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISTINCT_BUFFERS, &size) == DOE_RESOURCE_OK);
    assert(size == 16);
    assert(doe_resource_buffer_copy_validate(&other, &copy, DOE_RESOURCE_DISTINCT_BUFFERS, &size) == DOE_RESOURCE_DEVICE_MISMATCH);
    copy.source_offset = UINT64_MAX - 3;
    size = 123;
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISTINCT_BUFFERS, &size) == DOE_RESOURCE_RANGE);
    assert(size == 123);
    copy = (doe_resource_buffer_copy){src, src, 0, 16, 16};
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISJOINT_RANGES, &size) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISTINCT_BUFFERS, &size) == DOE_RESOURCE_ALIAS);
    copy.destination_offset = 12;
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISJOINT_RANGES, &size) == DOE_RESOURCE_ALIAS);
    copy.size = 0;
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISTINCT_BUFFERS, &size) == DOE_RESOURCE_ALIAS);
    copy.source_offset = 1;
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISJOINT_RANGES, &size) == DOE_RESOURCE_ALIGNMENT);
    doe_resource_buffer_destroy(src);
    assert(!a.releases);
    assert(doe_resource_buffer_copy_validate(&f, &copy, DOE_RESOURCE_DISJOINT_RANGES, &size) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_buffer_release(src);
    doe_resource_buffer_release(dst);
    assert(a.releases == 1 && b.releases == 1);
    empty(&f);
}

static void test_mapping(void)
{
    fixture f = fresh();
    native_token token = {0};
    doe_resource_buffer *buf = buffer(&f, &token, 64, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    unsigned char memory[64] = {0}, *range = memory;
    const unsigned char *read_range = memory;
    uint64_t generation = 0, newer = 0;
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Read, 8, 16, &generation) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Read, 0, 16, &newer) == DOE_RESOURCE_STATE);
    assert(doe_resource_buffer_unmap(buf) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Read, 16, 16, &newer) == DOE_RESOURCE_OK);
    assert(newer > generation);
    assert(doe_resource_buffer_map_complete(buf, generation, true, memory) == DOE_RESOURCE_STATE);
    assert(doe_resource_buffer_map_complete(buf, newer, true, memory) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_mapped_range_const(buf, 24, 8, &read_range) == DOE_RESOURCE_OK && read_range == memory + 8);
    assert(doe_resource_buffer_map_mode(buf) == WGPUMapMode_Read);
    assert(doe_resource_buffer_mapped_range(buf, 24, 8, &range) == DOE_RESOURCE_USAGE);
    assert(doe_resource_buffer_mapped_range_const(buf, 24, 12, &read_range) == DOE_RESOURCE_RANGE);
    assert(read_range == memory + 8);
    assert(doe_resource_buffer_unmap(buf) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Write, 0, 8, &generation) == DOE_RESOURCE_USAGE);
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Read, 4, 8, &generation) == DOE_RESOURCE_ALIGNMENT);
    doe_resource_buffer_release(buf);
    empty(&f);

    f = fresh();
    token = (native_token){0};
    doe_resource_config c = config(&f);
    doe_resource_buffer_desc desc = {16, WGPUBufferUsage_Storage, true};
    assert(doe_resource_buffer_validate(&desc) == DOE_RESOURCE_OK);
    doe_resource_buffer_desc invalid = {15, WGPUBufferUsage_Storage, true};
    assert(doe_resource_buffer_validate(&invalid) == DOE_RESOURCE_ALIGNMENT);
    assert(doe_resource_buffer_create(&c, &desc, native(&f, &token), &buf) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_map_complete(buf, 0, true, memory) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_mapped_range(buf, 8, UINT64_MAX, &range) == DOE_RESOURCE_OK && range == memory + 8);
    doe_resource_buffer_destroy(buf);
    assert(doe_resource_buffer_map_complete(buf, 0, true, memory) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_buffer_release(buf);
    empty(&f);
}

static void test_mapping_completion_tokens(void)
{
    for (unsigned nonempty = 0; nonempty < 2; nonempty++) {
        for (unsigned failure = 0; failure < 2; failure++) {
            fixture f = fresh();
            native_token token = {0};
            doe_resource_config c = config(&f);
            const uint64_t size = nonempty ? 16 : 0;
            doe_resource_buffer_desc desc = {size, WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc, true};
            doe_resource_buffer *buf;
            assert(doe_resource_buffer_create(&c, &desc, native(&f, &token), &buf) == DOE_RESOURCE_OK);
            size_t allocations = f.allocations;
            unsigned char memory[16] = {0};
            unsigned char *mapping = nonempty ? memory : NULL;
            assert(doe_resource_buffer_map_complete(buf, 1, true, mapping) == DOE_RESOURCE_STATE);
            if (nonempty)
                assert(doe_resource_buffer_map_complete(buf, 0, true, NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
            assert(doe_resource_buffer_map_complete(buf, 0, !failure, mapping) ==
                (failure ? DOE_RESOURCE_EXECUTION_FAILED : DOE_RESOURCE_OK));
            assert(doe_resource_buffer_map_complete(buf, 0, true, memory) == DOE_RESOURCE_STATE);
            assert(doe_resource_buffer_map_complete(buf, 0, true, NULL) == DOE_RESOURCE_STATE);
            assert(doe_resource_buffer_map_complete(buf, 0, false, NULL) == DOE_RESOURCE_STATE);
            assert(doe_resource_buffer_map_mode(buf) == (failure ? WGPUMapMode_None : WGPUMapMode_Write));
            unsigned char *range = memory + 1;
            if (failure) {
                assert(doe_resource_buffer_mapped_range(buf, 0, size, &range) == DOE_RESOURCE_USAGE);
                assert(range == memory + 1);
            } else {
                assert(doe_resource_buffer_mapped_range(buf, 0, size, &range) == DOE_RESOURCE_OK);
                assert(range == mapping);
            }
            assert(doe_resource_buffer_unmap(buf) == DOE_RESOURCE_OK);
            assert(doe_resource_buffer_map_complete(buf, 0, true, mapping) == DOE_RESOURCE_STATE);
            uint64_t generation = 0;
            assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Write, size, 0, &generation) == DOE_RESOURCE_OK);
            assert(generation > 0);
            assert(doe_resource_buffer_map_complete(buf, 0, true, mapping) == DOE_RESOURCE_STATE);
            assert(doe_resource_buffer_map_complete(buf, generation, true, NULL) == DOE_RESOURCE_OK);
            assert(doe_resource_buffer_map_complete(buf, generation, true, memory) == DOE_RESOURCE_STATE);
            range = memory + 1;
            assert(doe_resource_buffer_mapped_range(buf, size, 0, &range) == DOE_RESOURCE_OK && !range);
            assert(doe_resource_buffer_unmap(buf) == DOE_RESOURCE_OK);
            uint64_t newer = 0;
            assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Write, 0, size, &newer) == DOE_RESOURCE_OK);
            assert(newer > generation);
            assert(doe_resource_buffer_map_complete(buf, generation, true, memory) == DOE_RESOURCE_STATE);
            assert(doe_resource_buffer_map_complete(buf, newer, false, NULL) == DOE_RESOURCE_EXECUTION_FAILED);
            assert(doe_resource_buffer_map_complete(buf, newer, true, mapping) == DOE_RESOURCE_STATE);
            assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Write, 0, size, &generation) == DOE_RESOURCE_OK);
            assert(generation > newer);
            doe_resource_buffer_destroy(buf);
            assert(doe_resource_buffer_map_complete(buf, generation, true, mapping) == DOE_RESOURCE_UNAVAILABLE);
            assert(f.allocations == allocations && !token.releases);
            doe_resource_buffer_release(buf);
            assert(token.releases == 1);
            empty(&f);
        }
    }
    fixture f = fresh();
    native_token token = {0};
    doe_resource_buffer *buf = buffer(&f, &token, 0, WGPUBufferUsage_MapWrite);
    /* Private boundary injection, not a public mutation hook. */
    buf->generation = UINT64_MAX - 1;
    uint64_t generation = 0;
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Write, 0, 0, &generation) == DOE_RESOURCE_OK);
    assert(generation == UINT64_MAX);
    assert(doe_resource_buffer_map_complete(buf, generation, true, NULL) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_unmap(buf) == DOE_RESOURCE_OK);
    generation = 123;
    assert(doe_resource_buffer_map_begin(buf, WGPUMapMode_Write, 0, 0, &generation) == DOE_RESOURCE_OVERFLOW);
    assert(generation == 123 && doe_resource_buffer_map_mode(buf) == WGPUMapMode_None);
    assert(doe_resource_buffer_map_complete(buf, UINT64_MAX, true, NULL) == DOE_RESOURCE_STATE);
    doe_resource_buffer_release(buf);
    assert(token.releases == 1);
    empty(&f);
}

static void test_texture_layout(void)
{
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    assert(doe_resource_texture_validate(&desc, &limits) == DOE_RESOURCE_OK);
    desc.mip_levels = 5;
    assert(doe_resource_texture_validate(&desc, &limits) == DOE_RESOURCE_RANGE);
    desc.mip_levels = 1;
    desc.samples = 4;
    assert(doe_resource_texture_validate(&desc, &limits) == DOE_RESOURCE_USAGE); /* no RenderAttachment */
    desc.usage |= WGPUTextureUsage_RenderAttachment; /* copy and binding usages are admitted */
    assert(doe_resource_texture_validate(&desc, &limits) == DOE_RESOURCE_OK);
    desc.usage |= WGPUTextureUsage_StorageBinding;
    assert(doe_resource_texture_validate(&desc, &limits) == DOE_RESOURCE_LAYOUT);
    desc = texture_desc();
    doe_resource_texture_copy copy = {.width = 4, .height = 2, .depth_or_layers = 1};
    doe_resource_copy_region region = {.required_bytes = 123};
    assert(doe_resource_texture_copy_validate(32, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_OK);
    assert(region.required_bytes == 32 && region.pitch == 16 && region.image_rows == 2);
    /* BC srgb/unorm view formats are compatible (any srgb-only difference). */
    doe_resource_texture_desc bc = texture_desc();
    bc.format = WGPUTextureFormat_BC7RGBAUnorm;
    const WGPUTextureFormat bc_views[2] = {WGPUTextureFormat_BC7RGBAUnormSrgb, WGPUTextureFormat_BC1RGBAUnormSrgb};
    bc.view_formats = bc_views;
    bc.view_format_count = 1;
    assert(doe_resource_texture_validate(&bc, &limits) == DOE_RESOURCE_OK);
    bc.view_formats = bc_views + 1;
    assert(doe_resource_texture_validate(&bc, &limits) == DOE_RESOURCE_LAYOUT);
    /* writeTexture (QUEUE) needs whole blocks; internal NATIVE uploads may end at the edge. */
    bc = texture_desc();
    bc.format = WGPUTextureFormat_BC1RGBAUnorm;
    bc.mip_levels = 3;
    doe_resource_texture_copy partial = {.mip = 2, .width = 2, .height = 2, .depth_or_layers = 1,
        .bytes_per_row = UINT32_MAX, .rows_per_image = UINT32_MAX};
    doe_resource_copy_region bc_region;
    assert(doe_resource_texture_copy_validate(8, &bc, &partial, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_QUEUE, &bc_region) == DOE_RESOURCE_LAYOUT);
    partial.bytes_per_row = partial.rows_per_image = 0;
    assert(doe_resource_texture_copy_validate(8, &bc, &partial, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &bc_region) == DOE_RESOURCE_OK);
    partial.width = partial.height = 4;
    partial.bytes_per_row = 7; /* QUEUE has no 256-byte alignment, only >= one block row */
    assert(doe_resource_texture_copy_validate(8, &bc, &partial, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_QUEUE, &bc_region) == DOE_RESOURCE_LAYOUT);
    partial.bytes_per_row = 8;
    partial.rows_per_image = UINT32_MAX;
    assert(doe_resource_texture_copy_validate(8, &bc, &partial, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_QUEUE, &bc_region) == DOE_RESOURCE_OK && bc_region.pitch == 8);
    desc.samples = 4; /* Buffer copies need one sample. */
    assert(doe_resource_texture_copy_validate(32, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_RANGE);
    desc.samples = 1;
    assert(doe_resource_texture_copy_validate(31, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_RANGE);
    assert(region.required_bytes == 32);
    copy.bytes_per_row = UINT32_MAX;
    copy.rows_per_image = UINT32_MAX;
    assert(doe_resource_texture_copy_validate(1024, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_WEBGPU, &region) == DOE_RESOURCE_LAYOUT);
    copy.bytes_per_row = 256;
    assert(doe_resource_texture_copy_validate(272, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_WEBGPU, &region) == DOE_RESOURCE_OK && region.required_bytes == 272);
    copy.origin[0] = UINT32_MAX;
    assert(doe_resource_texture_copy_validate(UINT64_MAX, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_RANGE);
    desc.format = WGPUTextureFormat_BC1RGBAUnorm;
    desc.width = desc.height = 7;
    copy = (doe_resource_texture_copy){.width = 7, .height = 7, .depth_or_layers = 1};
    assert(doe_resource_texture_copy_validate(32, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_OK && region.required_bytes == 32);
    assert(doe_resource_texture_copy_validate(32, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_WEBGPU, &region) == DOE_RESOURCE_LAYOUT);
    desc = texture_desc();
    desc.format = WGPUTextureFormat_Depth32FloatStencil8;
    copy = (doe_resource_texture_copy){.width = 8, .height = 8, .depth_or_layers = 1};
    assert(doe_resource_texture_copy_validate(512, &desc, &copy, DOE_RESOURCE_TEXTURE_TO_BUFFER,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_ASPECT);
    copy.aspect = WGPUTextureAspect_StencilOnly;
    assert(doe_resource_texture_copy_validate(64, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_OK && region.required_bytes == 64);
    copy.aspect = WGPUTextureAspect_DepthOnly;
    assert(doe_resource_texture_copy_validate(256, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_ASPECT);
    assert(doe_resource_texture_copy_validate(256, &desc, &copy, DOE_RESOURCE_TEXTURE_TO_BUFFER,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_OK && region.required_bytes == 256);
    desc = texture_desc();
    desc.width = desc.height = desc.depth_or_layers = UINT32_MAX;
    copy = (doe_resource_texture_copy){.bytes_per_row = UINT32_MAX - 3, .rows_per_image = UINT32_MAX - 1,
        .width = 1, .height = 1, .depth_or_layers = UINT32_MAX};
    assert(doe_resource_texture_copy_validate(UINT64_MAX, &desc, &copy, DOE_RESOURCE_BUFFER_TO_TEXTURE,
        DOE_RESOURCE_COPY_NATIVE, &region) == DOE_RESOURCE_RANGE);
}

static void test_view_lifetime(void)
{
    fixture f = fresh();
    native_token texture_token = {0}, view_token = {0};
    doe_resource_config c = config(&f);
    doe_resource_texture_desc desc = texture_desc();
    WGPUTextureFormat formats[] = {WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_RGBA8UnormSrgb};
    desc.view_formats = formats;
    desc.view_format_count = 2;
    desc.mip_levels = 4;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &texture_token), &texture) == DOE_RESOURCE_OK);
    formats[0] = WGPUTextureFormat_R32Uint;
    assert(doe_resource_texture_description(texture)->view_format_count == 1);
    doe_resource_view_desc view_desc = {.format = WGPUTextureFormat_RGBA8UnormSrgb, .base_mip = 1};
    doe_resource_view *view;
    doe_resource_view_desc resolved;
    assert(doe_resource_view_validate(texture, &view_desc, &resolved) == DOE_RESOURCE_OK);
    assert(resolved.base_mip == 1 && resolved.mip_count == 3);
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    assert(doe_resource_view_description(view)->mip_count == 3);
    doe_resource_texture_release(texture);
    assert(!texture_token.releases);
    doe_resource_view_release(view);
    assert(texture_token.releases == 1 && view_token.releases == 1);
    empty(&f);
}

static doe_resource_layout_entry storage_entry(uint32_t binding, bool dynamic)
{
    return (doe_resource_layout_entry){.binding = binding, .visibility = WGPUShaderStage_Compute,
        .kind = DOE_BIND_BUFFER, .buffer_type = WGPUBufferBindingType_Storage,
        .dynamic_offset = dynamic, .minimum_size = 4};
}

static void test_binding_layouts(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout_entry entries[] = {storage_entry(7, true), storage_entry(2, false)};
    doe_resource_layout *a, *b, *automatic;
    assert(doe_resource_layout_create(&c, entries, 2, false, &a) == DOE_RESOURCE_OK);
    doe_resource_layout_entry reverse[] = {entries[1], entries[0]};
    reverse[0].storage_format = WGPUTextureFormat_RGBA8Unorm;
    assert(doe_resource_layout_create(&c, reverse, 2, false, &b) == DOE_RESOURCE_OK);
    assert(doe_resource_layout_compatible(a, b));
    assert(doe_resource_layout_create(&c, entries, 2, true, &automatic) == DOE_RESOURCE_OK);
    assert(!doe_resource_layout_compatible(a, automatic));
    assert(doe_resource_layout_compatible(automatic, automatic));
    doe_resource_pipeline_layout *automatic_pipeline_layout;
    assert(doe_resource_pipeline_layout_create(&c, &automatic, 1, 0, &automatic_pipeline_layout) == DOE_RESOURCE_OK);
    native_token first_pipeline = {0}, second_pipeline = {0};
    doe_resource_pipeline *exclusive;
    uint32_t workgroup[] = {1, 1, 1};
    assert(doe_resource_pipeline_create(&c, automatic_pipeline_layout, native(&f, &first_pipeline),
        workgroup, &exclusive) == DOE_RESOURCE_OK);
    doe_resource_pipeline_release(exclusive);
    assert(doe_resource_pipeline_create(&c, automatic_pipeline_layout, native(&f, &second_pipeline),
        workgroup, &exclusive) == DOE_RESOURCE_LAYOUT);
    assert(first_pipeline.releases == 1 && second_pipeline.releases == 0);
    doe_resource_pipeline_layout_release(automatic_pipeline_layout);
    native_token token = {0};
    doe_resource_buffer *buf = buffer(&f, &token, 128, WGPUBufferUsage_Storage);
    doe_resource_binding values[] = {
        {.binding = 7, .kind = DOE_BIND_BUFFER, .resource.buffer = {buf, 16, 32}},
        {.binding = 2, .kind = DOE_BIND_BUFFER, .resource.buffer = {buf, 0, 16}}
    };
    doe_resource_binding_limits limits = {16, 16, 128, 128};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(a, values, 2, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_binding result[2];
    uint32_t dynamic = 32;
    assert(doe_resource_bind_group_resolve(group, &dynamic, 1, result, 2) == DOE_RESOURCE_OK);
    assert(result[0].binding == 2 && result[1].resource.buffer.offset == 48);
    dynamic = 96;
    assert(doe_resource_bind_group_resolve(group, &dynamic, 1, result, 2) == DOE_RESOURCE_RANGE);
    assert(result[1].resource.buffer.offset == 48);
    dynamic = 1;
    assert(doe_resource_bind_group_resolve(group, &dynamic, 1, result, 2) == DOE_RESOURCE_ALIGNMENT);
    assert(doe_resource_bind_group_resolve(group, NULL, 0, result, 2) == DOE_RESOURCE_LAYOUT);
    doe_resource_buffer_release(buf);
    doe_resource_layout_release(a);
    doe_resource_layout_release(b);
    doe_resource_layout_release(automatic);
    assert(!token.releases);
    doe_resource_bind_group_release(group);
    assert(token.releases == 1);
    empty(&f);
}

static void test_recorded_submission(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token source_token = {0}, destination_token = {0};
    doe_resource_buffer *source = buffer(&f, &source_token, 64, WGPUBufferUsage_CopySrc);
    doe_resource_buffer *destination = buffer(&f, &destination_token, 64, WGPUBufferUsage_CopyDst);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    doe_resource_buffer_copy copy = {source, destination, 0, 0, UINT64_MAX};
    assert(doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS) == DOE_RESOURCE_OK);
    doe_resource_buffer_release(source);
    doe_resource_buffer_release(destination);
    assert(!source_token.releases && !destination_token.releases);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_STATE);
    doe_resource_encoder_release(encoder);
    size_t count;
    const doe_resource_command *recorded = doe_resource_commands_borrow(commands, &count);
    assert(count == 1 && recorded[0].value.copy.size == 64);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(!source_token.releases && !destination_token.releases);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(source_token.releases == 1 && destination_token.releases == 1);
    empty(&f);
}

static doe_resource_commands *copy_commands(fixture *f, doe_resource_buffer *source,
    doe_resource_buffer *destination)
{
    doe_resource_config c = config(f);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    doe_resource_buffer_copy copy = {source, destination, 0, 0, UINT64_MAX};
    assert(doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    return commands;
}

/* Compute-program extension: claimed commands replay repeatedly, never return to
 * the single-use queue path, and keep dependencies until the program releases. */
static void test_program_replay(void)
{
    fixture f = fresh();
    native_token source_token = {0}, destination_token = {0};
    doe_resource_buffer *source = buffer(&f, &source_token, 64, WGPUBufferUsage_CopySrc);
    doe_resource_buffer *destination = buffer(&f, &destination_token, 64, WGPUBufferUsage_CopyDst);
    doe_resource_commands *commands = copy_commands(&f, source, destination);
    assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_STATE); /* Not claimed. */
    assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_claim_program(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_claim_program(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands); /* Public handle gone; the program keeps it. */
    for (int run = 0; run < 3; run++) {
        assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_OK);
        assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_STATE);
        assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
        doe_resource_status terminal = run == 1 ? DOE_RESOURCE_EXECUTION_FAILED : DOE_RESOURCE_OK;
        assert(doe_resource_commands_settle_replay(commands, terminal) == terminal);
        assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_STATE);
    }
    doe_resource_buffer_destroy(source); /* A destroyed dependency stops later replays. */
    assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_buffer_release(source);
    doe_resource_buffer_release(destination);
    assert(!source_token.releases && !destination_token.releases);
    assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_OK);
    assert(source_token.releases == 1 && destination_token.releases == 1);

    /* Program released while a replay is unresolved: the replay's reference
     * keeps commands and dependencies until it settles. */
    native_token other_token = {0}, sink_token = {0};
    doe_resource_buffer *other = buffer(&f, &other_token, 64, WGPUBufferUsage_CopySrc);
    doe_resource_buffer *sink = buffer(&f, &sink_token, 64, WGPUBufferUsage_CopyDst);
    commands = copy_commands(&f, other, sink);
    assert(doe_resource_commands_claim_program(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_begin_replay(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_release_program(commands) == DOE_RESOURCE_OK);
    doe_resource_buffer_release(other);
    doe_resource_buffer_release(sink);
    assert(!other_token.releases && !sink_token.releases);
    assert(doe_resource_commands_settle_replay(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(other_token.releases == 1 && sink_token.releases == 1);

    /* Settled single-use commands and unavailable dependencies cannot be claimed. */
    native_token third_token = {0}, fourth_token = {0};
    doe_resource_buffer *third = buffer(&f, &third_token, 64, WGPUBufferUsage_CopySrc);
    doe_resource_buffer *fourth = buffer(&f, &fourth_token, 64, WGPUBufferUsage_CopyDst);
    commands = copy_commands(&f, third, fourth);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_claim_program(commands) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands);
    commands = copy_commands(&f, third, fourth);
    doe_resource_buffer_destroy(fourth);
    assert(doe_resource_commands_claim_program(commands) == DOE_RESOURCE_UNAVAILABLE);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_commands_release(commands);
    doe_resource_buffer_release(third);
    doe_resource_buffer_release(fourth);
    empty(&f);
}

static void test_failed_publication(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0};
    doe_resource_buffer *buf = buffer(&f, &token, 64, WGPUBufferUsage_CopyDst);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    f.fail_at = f.allocations;
    assert(doe_resource_encoder_clear_buffer(encoder, buf, 0, 16) == DOE_RESOURCE_OUT_OF_MEMORY);
    f.fail_at = SIZE_MAX;
    assert(doe_resource_encoder_clear_buffer(encoder, buf, 0, 16) == DOE_RESOURCE_OUT_OF_MEMORY);
    doe_resource_commands *commands = (doe_resource_commands *)(void *)&token;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(commands == (doe_resource_commands *)(void *)&token);
    doe_resource_buffer_release(buf);
    assert(token.releases == 1);
    doe_resource_encoder_release(encoder);
    empty(&f);

    f = fresh();
    c = config(&f);
    token = (native_token){0};
    doe_resource_buffer_desc desc = {16, WGPUBufferUsage_CopySrc, false};
    f.fail_at = 0;
    buf = (doe_resource_buffer *)(void *)&token;
    assert(doe_resource_buffer_create(&c, &desc, native(&f, &token), &buf) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(buf == (doe_resource_buffer *)(void *)&token && !token.releases);
    empty(&f);
}

static void test_dispatch_lifetime(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token buffer_token = {0}, pipeline_token = {0};
    doe_resource_buffer *buf = buffer(&f, &buffer_token, 64, WGPUBufferUsage_Storage);
    doe_resource_layout_entry entry = storage_entry(0, false);
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.binding = 0, .kind = DOE_BIND_BUFFER, .resource.buffer = {buf, 0, UINT64_MAX}};
    doe_resource_binding_limits limits = {16, 16, 64, 64};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *pl;
    assert(doe_resource_pipeline_layout_create(&c, &layout, 1, 0, &pl) == DOE_RESOURCE_OK);
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[] = {64, 1, 1};
    assert(doe_resource_pipeline_create(&c, pl, native(&f, &pipeline_token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_dispatch(encoder, pipeline, &group, 1, 1, 1, 1) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(pl);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_buffer_release(buf);
    assert(!buffer_token.releases && !pipeline_token.releases);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands);
    assert(buffer_token.releases == 1 && pipeline_token.releases == 1);
    empty(&f);
}

static void test_dynamic_dispatch(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token buffer_token = {0}, pipeline_token = {0};
    doe_resource_buffer *buf = buffer(&f, &buffer_token, 128, WGPUBufferUsage_Storage);
    doe_resource_layout_entry entry = storage_entry(3, true);
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.binding = 3, .kind = DOE_BIND_BUFFER, .resource.buffer = {buf, 16, 32}};
    doe_resource_binding_limits limits = {16, 16, 128, 128};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *pl;
    assert(doe_resource_pipeline_layout_create(&c, &layout, 1, 0, &pl) == DOE_RESOURCE_OK);
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[] = {64, 1, 1};
    assert(doe_resource_pipeline_create(&c, pl, native(&f, &pipeline_token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder;
    uint32_t offset = 32;
    doe_resource_dispatch_group bound = {group, &offset, 1};
    /* Failure at snapshot or command-array acquisition publishes no ownership. */
    for (size_t step = 0; step < 2; step++) {
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        size_t before = f.live;
        f.fail_at = f.allocations + step;
        assert(doe_resource_encoder_dispatch_groups(encoder, pipeline, &bound, 1, 1, 1, 1) == DOE_RESOURCE_OUT_OF_MEMORY);
        assert(f.live == before);
        f.fail_at = SIZE_MAX;
        doe_resource_encoder_release(encoder);
    }
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_dispatch_groups(encoder, pipeline, &bound, 1, 1, 1, 1) == DOE_RESOURCE_OK);
    offset = 96;
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    size_t count;
    const doe_resource_command *recorded = doe_resource_commands_borrow(commands, &count);
    assert(count == 1 && recorded[0].value.dispatch.dynamic_offsets->values[0][0] == 32);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(pl);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_buffer_release(buf);
    empty(&f);
}

static void test_rejected_dependencies(void)
{
    fixture f = fresh(), other = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0}, foreign_token = {0};
    doe_resource_buffer *buf = buffer(&f, &token, 64, WGPUBufferUsage_CopyDst);
    doe_resource_buffer *foreign = buffer(&other, &foreign_token, 64, WGPUBufferUsage_CopyDst);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    size_t before = f.allocations;
    assert(doe_resource_encoder_clear_buffer(encoder, foreign, 0, 4) == DOE_RESOURCE_DEVICE_MISMATCH);
    assert(f.allocations == before);
    assert(doe_resource_encoder_clear_buffer(encoder, buf, 0, 4) == DOE_RESOURCE_DEVICE_MISMATCH);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_clear_buffer(encoder, buf, 0, 4) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_buffer_destroy(buf);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_buffer_release(buf);
    assert(!token.releases);
    doe_resource_commands_release(commands);
    doe_resource_buffer_release(foreign);
    assert(token.releases == 1 && foreign_token.releases == 1);
    empty(&f);
    empty(&other);
}

static void test_sampler_and_texture_group(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token texture_token = {0}, view_token = {0}, sampler_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &texture_token), &texture) == DOE_RESOURCE_OK);
    doe_resource_view_desc view_desc = {0};
    doe_resource_view *view;
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    doe_resource_sampler *sampler;
    WGPUSamplerDescriptor normalized = WGPU_SAMPLER_DESCRIPTOR_INIT;
    normalized.lodMaxClamp = 19;
    sampler_desc.lodMaxClamp = -1;
    assert(doe_resource_sampler_create(&c, &sampler_desc, native(&f, &sampler_token), &sampler) == DOE_RESOURCE_RANGE);
    assert(doe_resource_sampler_validate(&sampler_desc, &normalized) == DOE_RESOURCE_RANGE);
    assert(normalized.lodMaxClamp == 19);
    assert(!sampler_token.releases);
    sampler_desc.lodMaxClamp = 32;
    assert(doe_resource_sampler_validate(&sampler_desc, &normalized) == DOE_RESOURCE_OK);
    assert(normalized.lodMaxClamp == 32 && normalized.label.data == NULL);
    assert(doe_resource_sampler_create(&c, &sampler_desc, native(&f, &sampler_token), &sampler) == DOE_RESOURCE_OK);
    doe_resource_layout_entry entries[] = {
        {.binding = 0, .visibility = WGPUShaderStage_Compute, .kind = DOE_BIND_TEXTURE,
         .sample_type = WGPUTextureSampleType_Float, .view_dimension = WGPUTextureViewDimension_2D},
        {.binding = 1, .visibility = WGPUShaderStage_Compute, .kind = DOE_BIND_SAMPLER,
         .sampler_type = WGPUSamplerBindingType_NonFiltering}
    };
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, entries, 2, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding bindings[] = {
        {.binding = 0, .kind = DOE_BIND_TEXTURE, .resource.view = view},
        {.binding = 1, .kind = DOE_BIND_SAMPLER, .resource.sampler = sampler}
    };
    doe_resource_binding_limits binding_limits = {16, 16, 1024, 1024};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, bindings, 2, &binding_limits, &group) == DOE_RESOURCE_OK);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    doe_resource_sampler_release(sampler);
    doe_resource_layout_release(layout);
    assert(!texture_token.releases && !view_token.releases && !sampler_token.releases);
    doe_resource_bind_group_release(group);
    assert(texture_token.releases == 1 && view_token.releases == 1 && sampler_token.releases == 1);
    empty(&f);
}

static void test_label_reuse(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_label_config labels = {.abi_version = DOE_LABELS_ABI_VERSION,
        .allocator = {&f, NULL, allocate, deallocate}};
    assert(doe_label_store_create(&labels, &c.labels) == DOE_LABEL_OK);
    native_token token = {0};
    doe_resource_buffer_desc desc = {16, WGPUBufferUsage_CopySrc, false};
    doe_resource_buffer *buf;
    assert(doe_resource_buffer_create(&c, &desc, native(&f, &token), &buf) == DOE_RESOURCE_OK);
    const void *key = buf;
    assert(doe_label_store_set(c.labels, key, (const unsigned char *)"buffer", 6) == DOE_LABEL_OK);
    doe_resource_buffer_release(buf);
    doe_label_view view;
    assert(doe_label_store_get(c.labels, key, &view) == DOE_LABEL_OK && view.length == 0);
    assert(doe_label_store_destroy(c.labels) == DOE_LABEL_OK);
    empty(&f);
}

static void *retain_worker(void *argument)
{
    doe_resource_buffer *buf = argument;
    for (size_t i = 0; i < 10000; i++) {
        doe_resource_buffer_retain(buf);
        doe_resource_buffer_release(buf);
    }
    return NULL;
}
static void test_atomic_references(void)
{
    fixture f = fresh();
    native_token token = {0};
    doe_resource_buffer *buf = buffer(&f, &token, 64, WGPUBufferUsage_Storage);
    pthread_t workers[4];
    for (size_t i = 0; i < 4; i++) assert(pthread_create(&workers[i], NULL, retain_worker, buf) == 0);
    for (size_t i = 0; i < 4; i++) assert(pthread_join(workers[i], NULL) == 0);
    assert(!token.releases);
    doe_resource_buffer_release(buf);
    assert(token.releases == 1);
    empty(&f);
}

int main(void)
{
    test_copy_admission();
    test_mapping();
    test_mapping_completion_tokens();
    test_texture_layout();
    test_view_lifetime();
    test_binding_layouts();
    test_recorded_submission();
    test_program_replay();
    test_failed_publication();
    test_dispatch_lifetime();
    test_dynamic_dispatch();
    test_rejected_dependencies();
    test_sampler_and_texture_group();
    test_label_reuse();
    test_atomic_references();
    puts("resource owner: 15 host suites passed; no GPU execution claimed");
    return 0;
}
