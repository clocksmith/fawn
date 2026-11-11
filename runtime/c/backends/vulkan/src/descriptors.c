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
enum { DESCRIPTOR_BLOCK_SETS = 256, DESCRIPTOR_BLOCK_PER_TYPE = 1024,
    REUSE_BLOCK_SETS = 32, REUSE_BLOCK_PER_TYPE = DOE_VK_MAX_BINDINGS };
enum { VK_ERROR_FRAGMENTED_POOL_VALUE = -12, VK_ERROR_OUT_OF_POOL_MEMORY_VALUE = -1000069000,
    VK_DESCRIPTOR_POOL_FREE_SET = 0x1 };
struct doe_vk_descriptor_block {
    VkDescriptorPool pool;
    struct doe_vk_descriptor_block *next;
};

static bool descriptor_reuse_enabled(const doe_vk_context *context) {
    const uint32_t both = DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS | DOE_VK_RESOURCE_DESCRIPTOR_REUSE;
    return (context->caps.resource_profile & both) == both;
}

static void free_dispatch_sets(doe_vk_context *context, doe_vk_dispatch_use *use) {
    if (use->pool == 0) return;
    if (context->caps.resource_profile & DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS)
        (void)context->vk.vkFreeDescriptorSets(context->device, use->pool, use->group_count, use->sets);
    else context->vk.vkDestroyDescriptorPool(context->device, use->pool, NULL);
}

static doe_vk_status descriptor_block(doe_vk_context *context, struct doe_vk_descriptor_block **out) {
    struct doe_vk_descriptor_block *block = calloc(1, sizeof(*block));
    if (block == NULL) return DOE_VK_OUT_OF_MEMORY;
    const bool compact = descriptor_reuse_enabled(context);
    const uint32_t per_type = compact ? REUSE_BLOCK_PER_TYPE : DESCRIPTOR_BLOCK_PER_TYPE;
    VkDescriptorPoolSize sizes[5];
    for (uint32_t i = 0; i < 5; ++i)
        sizes[i] = (VkDescriptorPoolSize){ doe_vk_descriptor_type((doe_vk_binding_kind)i), per_type };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_FREE_SET,
        .maxSets = compact ? REUSE_BLOCK_SETS : DESCRIPTOR_BLOCK_SETS,
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
    for (uint32_t i = 0; i < context->retained_dispatch_count; ++i)
        free(context->retained_dispatches[i]);
    context->retained_dispatch_count = 0;
    while (context->descriptor_blocks != NULL) {
        struct doe_vk_descriptor_block *block = context->descriptor_blocks;
        context->descriptor_blocks = block->next;
        context->vk.vkDestroyDescriptorPool(context->device, block->pool, NULL);
        free(block);
    }
}

/* Descriptor identity is immutable. View initialization, owner leases and image
 * layout are checked/prepared again on every use, independently of set reuse. */
static bool same_use(const doe_vk_dispatch_use *use, const doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count) {
    if (!use || use->pipeline != pipeline || use->binding_count != count) return false;
    for (size_t i = 0; i < count; ++i) {
        const doe_vk_binding_resource *a = &use->bindings[i], *b = &bindings[i];
        if (a->kind != b->kind) return false;
        if (a->kind == DOE_VK_BINDING_UNIFORM || a->kind == DOE_VK_BINDING_STORAGE) {
            if (a->value.buffer.buffer != b->value.buffer.buffer || a->value.buffer.offset != b->value.buffer.offset ||
                a->value.buffer.size != b->value.buffer.size) return false;
        } else if (a->kind == DOE_VK_BINDING_SAMPLER) {
            if (a->value.sampler != b->value.sampler) return false;
        } else if (a->value.view != b->value.view) return false;
    }
    return true;
}

static bool dispatch_references_resource(const doe_vk_dispatch_use *use, const void *resource) {
    if ((const void *)use->pipeline == resource) return true;
    for (uint32_t i = 0; i < use->binding_count; ++i) {
        const doe_vk_binding_resource *b = &use->bindings[i];
        if (b->kind == DOE_VK_BINDING_UNIFORM || b->kind == DOE_VK_BINDING_STORAGE) {
            if ((const void *)b->value.buffer.buffer == resource) return true;
        } else if (b->kind == DOE_VK_BINDING_SAMPLER) {
            if ((const void *)b->value.sampler == resource) return true;
        } else if ((const void *)b->value.view == resource ||
                (const void *)b->value.view->texture == resource) return true;
    }
    return false;
}

