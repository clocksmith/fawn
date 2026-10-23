#include "internal.h"

static doe_vk_status validate_layout(const doe_vk_context *context,
    const doe_vk_binding_layout *layout, size_t count, uint32_t *groups) {
    if (count > DOE_VK_MAX_BINDINGS || (count != 0 && layout == NULL))
        return DOE_VK_INVALID_ARGUMENT;
    uint32_t counts[5] = {0};
    uint32_t group_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (layout[i].group >= DOE_VK_MAX_GROUPS ||
            doe_vk_descriptor_type(layout[i].kind) == UINT32_MAX)
            return DOE_VK_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j) {
            if (layout[i].group == layout[j].group && layout[i].binding == layout[j].binding)
                return DOE_VK_INVALID_ARGUMENT;
        }
        ++counts[(uint32_t)layout[i].kind];
        if (layout[i].group + 1 > group_count) group_count = layout[i].group + 1;
    }
    const VkPhysicalDeviceLimits *limits = &context->properties.limits;
    if (group_count > limits->maxBoundDescriptorSets || count > limits->maxPerStageResources ||
        counts[0] > limits->maxPerStageDescriptorUniformBuffers ||
        counts[0] > limits->maxDescriptorSetUniformBuffers ||
        counts[1] > limits->maxPerStageDescriptorStorageBuffers ||
        counts[1] > limits->maxDescriptorSetStorageBuffers ||
        counts[2] > limits->maxPerStageDescriptorSampledImages ||
        counts[2] > limits->maxDescriptorSetSampledImages ||
        counts[3] > limits->maxPerStageDescriptorStorageImages ||
        counts[3] > limits->maxDescriptorSetStorageImages ||
        counts[4] > limits->maxPerStageDescriptorSamplers ||
        counts[4] > limits->maxDescriptorSetSamplers) return DOE_VK_UNSUPPORTED;
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
                .descriptorType = doe_vk_descriptor_type(layout[i].kind),
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

