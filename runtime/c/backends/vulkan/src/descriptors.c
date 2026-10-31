/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "../include/doe_vk_bindings.h"

uint32_t doe_vk_descriptor_type(doe_vk_binding_kind kind) {
    switch (kind) {
        case DOE_VK_BINDING_UNIFORM: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DOE_VK_BINDING_STORAGE: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DOE_VK_BINDING_SAMPLED_TEXTURE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DOE_VK_BINDING_STORAGE_TEXTURE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DOE_VK_BINDING_SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return UINT32_MAX;
}

static doe_vk_status validate_buffer(doe_vk_context *context,
    doe_vk_binding_kind kind, const doe_vk_buffer_binding *binding) {
    doe_vk_buffer *buffer = binding->buffer;
    bool uniform = kind == DOE_VK_BINDING_UNIFORM;
    uint32_t usage = uniform ? DOE_VK_BUFFER_UNIFORM : DOE_VK_BUFFER_STORAGE;
    uint64_t alignment = uniform ? context->caps.uniform_offset_alignment : context->caps.storage_offset_alignment;
    uint64_t max_range = uniform ? context->caps.max_uniform_buffer_range : context->caps.max_storage_buffer_range;
    if (buffer == NULL || buffer->context != context || !(buffer->usage & usage) ||
        binding->size == 0 || binding->size > max_range ||
        !doe_vk_range(buffer->size, binding->offset, binding->size) ||
        alignment == 0 || binding->offset % alignment != 0) return DOE_VK_INVALID_ARGUMENT;
    return buffer->content_status;
}

static doe_vk_status validate_view(doe_vk_commands *commands,
    doe_vk_binding_kind kind, doe_vk_texture_view *view) {
    if (view == NULL || view->texture->context != commands->context)
        return DOE_VK_INVALID_ARGUMENT;
    doe_vk_texture *texture = view->texture;
    uint32_t usage = kind == DOE_VK_BINDING_SAMPLED_TEXTURE ? DOE_VK_TEXTURE_SAMPLED : DOE_VK_TEXTURE_STORAGE;
    if (!(texture->desc.usage & usage)) return DOE_VK_INVALID_ARGUMENT;
    if (kind == DOE_VK_BINDING_STORAGE_TEXTURE && (view->desc.mip_count != 1 || doe_vk_texture_view_swizzled(view)))
        return DOE_VK_INVALID_ARGUMENT;
    /* A sampled depth/stencil view must select exactly one aspect. */
    if (view->aspects == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        return DOE_VK_INVALID_ARGUMENT;
    doe_vk_texture_use *use = NULL;
    doe_vk_status status = doe_vk_use_texture(commands, texture, &use);
    if (status != DOE_VK_OK) return status;
    uint32_t layers = texture->desc.dimension == DOE_VK_TEXTURE_3D ? 1 : texture->desc.depth_or_layers;
    for (uint32_t mip = view->desc.base_mip; mip < view->desc.base_mip + view->desc.mip_count; ++mip) {
        for (uint32_t layer = view->desc.base_layer; layer < view->desc.base_layer + view->desc.layer_count; ++layer) {
            if ((use->initialized[(size_t)mip * layers + layer] & view->aspects) != view->aspects)
                return DOE_VK_INVALID_STATE;
        }
    }
    return DOE_VK_OK;
}

static doe_vk_status validate_resources(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings, size_t count) {
    if (pipeline == NULL || pipeline->context != commands->context ||
        count != pipeline->binding_count || (count != 0 && bindings == NULL))
        return DOE_VK_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        if (bindings[i].kind != pipeline->bindings[i].kind) return DOE_VK_INVALID_ARGUMENT;
        doe_vk_status status;
        switch (bindings[i].kind) {
            case DOE_VK_BINDING_UNIFORM:
            case DOE_VK_BINDING_STORAGE:
                status = validate_buffer(commands->context, bindings[i].kind, &bindings[i].value.buffer);
                break;
            case DOE_VK_BINDING_SAMPLED_TEXTURE:
            case DOE_VK_BINDING_STORAGE_TEXTURE:
                status = validate_view(commands, bindings[i].kind, bindings[i].value.view);
                break;
            case DOE_VK_BINDING_SAMPLER:
                status = bindings[i].value.sampler != NULL && bindings[i].value.sampler->context == commands->context ?
                    DOE_VK_OK : DOE_VK_INVALID_ARGUMENT;
                break;
            default: status = DOE_VK_INVALID_ARGUMENT; break;
        }
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}

/* Descriptor sets follow the adapter's resource profile (doe_vk.h). Without
 * DESCRIPTOR_BLOCKS each dispatch use creates an exact-size pool and destroys it
 * at release. With it, uses allocate their sets from context-owned blocks created with
 * FREE_DESCRIPTOR_SET and free them at release. A block that is exhausted or
 * fragmented is skipped; a new block is created only when every block refuses.
 * Either way a set is written once by its use and never updated while any
 * recording holds it. */
enum { DESCRIPTOR_BLOCK_SETS = 256, DESCRIPTOR_BLOCK_PER_TYPE = 1024 };
enum { VK_ERROR_FRAGMENTED_POOL_VALUE = -12, VK_ERROR_OUT_OF_POOL_MEMORY_VALUE = -1000069000,
    VK_DESCRIPTOR_POOL_FREE_SET = 0x1 };
struct doe_vk_descriptor_block {
    VkDescriptorPool pool;
    struct doe_vk_descriptor_block *next;
};

static doe_vk_status descriptor_block(doe_vk_context *context, struct doe_vk_descriptor_block **out) {
    struct doe_vk_descriptor_block *block = calloc(1, sizeof(*block));
    if (block == NULL) return DOE_VK_OUT_OF_MEMORY;
    VkDescriptorPoolSize sizes[5];
    for (uint32_t i = 0; i < 5; ++i)
        sizes[i] = (VkDescriptorPoolSize){ doe_vk_descriptor_type((doe_vk_binding_kind)i), DESCRIPTOR_BLOCK_PER_TYPE };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_FREE_SET,
        .maxSets = DESCRIPTOR_BLOCK_SETS,
        .poolSizeCount = 5,
        .pPoolSizes = sizes
    };
    doe_vk_status status = doe_vk_result(context, context->vk.vkCreateDescriptorPool(context->device,
        &pool, NULL, &block->pool), "vkCreateDescriptorPool", NULL);
    if (status != DOE_VK_OK) {
        free(block);
        return status;
    }
    block->next = context->descriptor_blocks;
    context->descriptor_blocks = block;
    *out = block;
    return DOE_VK_OK;
}

static doe_vk_status use_pool(doe_vk_context *context,
    doe_vk_pipeline *pipeline, doe_vk_dispatch_use *use, VkDescriptorSet *sets) {
    uint32_t counts[5] = {0};
    for (uint32_t i = 0; i < pipeline->binding_count; ++i) ++counts[(uint32_t)pipeline->bindings[i].kind];
    VkDescriptorPoolSize sizes[5];
    uint32_t size_count = 0;
    for (uint32_t i = 0; i < 5; ++i) {
        if (counts[i] != 0) sizes[size_count++] = (VkDescriptorPoolSize){ doe_vk_descriptor_type((doe_vk_binding_kind)i), counts[i] };
    }
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = pipeline->group_count,
        .poolSizeCount = size_count,
        .pPoolSizes = sizes
    };
    doe_vk_status status = doe_vk_result(context, context->vk.vkCreateDescriptorPool(context->device,
        &pool, NULL, &use->pool), "vkCreateDescriptorPool", NULL);
    if (status != DOE_VK_OK) return status;
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = use->pool,
        .descriptorSetCount = pipeline->group_count,
        .pSetLayouts = pipeline->sets
    };
    return doe_vk_result(context, context->vk.vkAllocateDescriptorSets(context->device,
        &allocate, sets), "vkAllocateDescriptorSets", NULL);
}

