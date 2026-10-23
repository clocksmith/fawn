/* SPDX-License-Identifier: MIT
 * Native-call ownership controls only: no pixel or shader execution model. */
#include "mock_native.h"
#include "../src/texture_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static doe_mtl_context *context_open(mock_native *mock) {
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(mock), &context, NULL) == DOE_MTL_OK);
    return context;
}
static doe_mtl_buffer *buffer_new(doe_mtl_context *context, uint64_t size, doe_mtl_memory memory) {
    doe_mtl_buffer *buffer = NULL;
    assert(doe_mtl_buffer_create(context, size, memory, &buffer, NULL) == DOE_MTL_OK);
    return buffer;
}
static doe_mtl_commands *commands_new(doe_mtl_context *context) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}
static doe_mtl_texture_desc image_desc(void) {
    const doe_mtl_texture_desc desc = {DOE_MTL_TEXTURE_2D_ARRAY, DOE_MTL_RGBA8_UNORM,
        4, 2, 2, 2, DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST |
        DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_STORAGE, true, 1};
    return desc;
}
static doe_mtl_texture *image_new(doe_mtl_context *context, doe_mtl_texture_desc desc) {
    doe_mtl_texture *texture = NULL;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_OK);
    return texture;
}
static doe_mtl_texture_view_desc view_desc(void) {
    const doe_mtl_texture_view_desc desc = {DOE_MTL_TEXTURE_2D_ARRAY, DOE_MTL_RGBA8_UNORM,
        0, 1, 0, 2, DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA};
    return desc;
}
static doe_mtl_sampler_desc sampler_desc(void) {
    const doe_mtl_sampler_desc desc = {DOE_MTL_FILTER_LINEAR, DOE_MTL_FILTER_LINEAR,
        DOE_MTL_FILTER_LINEAR, DOE_MTL_ADDRESS_CLAMP, DOE_MTL_ADDRESS_REPEAT, DOE_MTL_ADDRESS_MIRROR,
        0.25f, 4.5f, 0, 4};
    return desc;
}
static doe_mtl_pipeline *pipeline_new(doe_mtl_context *context) {
    const doe_mtl_pipeline_desc desc = {"kernel", 6, "main", {2, 1, 1}, UINT32_MAX};
    doe_mtl_pipeline *pipeline = NULL;
    assert(doe_mtl_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_OK);
    return pipeline;
}
static void complete(mock_native *mock, doe_mtl_commands *commands) {
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
}
static void factory_rollback(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture_desc desc = image_desc();
    doe_mtl_texture *texture = (void *)&mock, *valid;
    doe_mtl_texture_view *view = (void *)&mock;
    doe_mtl_sampler *sampler = (void *)&mock;
    doe_mtl_texture_view_desc vd = view_desc();
    doe_mtl_sampler_desc sd = sampler_desc();
    unsigned live = mock.live;
    mock.fail = MOCK_TEXTURE;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(texture == (void *)&mock && mock.live == live && context->references == 1);
    mock.fail = MOCK_NONE;
    valid = image_new(context, desc);
    live = mock.live;
    mock.fail = MOCK_VIEW;
    assert(doe_mtl_texture_view_create(valid, &vd, &view, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(view == (void *)&mock && mock.live == live && valid->references == 1);
    mock.fail = MOCK_SAMPLER;
    assert(doe_mtl_sampler_create(context, &sd, &sampler, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(sampler == (void *)&mock && mock.live == live);
    doe_mtl_texture_release(valid);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void descriptor_validation(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture_desc desc = image_desc();
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_view_desc vd = view_desc();
    doe_mtl_texture_view *view = NULL;
    doe_mtl_sampler_desc sd = sampler_desc();
    doe_mtl_sampler *sampler = NULL;
    doe_mtl_format_info format;
    assert(doe_mtl_texture_format_info(DOE_MTL_BC1_RGBA, &format) == DOE_MTL_OK);
    assert(format.block_width == 4 && format.block_bytes == 8 && format.compressed);
    assert(doe_mtl_texture_format_info((doe_mtl_format)9999, &format) == DOE_MTL_UNSUPPORTED);
    desc.type = DOE_MTL_TEXTURE_CUBE;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_RANGE);
    desc = image_desc(); desc.mip_levels = 32;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_RANGE);
    desc = image_desc(); desc.format = DOE_MTL_RGBA8_SRGB;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_UNSUPPORTED);
    desc = image_desc(); desc.allow_format_views = false;
    texture = image_new(context, desc);
    vd.format = DOE_MTL_RGBA8_SRGB;
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_UNSUPPORTED);
    vd = view_desc(); vd.base_layer = UINT32_MAX;
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_RANGE);
    vd = view_desc(); vd.red = (doe_mtl_swizzle)-1;
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_INVALID_ARGUMENT);
    sd.max_lod = NAN;
    assert(doe_mtl_sampler_create(context, &sd, &sampler, NULL) == DOE_MTL_INVALID_ARGUMENT);
    sd = sampler_desc(); sd.min_filter = DOE_MTL_FILTER_NEAREST;
    assert(doe_mtl_sampler_create(context, &sd, &sampler, NULL) == DOE_MTL_INVALID_ARGUMENT);
    sd = sampler_desc(); sd.compare = 8;
    assert(doe_mtl_sampler_create(context, &sd, &sampler, NULL) == DOE_MTL_INVALID_ARGUMENT);
    doe_mtl_texture_release(texture);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void mixed_dispatch_lifetime(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture *texture = image_new(context, image_desc());
    doe_mtl_texture_view *view = NULL;
    doe_mtl_sampler *sampler = NULL;
    doe_mtl_texture_view_desc vd = view_desc();
    doe_mtl_sampler_desc sd = sampler_desc();
    doe_mtl_buffer *buffer = buffer_new(context, 1024, DOE_MTL_SHARED);
    doe_mtl_pipeline *pipeline = pipeline_new(context);
    doe_mtl_commands *commands = commands_new(context);
    doe_mtl_texture_region region = {0, 0, 0, 0, 4, 2, 2};
    doe_mtl_texture_binding tb;
    doe_mtl_sampler_binding sb;
    doe_mtl_buffer_binding bb = {3, buffer, 0, 16};
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_OK);
    assert(doe_mtl_sampler_create(context, &sd, &sampler, NULL) == DOE_MTL_OK);
    tb = (doe_mtl_texture_binding){127, view, true};
    sb = (doe_mtl_sampler_binding){15, sampler};
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    assert(mock.image_to.bytes_per_row == 256 && mock.image_to.bytes_per_image == 512);
    assert(doe_mtl_commands_dispatch_resources(commands, pipeline, &bb, 1,
        &tb, 1, &sb, 1, (doe_mtl_dimensions){2, 3, 1}, 2, NULL) == DOE_MTL_OK);
    assert(mock.resources.textures[127] == view->native && mock.resources.texture_count == 128);
    assert(mock.resources.samplers[15] == sampler->native && mock.resources.sampler_count == 16);
    assert(mock.bindings[3].buffer == buffer->native && mock.repeat == 2);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    doe_mtl_texture_release(texture); doe_mtl_texture_view_release(view); doe_mtl_sampler_release(sampler);
    doe_mtl_buffer_release(buffer); doe_mtl_pipeline_release(pipeline); doe_mtl_context_release(context);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && mock.closes == 0);
    mock_result(&mock, 0, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN && mock.closes == 0);
    mock_result(&mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&mock);
}
static void mip_layer_initialization(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture *texture = image_new(context, image_desc());
    doe_mtl_buffer *buffer = buffer_new(context, 1024, DOE_MTL_SHARED);
    doe_mtl_commands *commands = commands_new(context);
    doe_mtl_texture_region region = {0, 0, 0, 1, 4, 2, 1};
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    assert(!texture->initialized[1]);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(!texture->initialized[1] && texture->owner == NULL);
    commands = commands_new(context);
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(!texture->initialized[0] && texture->initialized[1] && !texture->initialized[3]);
    commands = commands_new(context); region.mip = 1; region.width = 2; region.height = 1;
    assert(doe_mtl_commands_read_texture(commands, texture, region, buffer,
        (doe_mtl_texture_buffer_layout){0}, NULL) == DOE_MTL_UNINITIALIZED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); region.mip = 0; region.width = 1;
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    commands = commands_new(context); region.z = 0;
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_UNINITIALIZED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_buffer_release(buffer); doe_mtl_texture_release(texture); doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void copies_and_padding(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture *source = image_new(context, image_desc());
    doe_mtl_texture *destination = image_new(context, image_desc());
    doe_mtl_buffer *staging = buffer_new(context, 1024, DOE_MTL_SHARED);
    doe_mtl_buffer *readback = buffer_new(context, 784, DOE_MTL_PRIVATE);
    doe_mtl_commands *commands = commands_new(context);
    doe_mtl_texture_region region = {0, 0, 0, 0, 4, 2, 2};
    assert(doe_mtl_commands_upload_texture(commands, staging,
        (doe_mtl_texture_buffer_layout){0}, source, region, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy_texture(commands, source, region, destination, region, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_read_texture(commands, destination, region, readback,
        (doe_mtl_texture_buffer_layout){0}, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(destination->initialized[0] && destination->initialized[1] && !readback->initialized);
    commands = commands_new(context);
    assert(doe_mtl_commands_copy_texture(commands, source, region, source, region, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context);
    assert(doe_mtl_commands_read_texture(commands, source, region, staging,
        (doe_mtl_texture_buffer_layout){4, 16, 0}, NULL) == DOE_MTL_ALIGNMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context);
    assert(doe_mtl_commands_read_texture(commands, source, region, staging,
        (doe_mtl_texture_buffer_layout){UINT64_MAX, 256, UINT32_MAX}, NULL) == DOE_MTL_ALIGNMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_buffer_release(staging); doe_mtl_buffer_release(readback);
    doe_mtl_texture_release(source); doe_mtl_texture_release(destination); doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void compressed_and_volume(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture_desc desc = image_desc();
    doe_mtl_texture *texture;
    doe_mtl_buffer *buffer = buffer_new(context, 2048, DOE_MTL_SHARED);
    doe_mtl_commands *commands;
    doe_mtl_texture_region region = {0, 0, 0, 0, 7, 5, 1};
    desc.width = 7; desc.height = 5; desc.format = DOE_MTL_BC1_RGBA;
    desc.usage = DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST | DOE_MTL_TEXTURE_SAMPLED;
    texture = image_new(context, desc);
    commands = commands_new(context);
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    assert(mock.image_to.bytes_per_row == 256 && mock.image_to.bytes_per_image == 512);
    complete(&mock, commands);
    commands = commands_new(context); region.x = 1; region.width = 4;
    assert(doe_mtl_commands_read_texture(commands, texture, region, buffer,
        (doe_mtl_texture_buffer_layout){0}, NULL) == DOE_MTL_ALIGNMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    desc = image_desc(); desc.type = DOE_MTL_TEXTURE_3D;
    texture = image_new(context, desc);
    commands = commands_new(context); region = (doe_mtl_texture_region){0, 0, 0, 0, 4, 2, 2};
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    assert(mock.image_to.volume);
    complete(&mock, commands);
    assert(texture->subresource_count == 2 && texture->initialized[0] && !texture->initialized[1]);
    doe_mtl_texture_release(texture); doe_mtl_buffer_release(buffer); doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void failures_and_busy(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture *texture = image_new(context, image_desc());
    doe_mtl_buffer *buffer = buffer_new(context, 1024, DOE_MTL_SHARED);
    doe_mtl_commands *commands = commands_new(context), *other;
    doe_mtl_texture_region region = {0, 0, 0, 0, 4, 2, 2};
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    other = commands_new(context);
    assert(doe_mtl_commands_copy_texture(other, texture, region, texture, region, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_release(other) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); mock.fail = MOCK_UPLOAD_IMAGE;
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_NATIVE_FAILURE && mock.commits == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && !texture->poisoned);
    mock.fail = MOCK_NONE; commands = commands_new(context);
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&mock, 5, -88);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(texture->poisoned && buffer->poisoned);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_texture_release(texture); doe_mtl_buffer_release(buffer); doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void image_binding_validation(void) {
    mock_native mock, foreign;
    doe_mtl_context *context = context_open(&mock), *other_context = context_open(&foreign);
    doe_mtl_texture *texture = image_new(context, image_desc());
    doe_mtl_texture *other = image_new(other_context, image_desc());
    doe_mtl_texture_view_desc vd = view_desc();
    doe_mtl_texture_view *view = NULL;
    doe_mtl_sampler_desc sd = sampler_desc();
    doe_mtl_sampler *sampler = NULL;
    doe_mtl_pipeline *pipeline = pipeline_new(context);
    doe_mtl_buffer *buffer = buffer_new(context, 1024, DOE_MTL_SHARED);
    doe_mtl_commands *commands = commands_new(context);
    doe_mtl_texture_region region = {0, 0, 0, 0, 4, 2, 2};
    doe_mtl_texture_binding bindings[2];
    doe_mtl_sampler_binding sb;
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, other, region, NULL) == DOE_MTL_DEVICE_MISMATCH);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && mock.uploads == 0);
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_OK);
    bindings[0] = (doe_mtl_texture_binding){0, view, false}; bindings[1] = bindings[0];
    commands = commands_new(context);
    assert(doe_mtl_commands_dispatch_resources(commands, pipeline, NULL, 0, bindings, 1, NULL, 0,
        (doe_mtl_dimensions){1, 1, 1}, 1, NULL) == DOE_MTL_UNINITIALIZED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && mock.dispatches == 0);
    commands = commands_new(context);
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    commands = commands_new(context);
    assert(doe_mtl_commands_dispatch_resources(commands, pipeline, NULL, 0, bindings, 2, NULL, 0,
        (doe_mtl_dimensions){1, 1, 1}, 1, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(doe_mtl_sampler_create(other_context, &sd, &sampler, NULL) == DOE_MTL_OK);
    sb = (doe_mtl_sampler_binding){0, sampler}; commands = commands_new(context);
    assert(doe_mtl_commands_dispatch_resources(commands, pipeline, NULL, 0, bindings, 1, &sb, 1,
        (doe_mtl_dimensions){1, 1, 1}, 1, NULL) == DOE_MTL_DEVICE_MISMATCH);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    view->references = UINT32_MAX; commands = commands_new(context);
    assert(doe_mtl_commands_dispatch_resources(commands, pipeline, NULL, 0, bindings, 1, NULL, 0,
        (doe_mtl_dimensions){1, 1, 1}, 1, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && texture->owner == NULL);
    view->references = 1;
    assert(mock.dispatches == 0);
    doe_mtl_sampler_release(sampler); doe_mtl_texture_release(other); doe_mtl_context_release(other_context);
    doe_mtl_texture_view_release(view); doe_mtl_texture_release(texture);
    doe_mtl_pipeline_release(pipeline); doe_mtl_buffer_release(buffer); doe_mtl_context_release(context);
    mock_assert_closed(&mock); mock_assert_closed(&foreign);
}
static void transfer_record_faults(void) {
    mock_native mock;
    doe_mtl_context *context = context_open(&mock);
    doe_mtl_texture *texture = image_new(context, image_desc());
    doe_mtl_texture *destination = image_new(context, image_desc());
    doe_mtl_buffer *buffer = buffer_new(context, 1024, DOE_MTL_SHARED);
    doe_mtl_commands *commands = commands_new(context);
    doe_mtl_texture_region region = {0, 0, 0, 0, 4, 2, 2};
    assert(doe_mtl_commands_upload_texture(commands, buffer,
        (doe_mtl_texture_buffer_layout){0}, texture, region, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    commands = commands_new(context); mock.fail = MOCK_READ_IMAGE;
    assert(doe_mtl_commands_read_texture(commands, texture, region, buffer,
        (doe_mtl_texture_buffer_layout){0}, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); mock.fail = MOCK_COPY_IMAGE;
    assert(doe_mtl_commands_copy_texture(commands, texture, region, destination, region, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && !destination->initialized[0]);
    assert(!texture->poisoned && mock.commits == 1);
    doe_mtl_texture_release(texture); doe_mtl_texture_release(destination);
    doe_mtl_buffer_release(buffer); doe_mtl_context_release(context); mock_assert_closed(&mock);
}
int main(void) {
    factory_rollback(); descriptor_validation(); mixed_dispatch_lifetime();
    mip_layer_initialization(); copies_and_padding(); compressed_and_volume(); failures_and_busy();
    image_binding_validation(); transfer_record_faults();
    puts("Metal image ownership controls: 9 groups passed; mocked native calls, no pixel/GPU execution");
    return 0;
}
