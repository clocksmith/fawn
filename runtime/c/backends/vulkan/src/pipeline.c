#include "internal.h"

static doe_vk_status validate_layout(const doe_vk_context *context,
    const doe_vk_binding_layout *layout, size_t count, uint32_t *groups) {
    if (count > DOE_VK_MAX_BINDINGS || (count != 0 && layout == NULL))
        return DOE_VK_INVALID_ARGUMENT;
    uint32_t uniform_count = 0;
    uint32_t storage_count = 0;
    uint32_t group_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (layout[i].group >= DOE_VK_MAX_GROUPS ||
            (layout[i].kind != DOE_VK_BINDING_UNIFORM && layout[i].kind != DOE_VK_BINDING_STORAGE))
            return DOE_VK_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j) {
            if (layout[i].group == layout[j].group && layout[i].binding == layout[j].binding)
                return DOE_VK_INVALID_ARGUMENT;
        }
        if (layout[i].kind == DOE_VK_BINDING_UNIFORM) ++uniform_count;
        else ++storage_count;
        if (layout[i].group + 1 > group_count) group_count = layout[i].group + 1;
    }
    const VkPhysicalDeviceLimits *limits = &context->properties.limits;
    if (group_count > limits->maxBoundDescriptorSets ||
        uniform_count > limits->maxPerStageDescriptorUniformBuffers ||
        uniform_count > limits->maxDescriptorSetUniformBuffers ||
        storage_count > limits->maxPerStageDescriptorStorageBuffers ||
        storage_count > limits->maxDescriptorSetStorageBuffers ||
        count > limits->maxPerStageResources) return DOE_VK_UNSUPPORTED;
    *groups = group_count;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_pipeline_create(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, const char *entrypoint,
    const doe_vk_binding_layout *layout, size_t binding_count,
    doe_vk_pipeline **out, doe_vk_error *error) {
    if (context == NULL || spirv == NULL || out == NULL || entrypoint == NULL ||
        entrypoint[0] == '\0' || word_count < 5 || word_count > SIZE_MAX / sizeof(uint32_t) ||
        (uintptr_t)spirv % _Alignof(uint32_t) != 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "pipeline descriptor", error);
    if (spirv[0] != UINT32_C(0x07230203) || spirv[3] == 0 || spirv[4] != 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "SPIR-V header", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "pipeline context", error);
    uint32_t groups = 0;
    doe_vk_status status = validate_layout(context, layout, binding_count, &groups);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "pipeline binding layout", error);
    doe_vk_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (pipeline == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "pipeline owner", error);
    status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(pipeline);
        return doe_vk_fail(status, "pipeline context retain", error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->binding_count = (uint32_t)binding_count;
    pipeline->group_count = groups;
    if (binding_count != 0) memcpy(pipeline->bindings, layout, binding_count * sizeof(*layout));
    VkShaderModule module = 0;
    const VkShaderModuleCreateInfo shader = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = word_count * sizeof(uint32_t),
        .pCode = spirv
    };
    status = doe_vk_result(context, context->vk.vkCreateShaderModule(context->device, &shader, NULL, &module), "vkCreateShaderModule", error);
    if (status != DOE_VK_OK) goto failed;
    for (uint32_t group = 0; group < groups; ++group) {
        VkDescriptorSetLayoutBinding bindings[DOE_VK_MAX_BINDINGS];
        uint32_t count = 0;
        for (size_t i = 0; i < binding_count; ++i) {
            if (layout[i].group != group) continue;
            bindings[count++] = (VkDescriptorSetLayoutBinding){
                .binding = layout[i].binding,
                .descriptorType = layout[i].kind == DOE_VK_BINDING_UNIFORM ?
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            };
        }
        const VkDescriptorSetLayoutCreateInfo set = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = count,
            .pBindings = count != 0 ? bindings : NULL
        };
        status = doe_vk_result(context, context->vk.vkCreateDescriptorSetLayout(context->device, &set, NULL, &pipeline->sets[group]), "vkCreateDescriptorSetLayout", error);
        if (status != DOE_VK_OK) goto failed;
    }
    const VkPipelineLayoutCreateInfo pipeline_layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = groups,
        .pSetLayouts = groups != 0 ? pipeline->sets : NULL
    };
    status = doe_vk_result(context, context->vk.vkCreatePipelineLayout(context->device, &pipeline_layout, NULL, &pipeline->layout), "vkCreatePipelineLayout", error);
    if (status != DOE_VK_OK) goto failed;
    const VkComputePipelineCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName = (const uint8_t *)entrypoint
        },
        .layout = pipeline->layout,
        .basePipelineIndex = -1
    };
    status = doe_vk_result(context, context->vk.vkCreateComputePipelines(context->device, 0, 1, &create, NULL, &pipeline->pipeline), "vkCreateComputePipelines", error);
    if (status != DOE_VK_OK) goto failed;
    context->vk.vkDestroyShaderModule(context->device, module, NULL);
    *out = pipeline;
    return doe_vk_fail(DOE_VK_OK, "pipeline ready", error);
failed:
    if (module != 0) context->vk.vkDestroyShaderModule(context->device, module, NULL);
    doe_vk_pipeline_release(pipeline);
    return status;
}

