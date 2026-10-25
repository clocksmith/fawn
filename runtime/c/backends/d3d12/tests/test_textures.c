/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *open_context(void) {
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    return context;
}

static doe_dx_texture *image(doe_dx_context *context, uint32_t width, uint32_t height,
    doe_dx_format format, uint32_t usage) {
    doe_dx_texture_desc desc = {width, height, format, usage};
    doe_dx_texture *texture = NULL;
    assert(doe_dx_texture_create(context, &desc, &texture, NULL) == DOE_DX_OK);
    return texture;
}

static doe_dx_buffer *buffer(doe_dx_context *context, uint64_t size, doe_dx_memory memory) {
    doe_dx_buffer *buffer = NULL;
    assert(doe_dx_buffer_create(context, size, memory, &buffer, NULL) == DOE_DX_OK);
    return buffer;
}

static doe_dx_commands *commands(doe_dx_context *context) {
    doe_dx_commands *commands = NULL;
    assert(doe_dx_commands_create(context, &commands, NULL) == DOE_DX_OK);
    return commands;
}

static void close_context(doe_dx_context *context) {
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void footprint_and_construction(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *texture = image(context, 3, 2, DOE_DX_R32_UINT, 15);
    doe_dx_texture_footprint footprint = {0};
    assert(doe_dx_texture_copy_footprint(texture, (doe_dx_texture_layout){0, 0}, &footprint) == DOE_DX_OK);
    assert(footprint.row_bytes == 12 && footprint.row_pitch == 256 && footprint.rows == 2);
    assert(footprint.span == 268 && footprint.offset == 0);
    assert(doe_dx_texture_copy_footprint(texture, (doe_dx_texture_layout){512, 512}, &footprint) == DOE_DX_OK);
    assert(footprint.span == 524 && footprint.offset == 512);
    doe_dx_texture_footprint saved = footprint;
    assert(doe_dx_texture_copy_footprint(texture, (doe_dx_texture_layout){1, 0}, &footprint) == DOE_DX_ALIGNMENT);
    assert(memcmp(&saved, &footprint, sizeof(saved)) == 0);
    assert(doe_dx_texture_copy_footprint(texture, (doe_dx_texture_layout){0, 260}, &footprint) == DOE_DX_ALIGNMENT);
    assert(doe_dx_texture_copy_footprint(texture, (doe_dx_texture_layout){UINT64_MAX - 511, 512}, &footprint) == DOE_DX_RANGE);
    doe_dx_texture_desc desc = {3, 2, DOE_DX_R32_UINT, 15};
    doe_dx_texture *out = NULL;
    mock.failure = MOCK_TEXTURE;
    assert(doe_dx_texture_create(context, &desc, &out, NULL) == DOE_DX_NATIVE_FAILURE && !out);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_texture_create(context, &desc, &out, NULL) == DOE_DX_OUT_OF_MEMORY && !out);
    mock.allocation_failure = 0;
    desc.format = (doe_dx_format)99;
    assert(doe_dx_texture_create(context, &desc, &out, NULL) == DOE_DX_UNSUPPORTED && !out);
    desc.format = DOE_DX_R32_UINT;
    desc.width = 16385;
    assert(doe_dx_texture_create(context, &desc, &out, NULL) == DOE_DX_RANGE && !out);
    desc.width = 0;
    assert(doe_dx_texture_create(context, &desc, &out, NULL) == DOE_DX_INVALID_ARGUMENT && !out);
    doe_dx_texture_release(texture);
    close_context(context);
}