static doe_vk_status descriptor_pool(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, doe_vk_dispatch_use *use, VkDescriptorSet *sets) {
    if (pipeline->group_count == 0) return DOE_VK_OK;
    doe_vk_context *context = commands->context;
    if (!(context->caps.resource_profile & DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS))
        return use_pool(context, pipeline, use, sets);
    VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorSetCount = pipeline->group_count,
        .pSetLayouts = pipeline->sets
    };
    VkResult result = 0;
    for (struct doe_vk_descriptor_block *block = context->descriptor_blocks; block != NULL; block = block->next) {
        allocate.descriptorPool = block->pool;
        result = context->vk.vkAllocateDescriptorSets(context->device, &allocate, sets);
        if (result == 0) {
            use->pool = block->pool;
            return DOE_VK_OK;
        }
        if (result != VK_ERROR_FRAGMENTED_POOL_VALUE && result != VK_ERROR_OUT_OF_POOL_MEMORY_VALUE)
            return doe_vk_result(context, result, "vkAllocateDescriptorSets", NULL);
    }
    struct doe_vk_descriptor_block *fresh = NULL;
    doe_vk_status status = descriptor_block(context, &fresh);
    if (status != DOE_VK_OK) return status;
    allocate.descriptorPool = fresh->pool;
    result = context->vk.vkAllocateDescriptorSets(context->device, &allocate, sets);
    if (result == 0) use->pool = fresh->pool;
    /* A refusal from a fresh block means the layout cannot fit: a failure. */
    return result == 0 ? DOE_VK_OK : doe_vk_result(context, result, "vkAllocateDescriptorSets", NULL);
}