doe_vk_status doe_vk_pipeline_retain(doe_vk_pipeline *pipeline) {
    if (pipeline == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (pipeline->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++pipeline->references;
    return DOE_VK_OK;
}

void doe_vk_pipeline_release(doe_vk_pipeline *pipeline) {
    if (pipeline == NULL || --pipeline->references != 0) return;
    doe_vk_context *context = pipeline->context;
    if (pipeline->pipeline != 0) context->vk.vkDestroyPipeline(context->device, pipeline->pipeline, NULL);
    if (pipeline->layout != 0) context->vk.vkDestroyPipelineLayout(context->device, pipeline->layout, NULL);
    for (uint32_t i = 0; i < pipeline->group_count; ++i) {
        if (pipeline->sets[i] != 0) context->vk.vkDestroyDescriptorSetLayout(context->device, pipeline->sets[i], NULL);
    }
    free(pipeline);
    doe_vk_context_release(context);
}

static doe_vk_status validate_bindings(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings, size_t count) {
    if (pipeline == NULL || pipeline->context != commands->context ||
        count != pipeline->binding_count || (count != 0 && bindings == NULL))
        return DOE_VK_INVALID_ARGUMENT;
    for (size_t i = 0; i < count; ++i) {
        doe_vk_buffer *buffer = bindings[i].buffer;
        bool uniform = pipeline->bindings[i].kind == DOE_VK_BINDING_UNIFORM;
        uint32_t usage = uniform ? DOE_VK_BUFFER_UNIFORM : DOE_VK_BUFFER_STORAGE;
        const doe_vk_caps *caps = &commands->context->caps;
        uint64_t alignment = uniform ? caps->uniform_offset_alignment : caps->storage_offset_alignment;
        uint64_t max_range = uniform ? caps->max_uniform_buffer_range : caps->max_storage_buffer_range;
        if (buffer == NULL || buffer->context != commands->context || !(buffer->usage & usage) ||
            bindings[i].size == 0 || bindings[i].size > max_range ||
            !doe_vk_range(buffer->size, bindings[i].offset, bindings[i].size) ||
            alignment == 0 || bindings[i].offset % alignment != 0)
            return DOE_VK_INVALID_ARGUMENT;
    }
    return DOE_VK_OK;
}

doe_vk_status doe_vk_bind_dispatch(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings, size_t count) {
    doe_vk_status status = validate_bindings(commands, pipeline, bindings, count);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_dispatch_use *use = calloc(1, sizeof(*use));
    if (use == NULL) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    status = doe_vk_pipeline_retain(pipeline);
    if (status != DOE_VK_OK) {
        free(use);
        return doe_vk_poison(commands, status);
    }
    use->pipeline = pipeline;
    use->next = commands->dispatches;
    commands->dispatches = use;
    doe_vk_context *context = commands->context;
    VkDescriptorSet sets[DOE_VK_MAX_GROUPS] = {0};
    if (pipeline->group_count != 0) {
        VkDescriptorPoolSize sizes[2] = {
            { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER }
        };
        for (size_t i = 0; i < count; ++i) {
            uint32_t index = pipeline->bindings[i].kind == DOE_VK_BINDING_UNIFORM ? 0 : 1;
            ++sizes[index].descriptorCount;
        }
        VkDescriptorPoolSize nonempty[2];
        uint32_t size_count = 0;
        for (uint32_t i = 0; i < 2; ++i) {
            if (sizes[i].descriptorCount != 0) nonempty[size_count++] = sizes[i];
        }
        const VkDescriptorPoolCreateInfo pool = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = pipeline->group_count,
            .poolSizeCount = size_count,
            .pPoolSizes = nonempty
        };
        status = doe_vk_result(context, context->vk.vkCreateDescriptorPool(context->device, &pool, NULL, &use->pool), "vkCreateDescriptorPool", NULL);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
        const VkDescriptorSetAllocateInfo allocate = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = use->pool,
            .descriptorSetCount = pipeline->group_count,
            .pSetLayouts = pipeline->sets
        };
        status = doe_vk_result(context, context->vk.vkAllocateDescriptorSets(context->device, &allocate, sets), "vkAllocateDescriptorSets", NULL);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    }
    VkDescriptorBufferInfo infos[DOE_VK_MAX_BINDINGS];
    VkWriteDescriptorSet writes[DOE_VK_MAX_BINDINGS];
    for (size_t i = 0; i < count; ++i) {
        status = doe_vk_use_buffer(commands, bindings[i].buffer);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
        infos[i] = (VkDescriptorBufferInfo){ bindings[i].buffer->buffer, bindings[i].offset, bindings[i].size };
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sets[pipeline->bindings[i].group],
            .dstBinding = pipeline->bindings[i].binding,
            .descriptorCount = 1,
            .descriptorType = pipeline->bindings[i].kind == DOE_VK_BINDING_UNIFORM ?
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &infos[i]
        };
    }
    if (count != 0) context->vk.vkUpdateDescriptorSets(context->device, (uint32_t)count, writes, 0, NULL);
    doe_vk_barrier(commands);
    context->vk.vkCmdBindPipeline(commands->command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
    if (pipeline->group_count != 0) {
        context->vk.vkCmdBindDescriptorSets(commands->command, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline->layout, 0, pipeline->group_count, sets, 0, NULL);
    }
    return DOE_VK_OK;
}