static doe_dx_pipeline *texture_pipeline(doe_dx_context *context, doe_dx_format format) {
    unsigned char bytecode = 1;
    doe_dx_texture_binding_layout textures[2] = {
        {0, 2, format, DOE_DX_READ_ONLY, false}, {1, 2, format, DOE_DX_READ_WRITE, false}
    };
    doe_dx_static_sampler sampler = {3, 2, DOE_DX_NEAREST, DOE_DX_CLAMP, DOE_DX_REPEAT};
    doe_dx_resource_pipeline_desc desc = {{&bytecode, 1, NULL, 0}, textures, 2, &sampler, 1};
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_OK);
    assert(mock.pipeline_desc.sampler_count == 1 && mock.samplers[0].shader_register == 3);
    assert(mock.texture_layouts[0].register_space == 2 && mock.texture_layouts[1].shader_register == 1);
    return pipeline;
}

static void padded_readback_and_early_release(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *source = image(context, 3, 2, DOE_DX_R32_FLOAT, 15);
    doe_dx_texture *destination = image(context, 3, 2, DOE_DX_R32_FLOAT, 15);
    doe_dx_buffer *upload = buffer(context, 268, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(context, 268, DOE_DX_READBACK);
    doe_dx_commands *work = commands(context);
    doe_dx_pipeline *pipeline = texture_pipeline(context, DOE_DX_R32_FLOAT);
    doe_dx_texture_layout layout = {0};
    assert(doe_dx_commands_upload_texture(work, upload, layout, source, NULL) == DOE_DX_OK);
    assert(mock.upload && mock.transfer_texture == source->native && mock.transfer_buffer == upload->native);
    assert(mock.footprint.row_pitch == 256 && mock.footprint.span == 268);
    assert(doe_dx_commands_copy_texture(work, source, destination, NULL) == DOE_DX_OK);
    assert(mock.texture_source == source->native && mock.texture_destination == destination->native);
    assert(doe_dx_commands_read_texture(work, destination, readback, layout, NULL) == DOE_DX_UNINITIALIZED);
    assert(mock.texture_transfers == 1 && readback->owner == NULL);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 268, NULL) == DOE_DX_OK);
    doe_dx_texture *textures[2] = {source, destination};
    doe_dx_compute_resources resources = {NULL, 0, textures, 2};
    mock.failure = MOCK_DISPATCH;
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 3, 2, 1, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!work->pipeline_count && !mock.dispatches);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 3, 2, 1, NULL) == DOE_DX_OK);
    assert(mock.texture_count == 2 && mock.texture_bindings[0] == source->native);
    assert(mock.texture_bindings[1] == destination->native);
    assert(doe_dx_commands_read_texture(work, destination, readback, layout, NULL) == DOE_DX_OK);
    assert(!mock.upload && mock.transfer_texture == destination->native);
    assert(!source->initialized && !destination->initialized && !readback->initialized);
    doe_dx_texture_release(source);
    doe_dx_texture_release(destination);
    doe_dx_buffer_release(upload);
    doe_dx_pipeline_release(pipeline);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(work);
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.textures == 2 && mock.buffers == 2 && mock.pipelines == 1);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_PENDING);
    mock.completed = 1;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
    assert(!mock.textures && mock.buffers == 1 && !mock.pipelines);
    unsigned char bytes[268];
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_OK);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void cancellation_and_unpadded_readback(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *texture = image(context, 64, 2, DOE_DX_RGBA8_UINT, 15);
    doe_dx_buffer *upload = buffer(context, 512, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(context, 512, DOE_DX_READBACK);
    doe_dx_commands *work = commands(context);
    doe_dx_commands *other = commands(context);
    doe_dx_texture_layout layout = {0};
    assert(doe_dx_commands_upload_texture(work, upload, layout, texture, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_read_texture(other, texture, readback, layout, NULL) == DOE_DX_BUSY);
    doe_dx_commands_release(work);
    assert(!texture->initialized && texture->owner == NULL);
    assert(doe_dx_commands_read_texture(other, texture, readback, layout, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_upload_texture(other, upload, layout, texture, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_read_texture(other, texture, readback, layout, NULL) == DOE_DX_OK);
    assert(!readback->initialized);
    assert(doe_dx_commands_submit(other, NULL) == DOE_DX_OK);
    mock.completed = 1;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
    assert(texture->initialized && readback->initialized);
    doe_dx_commands_release(other);
    doe_dx_texture_release(texture);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    close_context(context);
}

static void binding_layout_rejections(void) {
    doe_dx_context *context = open_context();
    unsigned char bytecode = 1;
    doe_dx_binding_layout buffer_layout = {0, 0, 4, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED};
    doe_dx_texture_binding_layout texture_layout = {0, 0, DOE_DX_R32_UINT, DOE_DX_READ_ONLY, false};
    doe_dx_resource_pipeline_desc desc = {{&bytecode, 1, &buffer_layout, 1}, &texture_layout, 1, NULL, 0};
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(!pipeline && !mock.pipelines);
    texture_layout.shader_register = 1;
    doe_dx_static_sampler samplers[2] = {
        {0, 0, DOE_DX_LINEAR, DOE_DX_CLAMP, DOE_DX_CLAMP},
        {0, 0, DOE_DX_LINEAR, DOE_DX_CLAMP, DOE_DX_CLAMP}
    };
    desc.samplers = samplers;
    desc.sampler_count = 2;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_INVALID_ARGUMENT);
    desc.sampler_count = 1;
    samplers[0].address_u = (doe_dx_address)99;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_INVALID_ARGUMENT);
    samplers[0].address_u = DOE_DX_MIRROR;
    desc.texture_count = DOE_DX_MAX_BINDINGS;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_RANGE);
    desc.texture_count = 1;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_OK);
    doe_dx_pipeline_release(pipeline);
    close_context(context);
}

