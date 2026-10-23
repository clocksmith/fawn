/* SPDX-License-Identifier: MIT
 * Real native-API lifetime consumer. Explicit software ICD only, not GPU evidence. */
#include "vulkan_fixture.h"

static doe_vk_texture_region image_region(const doe_resource_image_copy *image,
    const doe_resource_texture_blit *copy)
{
    return (doe_vk_texture_region){image->mip, {image->origin[0], image->origin[1], image->origin[2]},
        {copy->width, copy->height, copy->depth_or_layers}, DOE_VK_ASPECT_ALL};
}

int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--allow-software")) return 2;
    doe_vk_options options = {.allow_cpu_adapter = true};
    doe_vk_error error = {0};
    doe_vk_context *context;
    require(doe_vk_context_create(&options, &context, &error), "create exact ICD context");
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    if (!caps.software_adapter) {
        fputs("Refusing non-software adapter in software-only developer check\n", stderr);
        doe_vk_context_release(context);
        return 77;
    }
    printf("Software Vulkan lifetime consumer: %s; not physical GPU qualification\n", caps.device_name);
    lifecycle life = {0};
    doe_resource_config config = {
        .allocator = {&life, NULL, allocate, deallocate},
        .device = {context, alive, retain, release}
    };
    doe_vk_buffer *native_source, *native_destination;
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &native_source, &error), "source create");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &native_destination, &error), "destination create");
    uint32_t expected[64], actual[64];
    for (uint32_t i = 0; i < 64; i++) expected[i] = i * 17 + 9;
    require(doe_vk_buffer_write(native_source, 0, expected, sizeof(expected)), "initialize source");
    doe_resource_buffer_desc source_desc = {256, WGPUBufferUsage_CopySrc, false};
    doe_resource_buffer_desc destination_desc = {256, WGPUBufferUsage_CopyDst, false};
    doe_resource_buffer *source, *destination;
    assert(doe_resource_buffer_create(&config, &source_desc,
        (doe_resource_native){native_source, &life, release_buffer}, &source) == DOE_RESOURCE_OK);
    assert(doe_resource_buffer_create(&config, &destination_desc,
        (doe_resource_native){native_destination, &life, release_buffer}, &destination) == DOE_RESOURCE_OK);
    doe_vk_texture_desc native_desc = {
        .dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1,
        .usage = DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST
    };
    doe_resource_texture_desc texture_desc = {
        .dimension = WGPUTextureDimension_2D, .format = WGPUTextureFormat_RGBA8Unorm,
        .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1, .samples = 1,
        .usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst
    };
    doe_resource_texture_limits limits = {caps.max_texture_dimension_1d, caps.max_texture_dimension_2d,
        caps.max_texture_dimension_3d, caps.max_texture_array_layers};
    doe_resource_texture *images[2];
    for (size_t i = 0; i < 2; i++) {
        doe_vk_texture *image;
        require(doe_vk_texture_create(context, &native_desc, &image, &error), "image create");
        assert(doe_resource_texture_create(&config, &texture_desc, &limits,
            (doe_resource_native){image, &life, release_texture}, &images[i]) == DOE_RESOURCE_OK);
    }
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&config, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_clear_buffer(encoder, destination, 0, 256) == DOE_RESOURCE_OK);
    doe_resource_buffer_copy copy = {source, destination, 0, 0, UINT64_MAX};
    assert(doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS) == DOE_RESOURCE_OK);
    /* Full native upload establishes content before any read; image allocation
     * alone is not WebGPU initialization or permission to read undefined bytes. */
    doe_resource_texture_copy texture_copy = {.bytes_per_row = 32, .rows_per_image = 8,
        .width = 8, .height = 8, .depth_or_layers = 1};
    assert(doe_resource_encoder_copy_texture(encoder, source, images[0], &texture_copy,
        DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_COPY_NATIVE) == DOE_RESOURCE_OK);
    doe_resource_texture_blit blit = {.source = {.texture = images[0]}, .destination = {.texture = images[1]},
        .width = 8, .height = 8, .depth_or_layers = 1};
    assert(doe_resource_encoder_blit_texture(encoder, &blit, DOE_RESOURCE_COPY_NATIVE) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_copy_texture(encoder, destination, images[1], &texture_copy,
        DOE_RESOURCE_TEXTURE_TO_BUFFER, DOE_RESOURCE_COPY_NATIVE) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_buffer_release(source);
    doe_resource_buffer_release(destination);
    doe_resource_texture_release(images[0]);
    doe_resource_texture_release(images[1]);
    assert(!life.native_releases);
    doe_vk_commands *native_commands;
    require(doe_vk_commands_create(context, &native_commands, &error), "native commands create");
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 5);
    for (size_t i = 0; i < count; i++) {
        const doe_resource_command *record = &records[i];
        if (record->kind == DOE_COMMAND_CLEAR_BUFFER)
            require(doe_vk_commands_fill(native_commands,
                doe_resource_buffer_native(record->value.clear.buffer),
                record->value.clear.offset, record->value.clear.size, 0), "record clear");
        else if (record->kind == DOE_COMMAND_COPY_BUFFER)
            require(doe_vk_commands_copy(native_commands,
                doe_resource_buffer_native(record->value.copy.source), record->value.copy.source_offset,
                doe_resource_buffer_native(record->value.copy.destination), record->value.copy.destination_offset,
                record->value.copy.size), "record copy");
        else if (record->kind == DOE_COMMAND_BUFFER_TEXTURE) {
            const doe_resource_texture_copy *region = &record->value.texture.copy;
            doe_vk_texture_region native_region = {region->mip,
                {region->origin[0], region->origin[1], region->origin[2]},
                {region->width, region->height, region->depth_or_layers}, DOE_VK_ASPECT_ALL};
            doe_vk_texture_buffer_layout layout = {region->offset,
                record->value.texture.region.pitch, record->value.texture.region.image_rows};
            doe_vk_texture *image = doe_resource_texture_native(record->value.texture.texture);
            doe_vk_buffer *buffer = doe_resource_buffer_native(record->value.texture.buffer);
            if (record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE)
                require(doe_vk_commands_upload_texture(native_commands, buffer, layout, image, native_region), "upload image");
            else require(doe_vk_commands_read_texture(native_commands, image, native_region, buffer, layout), "read image");
        } else if (record->kind == DOE_COMMAND_COPY_TEXTURE) {
            const doe_resource_texture_blit *image_copy = &record->value.blit;
            require(doe_vk_commands_copy_texture(native_commands,
                doe_resource_texture_native(image_copy->source.texture), image_region(&image_copy->source, image_copy),
                doe_resource_texture_native(image_copy->destination.texture), image_region(&image_copy->destination, image_copy)),
                "copy image");
        } else abort();
    }
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    require(doe_vk_commands_submit(native_commands, &error), "submit");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_vk_commands_release(native_commands) == DOE_VK_BUSY);
    assert(doe_vk_buffer_read(native_destination, 0, actual, sizeof(actual)) == DOE_VK_BUSY);
    assert(!life.native_releases);
    require(doe_vk_commands_wait(native_commands, UINT64_C(5000000000), &error), "wait");
    require(doe_vk_buffer_read(native_destination, 0, actual, sizeof(actual)), "readback");
    assert(!memcmp(expected, actual, sizeof(actual)));
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(life.native_releases == 4 && !life.allocations);
    require(doe_vk_commands_release(native_commands), "native commands release");
    doe_vk_context_release(context);
    puts("PASS C resource adoption, early caller release, pending retention, real fill/buffer copy/image upload/copy/readback and terminal release");
    return 0;
}