void doe_vk_descriptor_blocks_clear(doe_vk_context *context) {
    while (context->descriptor_blocks != NULL) {
        struct doe_vk_descriptor_block *block = context->descriptor_blocks;
        context->descriptor_blocks = block->next;
        context->vk.vkDestroyDescriptorPool(context->device, block->pool, NULL);
        free(block);
    }
}

static bool buffers_only(const doe_vk_binding_resource *bindings, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (bindings[i].kind != DOE_VK_BINDING_UNIFORM && bindings[i].kind != DOE_VK_BINDING_STORAGE) return false;
    return true;
}

/* The latest use of this recording binds the same compute pipeline to the
 * same exact buffer ranges: its descriptor sets already hold these
 * descriptors, and its buffers are already recorded uses. Images are excluded
 * because their layouts may change between uses; draws keep one use each. */
static bool same_buffer_use(const doe_vk_dispatch_use *use, const doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count) {
    if (use == NULL || pipeline->graphics || !use->buffers_only || use->pipeline != pipeline ||
        use->buffer_count != count) return false;
    for (size_t i = 0; i < count; ++i) {
        const doe_vk_buffer_binding *a = &use->buffers[i], *b = &bindings[i].value.buffer;
        if (a->buffer != b->buffer || a->offset != b->offset || a->size != b->size) return false;
    }
    return true;
}

doe_vk_status doe_vk_prepare_resources(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings, size_t count, doe_vk_dispatch_use **out) {
    doe_vk_status status = validate_resources(commands, pipeline, bindings, count);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    const bool only_buffers = buffers_only(bindings, count);
    if (only_buffers && same_buffer_use(commands->dispatches, pipeline, bindings, count)) {
        doe_vk_barrier(commands);
        *out = commands->dispatches;
        return DOE_VK_OK;
    }
    doe_vk_dispatch_use *use = calloc(1, sizeof(*use) + (only_buffers ? count : 0) * sizeof(doe_vk_buffer_binding));
    if (use == NULL) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    use->buffers_only = only_buffers;
    if (only_buffers) {
        use->buffer_count = (uint32_t)count;
        for (size_t i = 0; i < count; ++i) use->buffers[i] = bindings[i].value.buffer;
    }
    status = doe_vk_pipeline_retain(pipeline);
    if (status != DOE_VK_OK) {
        free(use);
        return doe_vk_poison(commands, status);
    }
    use->pipeline = pipeline;
    use->next = commands->dispatches;
    commands->dispatches = use;
    status = descriptor_pool(commands, pipeline, use, use->sets);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    VkDescriptorBufferInfo buffers[DOE_VK_MAX_BINDINGS];
    VkDescriptorImageInfo images[DOE_VK_MAX_BINDINGS];
    VkWriteDescriptorSet writes[DOE_VK_MAX_BINDINGS];
    for (size_t i = 0; i < count; ++i) {
        const doe_vk_binding_resource *binding = &bindings[i];
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = use->sets[pipeline->bindings[i].group],
            .dstBinding = pipeline->bindings[i].binding,
            .descriptorCount = 1,
            .descriptorType = doe_vk_descriptor_type(binding->kind)
        };
        if (binding->kind == DOE_VK_BINDING_UNIFORM || binding->kind == DOE_VK_BINDING_STORAGE) {
            status = doe_vk_use_buffer(commands, binding->value.buffer.buffer);
            if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
            buffers[i] = (VkDescriptorBufferInfo){ binding->value.buffer.buffer->buffer,
                binding->value.buffer.offset, binding->value.buffer.size };
            writes[i].pBufferInfo = &buffers[i];
        } else if (binding->kind == DOE_VK_BINDING_SAMPLER) {
            status = doe_vk_sampler_retain(binding->value.sampler);
            if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
            use->samplers[i] = binding->value.sampler;
            images[i] = (VkDescriptorImageInfo){ .sampler = binding->value.sampler->sampler };
            writes[i].pImageInfo = &images[i];
        } else {
            status = doe_vk_texture_view_retain(binding->value.view);
            if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
            use->views[i] = binding->value.view;
            images[i] = (VkDescriptorImageInfo){ .imageView = binding->value.view->view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
            writes[i].pImageInfo = &images[i];
        }
    }
    doe_vk_context *context = commands->context;
    if (count != 0) context->vk.vkUpdateDescriptorSets(context->device, (uint32_t)count, writes, 0, NULL);
    doe_vk_barrier(commands);
    for (size_t i = 0; i < count; ++i) {
        if (use->views[i] == NULL) continue;
        doe_vk_texture_use *texture_use = NULL;
        status = doe_vk_use_texture(commands, use->views[i]->texture, &texture_use);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
        doe_vk_texture_transition(commands, texture_use, VK_IMAGE_LAYOUT_GENERAL);
    }
    *out = use;
    return DOE_VK_OK;
}