static void texture_binding_rejections(void) {
    doe_dx_context *context = open_context();
    doe_dx_context *foreign_context = open_context();
    doe_dx_texture *source = image(context, 3, 1, DOE_DX_R32_UINT, 15);
    doe_dx_texture *other_format = image(context, 3, 1, DOE_DX_RGBA8_UINT, 15);
    doe_dx_texture *foreign = image(foreign_context, 3, 1, DOE_DX_R32_UINT, 15);
    doe_dx_buffer *upload = buffer(context, 12, DOE_DX_UPLOAD);
    doe_dx_commands *work = commands(context);
    doe_dx_pipeline *pipeline = texture_pipeline(context, DOE_DX_R32_UINT);
    doe_dx_texture *textures[2] = {source, source};
    doe_dx_compute_resources resources = {NULL, 0, textures, 2};
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 1, 1, 1, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, source, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 1, 1, 1, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, other_format, NULL) == DOE_DX_OK);
    textures[1] = other_format;
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 1, 1, 1, NULL) == DOE_DX_INVALID_ARGUMENT);
    textures[1] = foreign;
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 1, 1, 1, NULL) == DOE_DX_DEVICE_MISMATCH);
    assert(!mock.dispatches);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(pipeline);
    doe_dx_buffer_release(upload);
    doe_dx_texture_release(source);
    doe_dx_texture_release(other_format);
    doe_dx_texture_release(foreign);
    assert(doe_dx_context_destroy(foreign_context) == DOE_DX_OK);
    close_context(context);
}

static void texture_loss(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *texture = image(context, 3, 1, DOE_DX_R32_UINT, 15);
    doe_dx_buffer *upload = buffer(context, 12, DOE_DX_UPLOAD);
    doe_dx_commands *work = commands(context);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, texture, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_buffer_release(upload);
    doe_dx_commands_release(work);
    mock.lost = true;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_DEVICE_LOST);
    assert(!mock.buffers && !mock.commands && mock.textures == 1);
    assert(!texture->initialized && !texture->owner);
    doe_dx_texture_release(texture);
    close_context(context);
}