void doe_vk_descriptor_evict(doe_vk_context *context, const void *resource) {
    if (context == NULL || resource == NULL || context->retained_dispatch_count == 0) return;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < context->retained_dispatch_count; ++i) {
        doe_vk_dispatch_use *entry = context->retained_dispatches[i];
        if (dispatch_references_resource(entry, resource)) {
            free_dispatch_sets(context, entry);
            free(entry);
        } else context->retained_dispatches[kept++] = entry;
    }
    context->retained_dispatch_count = kept;
}

static doe_vk_dispatch_use *take_retained_dispatch(doe_vk_context *context,
    const doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings, size_t count) {
    for (uint32_t i = context->retained_dispatch_count; i-- > 0;) {
        doe_vk_dispatch_use *entry = context->retained_dispatches[i];
        if (!same_use(entry, pipeline, bindings, count)) continue;
        const uint32_t tail = context->retained_dispatch_count - i - 1u;
        if (tail != 0) memmove(&context->retained_dispatches[i],
            &context->retained_dispatches[i + 1u], tail * sizeof(*context->retained_dispatches));
        --context->retained_dispatch_count;
        return entry;
    }
    return NULL;
}

static doe_vk_status lease_dispatch_resources(doe_vk_commands *commands,
    doe_vk_dispatch_use *use, const doe_vk_binding_resource *bindings, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const doe_vk_binding_resource *binding = &bindings[i];
        doe_vk_status status;
        if (binding->kind == DOE_VK_BINDING_UNIFORM || binding->kind == DOE_VK_BINDING_STORAGE) {
            status = doe_vk_use_buffer(commands, binding->value.buffer.buffer);
            if (status != DOE_VK_OK) return status;
        } else if (binding->kind == DOE_VK_BINDING_SAMPLER) {
            status = doe_vk_sampler_retain(binding->value.sampler);
            if (status != DOE_VK_OK) return status;
            use->samplers[i] = binding->value.sampler;
        } else {
            status = doe_vk_texture_view_retain(binding->value.view);
            if (status != DOE_VK_OK) return status;
            use->views[i] = binding->value.view;
        }
    }
    return DOE_VK_OK;
}

static doe_vk_status prepare_resources(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings, size_t count, doe_vk_dispatch_use **out,
    bool synchronize) {
    doe_vk_status status = validate_resources(commands, pipeline, bindings, count);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_context *context = commands->context;
    const bool reuse = descriptor_reuse_enabled(context);
    doe_vk_dispatch_use *use = commands->dispatches;
    if (same_use(use, pipeline, bindings, count)) goto prepared;
    if (reuse && use != NULL) {
        uint32_t scanned = 1;
        for (doe_vk_dispatch_use *prior = use->next; prior != NULL && scanned < DOE_VK_RETAINED_DISPATCHES;
                prior = prior->next, ++scanned) {
            if (same_use(prior, pipeline, bindings, count)) { use = prior; goto prepared; }
        }
    }
    if (reuse) {
        use = take_retained_dispatch(context, pipeline, bindings, count);
        if (use != NULL) {
            status = doe_vk_pipeline_retain(pipeline);
            if (status != DOE_VK_OK) {
                free_dispatch_sets(context, use);
                free(use);
                return doe_vk_poison(commands, status);
            }
            use->next = commands->dispatches;
            commands->dispatches = use;
            status = lease_dispatch_resources(commands, use, bindings, count);
            if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
            goto prepared;
        }
    }
    if (count > (SIZE_MAX - sizeof(*use)) / sizeof(*bindings)) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    use = calloc(1, sizeof(*use) + count * sizeof(*bindings));
    if (!use) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    use->binding_count = (uint32_t)count;
    use->group_count = pipeline->group_count;
    for (size_t i = 0; i < count; ++i) use->bindings[i] = bindings[i];
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
    status = lease_dispatch_resources(commands, use, bindings, count);
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
            buffers[i] = (VkDescriptorBufferInfo){ binding->value.buffer.buffer->buffer,
                binding->value.buffer.offset, binding->value.buffer.size };
            writes[i].pBufferInfo = &buffers[i];
        } else if (binding->kind == DOE_VK_BINDING_SAMPLER) {
            images[i] = (VkDescriptorImageInfo){ .sampler = binding->value.sampler->sampler };
            writes[i].pImageInfo = &images[i];
        } else {
            images[i] = (VkDescriptorImageInfo){ .imageView = binding->value.view->view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
            writes[i].pImageInfo = &images[i];
        }
    }
    if (count != 0) context->vk.vkUpdateDescriptorSets(context->device, (uint32_t)count, writes, 0, NULL);
    use->written = true;