void doe_vk_bind_prepared_resources(doe_vk_commands *commands, const doe_vk_dispatch_use *use) {
    const doe_vk_pipeline *pipeline = use->pipeline;
    doe_vk_context *context = commands->context;
    int32_t bind_point = pipeline->graphics ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
    context->vk.vkCmdBindPipeline(commands->command, bind_point, pipeline->pipeline);
    if (pipeline->group_count != 0) context->vk.vkCmdBindDescriptorSets(commands->command,
        bind_point, pipeline->layout, 0, pipeline->group_count, use->sets, 0, NULL);
}

doe_vk_status doe_vk_bind_resources(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings, size_t count) {
    doe_vk_dispatch_use *use = NULL;
    doe_vk_status status = doe_vk_prepare_resources(commands, pipeline, bindings, count, &use);
    if (status != DOE_VK_OK) return status;
    doe_vk_bind_prepared_resources(commands, use);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_bind_dispatch(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings, size_t count) {
    if (pipeline == NULL || pipeline->graphics || count > DOE_VK_MAX_BINDINGS || count != pipeline->binding_count ||
        (count != 0 && bindings == NULL)) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    doe_vk_binding_resource resources[DOE_VK_MAX_BINDINGS];
    for (size_t i = 0; i < count; ++i) {
        if (pipeline->bindings[i].kind != DOE_VK_BINDING_UNIFORM && pipeline->bindings[i].kind != DOE_VK_BINDING_STORAGE)
            return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
        resources[i].kind = pipeline->bindings[i].kind;
        resources[i].value.buffer = bindings[i];
    }
    return doe_vk_bind_resources(commands, pipeline, resources, count);
}

doe_vk_status doe_vk_commands_dispatch_resources(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings,
    size_t count, uint32_t x, uint32_t y, uint32_t z) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (pipeline == NULL || pipeline->graphics) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    const uint32_t *limit = commands->context->caps.max_workgroup_count;
    if (x > limit[0] || y > limit[1] || z > limit[2]) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    status = doe_vk_bind_resources(commands, pipeline, bindings, count);
    if (status != DOE_VK_OK) return status;
    commands->context->vk.vkCmdDispatch(commands->command, x, y, z);
    return DOE_VK_OK;
}

void doe_vk_dispatch_release(doe_vk_context *context, doe_vk_dispatch_use *use) {
    if (use->pool != 0 && (context->caps.resource_profile & DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS))
        /* Freeing never fails for a FREE_DESCRIPTOR_SET pool. */
        (void)context->vk.vkFreeDescriptorSets(context->device, use->pool, use->pipeline->group_count, use->sets);
    else if (use->pool != 0) context->vk.vkDestroyDescriptorPool(context->device, use->pool, NULL);
    for (uint32_t i = 0; i < DOE_VK_MAX_BINDINGS; ++i) {
        doe_vk_texture_view_release(use->views[i]);
        doe_vk_sampler_release(use->samplers[i]);
    }
    doe_vk_pipeline_release(use->pipeline);
    free(use);
}