static void texture_and_buffer_budget(void) {
    doe_dx_context *context = open_context();
    doe_dx_buffer *upload = buffer(context, 4, DOE_DX_UPLOAD);
    doe_dx_commands *work = commands(context);
    doe_dx_texture *textures[64];
    for (size_t index = 0; index < 64; index++) textures[index] = image(context, 1, 1, DOE_DX_R32_UINT, 15);
    textures[0]->references = UINT32_MAX;
    assert(doe_dx_texture_retain(textures[0]) == DOE_DX_RANGE);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, textures[0], NULL) == DOE_DX_RANGE);
    textures[0]->references = 1;
    for (size_t index = 0; index < 63; index++)
        assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, textures[index], NULL) == DOE_DX_OK);
    assert(work->buffer_count == 1 && work->texture_count == 63);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, textures[63], NULL) == DOE_DX_OK);
    assert(textures[63]->owner == work && work->texture_count == 64 && work->buffer_count == 1);
    for (size_t index = 0; index < 64; index++) doe_dx_texture_release(textures[index]);
    doe_dx_buffer_release(upload);
    doe_dx_commands_release(work);
    close_context(context);
}


static void transfer_rejections(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *texture = image(context, 3, 2, DOE_DX_RGBA8_UNORM, 15);
    doe_dx_texture *wrong_shape = image(context, 4, 2, DOE_DX_RGBA8_UNORM, 15);
    doe_dx_texture *no_copy = image(context, 3, 2, DOE_DX_RGBA8_UNORM, DOE_DX_TEXTURE_SAMPLED);
    doe_dx_buffer *upload = buffer(context, 780, DOE_DX_UPLOAD);
    doe_dx_buffer *short_upload = buffer(context, 264, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(context, 780, DOE_DX_READBACK);
    doe_dx_commands *work = commands(context);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, no_copy, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_upload_texture(work, short_upload, (doe_dx_texture_layout){0}, texture, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){4, 0}, texture, NULL) == DOE_DX_ALIGNMENT);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0, 260}, texture, NULL) == DOE_DX_ALIGNMENT);
    assert(!work->texture_count && !work->buffer_count && !mock.texture_transfers);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 780, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(work, readback, (doe_dx_texture_layout){0}, texture, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){512, 0}, texture, NULL) == DOE_DX_OK);
    assert(mock.footprint.offset == 512 && mock.footprint.span == 268);
    assert(doe_dx_commands_read_texture(work, texture, upload, (doe_dx_texture_layout){0}, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_copy_texture(work, texture, texture, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_commands_copy_texture(work, texture, wrong_shape, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_read_texture(work, texture, readback, (doe_dx_texture_layout){512, 0}, NULL) == DOE_DX_OK);
    assert(!mock.upload && mock.footprint.offset == 512);
    size_t transfers = mock.texture_transfers;
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, texture, NULL) == DOE_DX_OK);
    assert(mock.texture_transfers == transfers + 1 && !wrong_shape->owner && !no_copy->owner);
    doe_dx_commands_release(work);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(short_upload);
    doe_dx_buffer_release(readback);
    doe_dx_texture_release(texture);
    doe_dx_texture_release(wrong_shape);
    doe_dx_texture_release(no_copy);
    close_context(context);
}