prepared:
    if (synchronize) doe_vk_barrier(commands);
    for (size_t i = 0; i < count; ++i) {
        if (use->views[i] == NULL) continue;
        doe_vk_texture_use *texture_use = NULL;
        status = doe_vk_use_texture(commands, use->views[i]->texture, &texture_use);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
        if (synchronize || texture_use->layout != VK_IMAGE_LAYOUT_GENERAL)
            doe_vk_texture_transition(commands, texture_use, VK_IMAGE_LAYOUT_GENERAL);
    }
    *out = use;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_prepare_resources(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count, doe_vk_dispatch_use **out) {
    return prepare_resources(commands, pipeline, bindings, count, out, true);
}
doe_vk_status doe_vk_prepare_resources_unsynchronized(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count, doe_vk_dispatch_use **out) {
    return prepare_resources(commands,pipeline,bindings,count,out,false);
}

doe_vk_status doe_vk_prepare_graphics_resources(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count, doe_vk_dispatch_use **out) {
    if (!pipeline || !pipeline->graphics) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    return prepare_resources(commands, pipeline, bindings, count, out, false);
}

void doe_vk_bind_prepared_resources_since(doe_vk_commands *commands, const doe_vk_dispatch_use *use,
    const doe_vk_dispatch_use *previous) {
    const doe_vk_pipeline *pipeline = use->pipeline;
    doe_vk_context *context = commands->context;
    int32_t bind_point = pipeline->graphics ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
    if (!previous || previous->pipeline != pipeline)
        context->vk.vkCmdBindPipeline(commands->command, bind_point, pipeline->pipeline);
    if (pipeline->group_count != 0 && previous != use) context->vk.vkCmdBindDescriptorSets(commands->command,
        bind_point, pipeline->layout, 0, pipeline->group_count, use->sets, 0, NULL);
}

void doe_vk_bind_prepared_resources(doe_vk_commands *commands, const doe_vk_dispatch_use *use) {
    doe_vk_bind_prepared_resources_since(commands, use, NULL);
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
    doe_vk_pipeline *pipeline = use->pipeline;
    doe_vk_texture_view *views[DOE_VK_MAX_BINDINGS];
    doe_vk_sampler *samplers[DOE_VK_MAX_BINDINGS];
    memcpy(views, use->views, sizeof(views));
    memcpy(samplers, use->samplers, sizeof(samplers));
    memset(use->views, 0, sizeof(use->views));
    memset(use->samplers, 0, sizeof(use->samplers));
    use->next = NULL;
    bool cached = false;
    if (use->pool != 0 && use->written && descriptor_reuse_enabled(context) && !context->lost) {
        bool duplicate = false;
        for (uint32_t i = 0; i < context->retained_dispatch_count; ++i) {
            if (same_use(context->retained_dispatches[i], pipeline, use->bindings, use->binding_count)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            if (context->retained_dispatch_count == DOE_VK_RETAINED_DISPATCHES) {
                doe_vk_dispatch_use *oldest = context->retained_dispatches[0];
                memmove(&context->retained_dispatches[0], &context->retained_dispatches[1],
                    (DOE_VK_RETAINED_DISPATCHES - 1u) * sizeof(*context->retained_dispatches));
                --context->retained_dispatch_count;
                free_dispatch_sets(context, oldest);
                free(oldest);
            }
            context->retained_dispatches[context->retained_dispatch_count++] = use;
            cached = true;
        }
    }
    if (!cached) {
        free_dispatch_sets(context, use);
        free(use);
    }
    /* Release references after publishing into retained_dispatches so any
     * terminal resource destruction evicts the cached entry synchronously. */
    for (uint32_t i = 0; i < DOE_VK_MAX_BINDINGS; ++i) {
        doe_vk_texture_view_release(views[i]);
        doe_vk_sampler_release(samplers[i]);
    }
    doe_vk_pipeline_release(pipeline);
}
