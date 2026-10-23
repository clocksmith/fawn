/* SPDX-License-Identifier: MIT */
#include "../src/texture_internal.h"
#include "../include/doe_vk_bindings.h"
#include <assert.h>
#include <stdio.h>

static doe_vk_commands recording(doe_vk_context *context) {
    doe_vk_commands commands = { .context = context, .state = DOE_VK_RECORDING, .uses_active = true };
    return commands;
}

int main(void) {
    doe_vk_context context = { .references = 1 };
    context.caps.max_workgroup_count[0] = 64;
    context.caps.max_workgroup_count[1] = 64;
    context.caps.max_workgroup_count[2] = 64;
    doe_vk_pipeline pipeline = { .context = &context, .references = 1, .binding_count = 1 };
    pipeline.bindings[0] = (doe_vk_binding_layout){0, 7, DOE_VK_BINDING_STORAGE_TEXTURE};
    doe_vk_commands commands = recording(&context);
    doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_STORAGE };
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_INVALID_ARGUMENT);
    assert(commands.state == DOE_VK_POISONED && pipeline.references == 1);
    uint8_t initialized[1] = {0};
    doe_vk_texture texture = {
        .context = &context, .references = 1, .subresource_count = 1,
        .initialized = initialized,
        .desc = { .dimension = DOE_VK_TEXTURE_2D, .width = 4, .height = 4,
            .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_STORAGE }
    };
    doe_vk_texture_view view = { .texture = &texture, .references = 1, .aspects = 1,
        .desc = { DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL} };
    binding = (doe_vk_binding_resource){ .kind = DOE_VK_BINDING_STORAGE_TEXTURE, .value.view = &view };
    commands = recording(&context);
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_INVALID_STATE);
    assert(texture.recording_owner == &commands && texture.references == 2);
    doe_vk_textures_release(&commands);
    assert(texture.recording_owner == NULL && texture.references == 1 && view.references == 1);
    texture.desc.usage = DOE_VK_TEXTURE_SAMPLED;
    initialized[0] = 1;
    commands = recording(&context);
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_INVALID_ARGUMENT);
    assert(commands.textures == NULL);
    pipeline.bindings[0].kind = DOE_VK_BINDING_SAMPLED_TEXTURE;
    binding.kind = DOE_VK_BINDING_SAMPLED_TEXTURE;
    texture.content_status = DOE_VK_OUT_OF_MEMORY;
    commands = recording(&context);
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OUT_OF_MEMORY);
    assert(commands.textures == NULL);
    commands = recording(&context);
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 65, 1, 1) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_descriptor_type(DOE_VK_BINDING_SAMPLED_TEXTURE) == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    assert(doe_vk_descriptor_type(DOE_VK_BINDING_STORAGE_TEXTURE) == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    assert(doe_vk_descriptor_type(DOE_VK_BINDING_SAMPLER) == VK_DESCRIPTOR_TYPE_SAMPLER);
    assert(doe_vk_descriptor_type((doe_vk_binding_kind)999) == UINT32_MAX);
    puts("PASS typed descriptor mismatch, uninitialized storage view, wrong usage, failed content and oversized dispatch rejection without native publication");
    return 0;
}