static void mixed_binding_snapshot(void) {
    doe_dx_context *context = open_context();
    doe_dx_texture *texture = image(context, 1, 1, DOE_DX_R32_UINT, 15);
    doe_dx_buffer *upload = buffer(context, 4, DOE_DX_UPLOAD);
    doe_dx_buffer *storage = buffer(context, 4, DOE_DX_DEFAULT);
    doe_dx_commands *work = commands(context);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 4, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_upload_texture(work, upload, (doe_dx_texture_layout){0}, texture, NULL) == DOE_DX_OK);
    unsigned char bytecode = 1;
    doe_dx_binding_layout buffer_layout = {2, 3, 4, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED};
    doe_dx_texture_binding_layout texture_layout = {2, 3, DOE_DX_R32_UINT, DOE_DX_READ_WRITE, false};
    doe_dx_resource_pipeline_desc desc = {{&bytecode, 1, &buffer_layout, 1}, &texture_layout, 1, NULL, 0};
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL) == DOE_DX_OK);
    /* Mutating caller layout after creation cannot change the pipeline contract. */
    buffer_layout.stride = 8;
    texture_layout.format = DOE_DX_RGBA8_UINT;
    doe_dx_buffer_binding binding = {storage, 0, 4};
    doe_dx_compute_resources resources = {&binding, 1, &texture, 1};
    assert(doe_dx_commands_dispatch_resources(work, pipeline, &resources, 1, 1, 1, NULL) == DOE_DX_OK);
    assert(mock.binding_count == 1 && mock.texture_count == 1);
    assert(mock.bindings[0].buffer == storage->native && mock.texture_bindings[0] == texture->native);
    assert(work->buffer_count == 2 && work->texture_count == 1 && work->pipeline_count == 1);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(pipeline);
    doe_dx_texture_release(texture);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(storage);
    close_context(context);
}

static void queried_format_projection(void) {
    doe_dx_context *context=open_context();
    mock.reported_format_caps=(doe_dx_format_caps){.texture_3d=true,.storage_store=true};
    doe_dx_format_caps caps={0};
    assert(doe_dx_context_format_caps(context,DOE_DX_R16_UNORM,&caps,NULL)==DOE_DX_OK);
    assert(mock.queried_format==DOE_DX_R16_UNORM && caps.texture_3d && caps.storage_store && !caps.storage_load);
    doe_dx_format_caps saved=caps;
    mock.failure=MOCK_TEXTURE;
    assert(doe_dx_context_format_caps(context,DOE_DX_R32_FLOAT,&caps,NULL)==DOE_DX_NATIVE_FAILURE);
    assert(memcmp(&saved,&caps,sizeof(caps))==0);
    mock.failure=MOCK_NONE;
    assert(doe_dx_context_format_caps(context,(doe_dx_format)999,&caps,NULL)==DOE_DX_INVALID_ARGUMENT);
    close_context(context);
}
static void component_swizzles(void) {
    doe_dx_context *context=open_context();
    doe_dx_texture *texture=image(context,2,2,DOE_DX_RGBA8_UNORM,DOE_DX_TEXTURE_SAMPLED);
    doe_dx_texture_view_desc view={.format=DOE_DX_RGBA8_UNORM,.dimension=2,.mip_count=1,.layer_count=1},out;
    assert(!doe_dx_texture_view_swizzled(&view));
    view.swizzle[0]=DOE_DX_SWIZZLE_B;
    view.swizzle[1]=DOE_DX_SWIZZLE_ZERO;
    view.swizzle[2]=DOE_DX_SWIZZLE_R;
    view.swizzle[3]=DOE_DX_SWIZZLE_ONE;
    assert(doe_dx_texture_view_validate(texture,&view,&out)==DOE_DX_OK);
    assert(doe_dx_texture_view_swizzled(&out));
    assert(memcmp(view.swizzle,out.swizzle,sizeof(view.swizzle))==0);
    doe_dx_texture_view_desc saved=out;
    view.swizzle[2]=(doe_dx_swizzle)99;
    assert(doe_dx_texture_view_validate(texture,&view,&out)==DOE_DX_INVALID_ARGUMENT);
    assert(memcmp(&saved,&out,sizeof(out))==0);
    doe_dx_texture_release(texture);
    close_context(context);
}

int main(void) {
    mock_reset();
    queried_format_projection();
    component_swizzles();
    footprint_and_construction();
    padded_readback_and_early_release();
    cancellation_and_unpadded_readback();
    binding_layout_rejections();
    texture_binding_rejections();
    texture_loss();
    texture_and_buffer_budget();
    transfer_rejections();
    mixed_binding_snapshot();
    puts("D3D12 C texture owner: 11 groups passed (no SDK or GPU execution)");
    return 0;
}
