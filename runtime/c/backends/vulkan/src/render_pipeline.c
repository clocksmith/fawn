/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <math.h>

static bool fragment_absent(const doe_vk_render_pipeline_desc *desc) {
    return desc->fragment.words == NULL && desc->fragment.word_count == 0 &&
        desc->fragment.entrypoint == NULL;
}

static bool stencil_face_valid(doe_vk_stencil_face face) {
    return face.fail_operation <= 7 && face.pass_operation <= 7 &&
        face.depth_fail_operation <= 7 && face.compare_operation <= 7;
}
static bool stencil_face_writes(doe_vk_stencil_face face) {
    return face.fail_operation != 0 || face.pass_operation != 0 || face.depth_fail_operation != 0;
}
bool doe_vk_render_stencil_writes(const doe_vk_render_pipeline_desc *desc) {
    if (!desc->stencil_test || desc->stencil_write_mask == 0) return false;
    bool front = !(desc->cull_mask & 1) && stencil_face_writes(desc->stencil_front);
    bool back = !(desc->cull_mask & 2) && stencil_face_writes(desc->stencil_back);
    return front || back;
}

static doe_vk_status validate_depth_stencil(doe_vk_context *context,
    const doe_vk_render_pipeline_desc *desc) {
    if (desc->depth_stencil_format == 0) {
        return desc->depth_test || desc->depth_write || desc->stencil_test ?
            DOE_VK_INVALID_ARGUMENT : DOE_VK_OK;
    }
    uint32_t aspects = doe_vk_format_aspects(desc->depth_stencil_format);
    if (aspects == VK_IMAGE_ASPECT_COLOR_BIT) return DOE_VK_INVALID_ARGUMENT;
    if ((desc->depth_test || desc->depth_write) && !(aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
        return DOE_VK_INVALID_ARGUMENT;
    if (desc->depth_write && !desc->depth_test) return DOE_VK_INVALID_ARGUMENT;
    if (desc->depth_test && desc->depth_compare > 7) return DOE_VK_INVALID_ARGUMENT;
    if (desc->stencil_test && (!(aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ||
        !stencil_face_valid(desc->stencil_front) || !stencil_face_valid(desc->stencil_back)))
        return DOE_VK_INVALID_ARGUMENT;
    doe_vk_format_caps caps;
    doe_vk_status status = doe_vk_texture_format_caps(context, desc->depth_stencil_format, &caps);
    if (status != DOE_VK_OK) return status;
    return caps.depth_stencil_attachment && (caps.attachment_sample_counts & doe_vk_samples(desc->sample_count)) ?
        DOE_VK_OK : DOE_VK_UNSUPPORTED;
}
static VkStencilOpState stencil_state(doe_vk_stencil_face face,
    const doe_vk_render_pipeline_desc *desc) {
    return (VkStencilOpState){.failOp = face.fail_operation, .passOp = face.pass_operation,
        .depthFailOp = face.depth_fail_operation, .compareOp = face.compare_operation,
        .compareMask = desc->stencil_read_mask, .writeMask = desc->stencil_write_mask};
}

typedef struct color_targets {
    uint32_t count;
    bool used;
    doe_vk_color_target slots[DOE_VK_MAX_COLOR_ATTACHMENTS];
} color_targets;

/* Legacy single-color fields are slot 0 only; never combined with slots. */
static doe_vk_status normalize_targets(const doe_vk_render_pipeline_desc *desc, color_targets *out) {
    color_targets targets = {0};
    const bool legacy = desc->color_format != 0 || desc->color_write_mask != 0 || desc->blend_enabled;
    if (desc->color_target_count != 0) {
        if (legacy || desc->color_target_count > DOE_VK_MAX_COLOR_ATTACHMENTS) return DOE_VK_INVALID_ARGUMENT;
        targets.count = desc->color_target_count;
        memcpy(targets.slots, desc->color_targets, targets.count * sizeof(*targets.slots));
    } else if (desc->color_format != 0) {
        targets.count = 1;
        targets.slots[0] = (doe_vk_color_target){desc->color_format, desc->color_write_mask, desc->blend_enabled,
            desc->color_source_factor, desc->color_destination_factor, desc->color_operation,
            desc->alpha_source_factor, desc->alpha_destination_factor, desc->alpha_operation};
    } else if (legacy) return DOE_VK_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < targets.count; ++i) {
        const doe_vk_color_target *slot = &targets.slots[i];
        if (slot->write_mask > 15) return DOE_VK_INVALID_ARGUMENT;
        if (slot->format == 0) {
            if (slot->write_mask != 0 || slot->blend_enabled) return DOE_VK_INVALID_ARGUMENT;
            continue;
        }
        targets.used = true;
        if (slot->blend_enabled && (slot->color_source_factor > 18 || slot->color_destination_factor > 18 ||
            slot->alpha_source_factor > 18 || slot->alpha_destination_factor > 18 ||
            slot->color_operation > 4 || slot->alpha_operation > 4)) return DOE_VK_UNSUPPORTED;
    }
    *out = targets;
    return DOE_VK_OK;
}

static VkPipelineColorBlendAttachmentState blend_state(const doe_vk_color_target *slot) {
    return (VkPipelineColorBlendAttachmentState){.blendEnable = slot->blend_enabled,
        .srcColorBlendFactor = slot->color_source_factor, .dstColorBlendFactor = slot->color_destination_factor,
        .colorBlendOp = slot->color_operation, .srcAlphaBlendFactor = slot->alpha_source_factor,
        .dstAlphaBlendFactor = slot->alpha_destination_factor, .alphaBlendOp = slot->alpha_operation,
        .colorWriteMask = slot->write_mask};
}

/* Without enabled independentBlend every element must be identical: used slots
 * must already agree, and unused slots take that one state (they write nothing). */
static doe_vk_status blend_states(const doe_vk_context *context, const color_targets *targets,
    VkPipelineColorBlendAttachmentState out[DOE_VK_MAX_COLOR_ATTACHMENTS]) {
    const VkPipelineColorBlendAttachmentState *shared = NULL;
    for (uint32_t i = 0; i < targets->count; ++i) {
        const doe_vk_color_target *slot=&targets->slots[i];
        if (slot->blend_enabled && (slot->color_source_factor>=15 || slot->color_destination_factor>=15 ||
            slot->alpha_source_factor>=15 || slot->alpha_destination_factor>=15) &&
            (!context->caps.dual_source_blend || targets->count!=1 || i!=0)) return DOE_VK_UNSUPPORTED;
        out[i] = blend_state(&targets->slots[i]);
        if (targets->slots[i].format == 0 || context->caps.independent_blend) continue;
        if (shared != NULL && memcmp(shared, &out[i], sizeof(out[i])) != 0) return DOE_VK_UNSUPPORTED;
        if (shared == NULL) shared = &out[i];
    }
    if (shared != NULL)
        for (uint32_t i = 0; i < targets->count; ++i) if (targets->slots[i].format == 0) out[i] = *shared;
    return DOE_VK_OK;
}

static doe_vk_status validate_pipeline(doe_vk_context *context,
    const doe_vk_render_pipeline_desc *desc, const color_targets *targets) {
    if (desc == NULL || !doe_vk_shader_valid(&desc->vertex) ||
        (!fragment_absent(desc) && !doe_vk_shader_valid(&desc->fragment)) ||
        desc->topology < DOE_VK_POINTS || desc->topology > DOE_VK_TRIANGLE_STRIP ||
        desc->cull_mask > 3 ||
        desc->vertex_buffer_count > DOE_VK_MAX_VERTEX_BUFFERS ||
        desc->attribute_count > DOE_VK_MAX_VERTEX_ATTRIBUTES ||
        (desc->vertex_buffer_count != 0 && desc->vertex_buffers == NULL) ||
        (desc->attribute_count != 0 && desc->attributes == NULL)) return DOE_VK_INVALID_ARGUMENT;
    if (!(context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT)) return DOE_VK_UNSUPPORTED;
    /* Without maintenance5 an unwritten PointSize is undefined; never patch
     * shaders to supply it. */
    if (desc->topology == DOE_VK_POINTS && !context->caps.maintenance5) return DOE_VK_UNSUPPORTED;
    if (desc->unclipped_depth && !context->caps.depth_clamp) return DOE_VK_UNSUPPORTED;
    if (!isfinite(desc->depth_bias_slope_scale) || !isfinite(desc->depth_bias_clamp)) return DOE_VK_INVALID_ARGUMENT;
    if (desc->depth_bias_clamp != 0 && !context->caps.depth_bias_clamp) return DOE_VK_UNSUPPORTED;
    if (!targets->used) {
        if (desc->depth_stencil_format == 0 || desc->alpha_to_coverage) return DOE_VK_INVALID_ARGUMENT;
    } else if (fragment_absent(desc)) return DOE_VK_INVALID_ARGUMENT;
    if (targets->count > context->caps.max_color_attachments) return DOE_VK_UNSUPPORTED;
    for (unsigned i = 0; i < 4; ++i) if (!isfinite(desc->blend_constant[i])) return DOE_VK_INVALID_ARGUMENT;
    uint32_t samples = doe_vk_samples(desc->sample_count);
    if (samples > 64 || (samples & (samples - 1)) != 0 || (desc->alpha_to_coverage && samples == 1))
        return DOE_VK_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < targets->count; ++i) {
        const doe_vk_color_target *slot = &targets->slots[i];
        if (slot->format == 0) continue;
        doe_vk_format_caps format;
        doe_vk_status status = doe_vk_texture_format_caps(context, slot->format, &format);
        if (status != DOE_VK_OK) return status;
        /* WebGPU checks a color target's renderability and blendability here,
         * not its multisample support: a pipeline whose sample count the format
         * lacks is valid, and no attachment it could render to can exist. */
        if (!format.color_attachment || (slot->blend_enabled && !format.blendable)) return DOE_VK_UNSUPPORTED;
    }
    doe_vk_status status = validate_depth_stencil(context, desc);
    if (status != DOE_VK_OK) return status;
    const VkPhysicalDeviceLimits *limits = &context->properties.limits;
    if (desc->vertex_buffer_count > limits->maxVertexInputBindings ||
        desc->attribute_count > limits->maxVertexInputAttributes) return DOE_VK_UNSUPPORTED;
    for (size_t i = 0; i < desc->vertex_buffer_count; ++i) {
        if (desc->vertex_buffers[i].stride > limits->maxVertexInputBindingStride ||
            desc->vertex_buffers[i].stride % 4 != 0) return DOE_VK_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < desc->attribute_count; ++i) {
        const doe_vk_vertex_attribute *attribute = &desc->attributes[i];
        /* WebGPU aligns attribute offsets to min(4, format byte size). */
        uint32_t bytes = doe_vk_vertex_format_bytes(attribute->format);
        if (bytes == 0 || attribute->slot >= desc->vertex_buffer_count ||
            attribute->location >= limits->maxVertexInputAttributes ||
            attribute->offset > limits->maxVertexInputAttributeOffset || attribute->offset % (bytes < 4 ? bytes : 4) != 0)
            return DOE_VK_INVALID_ARGUMENT;
        uint32_t stride = desc->vertex_buffers[attribute->slot].stride;
        if (attribute->offset > UINT32_MAX - bytes ||
            (stride != 0 && !doe_vk_range(stride, attribute->offset, bytes))) return DOE_VK_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j) {
            if (attribute->location == desc->attributes[j].location) return DOE_VK_INVALID_ARGUMENT;
        }
        VkFormatProperties properties = {0};
        context->vk.vkGetPhysicalDeviceFormatProperties(context->physical, (uint32_t)attribute->format, &properties);
        if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) return DOE_VK_UNSUPPORTED;
    }
    return DOE_VK_OK;
}

doe_vk_status doe_vk_render_pipeline_create(doe_vk_context *context,
    const doe_vk_render_pipeline_desc *desc, doe_vk_pipeline **out, doe_vk_error *error) {
    if (context == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "graphics descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "graphics context", error);
    color_targets targets;
    doe_vk_status status = desc != NULL ? normalize_targets(desc, &targets) : DOE_VK_INVALID_ARGUMENT;
    if (status == DOE_VK_OK) status = validate_pipeline(context, desc, &targets);
    VkPipelineColorBlendAttachmentState attachments[DOE_VK_MAX_COLOR_ATTACHMENTS];
    if (status == DOE_VK_OK) status = blend_states(context, &targets, attachments);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "graphics admission", error);
    doe_vk_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (pipeline == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "graphics owner", error);
    status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(pipeline);
        return doe_vk_fail(status, "graphics context retain", error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->graphics = true;
    pipeline->immediate_bytes = desc->immediate_bytes;
    pipeline->immediate_stages = desc->immediate_stages;
    memcpy(pipeline->blend_constant, desc->blend_constant, sizeof(pipeline->blend_constant));
    pipeline->color_count = targets.count;
    doe_vk_render_layout layout = {.color_count = targets.count, .depth_stencil = desc->depth_stencil_format,
        .samples = doe_vk_samples(desc->sample_count)};
    for (uint32_t i = 0; i < targets.count; ++i) layout.colors[i] = pipeline->color_formats[i] = targets.slots[i].format;
    pipeline->depth_stencil_format = desc->depth_stencil_format;
    pipeline->sample_count = doe_vk_samples(desc->sample_count);
    pipeline->depth_write = desc->depth_write;
    pipeline->stencil_test = desc->stencil_test;
    pipeline->stencil_write = doe_vk_render_stencil_writes(desc);
    pipeline->vertex_count = (uint32_t)desc->vertex_buffer_count;
    if (pipeline->vertex_count != 0) memcpy(pipeline->vertices, desc->vertex_buffers,
        pipeline->vertex_count * sizeof(*desc->vertex_buffers));
    VkShaderModule vertex = 0, fragment = 0;
    bool has_fragment = !fragment_absent(desc);
    VkFlags shader_stages = VK_SHADER_STAGE_VERTEX_BIT | (has_fragment ? VK_SHADER_STAGE_FRAGMENT_BIT : 0);
    status = doe_vk_pipeline_layout_create(pipeline, desc->bindings, desc->binding_count, shader_stages, error);
    if (status != DOE_VK_OK) goto failed;
    /* Vulkan single-subpass compatibility ignores resolve attachment references;
     * this pipeline pass is also compatible with the recorder's resolve pass. */
    status = doe_vk_render_pass_create(context, &layout, NULL, NULL, &pipeline->render_pass, error);
    if (status != DOE_VK_OK) goto failed;
    status = doe_vk_shader_module(context, &desc->vertex, &vertex, error);
    if (status != DOE_VK_OK) goto failed;
    if (has_fragment) {
        status = doe_vk_shader_module(context, &desc->fragment, &fragment, error);
        if (status != DOE_VK_OK) goto failed;
    }
    const VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex, .pName = (const uint8_t *)desc->vertex.entrypoint },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment, .pName = (const uint8_t *)desc->fragment.entrypoint }
    };
    VkVertexInputBindingDescription bindings[DOE_VK_MAX_VERTEX_BUFFERS];
    for (uint32_t i = 0; i < pipeline->vertex_count; ++i)
        bindings[i] = (VkVertexInputBindingDescription){i, pipeline->vertices[i].stride, pipeline->vertices[i].per_instance ? 1 : 0};
    VkVertexInputAttributeDescription attributes[DOE_VK_MAX_VERTEX_ATTRIBUTES];
    for (size_t i = 0; i < desc->attribute_count; ++i) {
        const doe_vk_vertex_attribute *attribute = &desc->attributes[i];
        attributes[i] = (VkVertexInputAttributeDescription){attribute->location, attribute->slot,
            (uint32_t)attribute->format, attribute->offset};
        uint32_t end = attribute->offset + doe_vk_vertex_format_bytes(attribute->format);
        if (end > pipeline->vertex_attribute_end[attribute->slot]) pipeline->vertex_attribute_end[attribute->slot] = end;
    }
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = pipeline->vertex_count, .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = (uint32_t)desc->attribute_count, .pVertexAttributeDescriptions = attributes
    };
    /* WebGPU strip topologies always restart at the all-ones index for indexed
     * draws; native restart only affects indexed draws. Lists never restart,
     * so no optional list-restart feature is needed. */
    const bool strip = desc->topology == DOE_VK_LINE_STRIP || desc->topology == DOE_VK_TRIANGLE_STRIP;
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = (uint32_t)desc->topology,
        .primitiveRestartEnable = strip ? 1 : 0
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .depthClampEnable = desc->unclipped_depth,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .depthBiasEnable = desc->depth_bias != 0 || desc->depth_bias_slope_scale != 0 || desc->depth_bias_clamp != 0,
        .depthBiasConstantFactor = (float)desc->depth_bias, .depthBiasClamp = desc->depth_bias_clamp,
        .depthBiasSlopeFactor = desc->depth_bias_slope_scale,
        .cullMode = desc->cull_mask, .frontFace = desc->clockwise_front ? 1 : 0, .lineWidth = 1.0f
    };
    const uint32_t sample_mask[2] = {(uint32_t)desc->sample_mask, (uint32_t)(desc->sample_mask >> 32)};
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = pipeline->sample_count,
        .pSampleMask = desc->override_sample_mask ? sample_mask : NULL, .alphaToCoverageEnable = desc->alpha_to_coverage
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = targets.count, .pAttachments = targets.count != 0 ? attachments : NULL
    };
    memcpy(blend.blendConstants, desc->blend_constant, sizeof(blend.blendConstants));
    const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc->depth_test, .depthWriteEnable = desc->depth_write,
        .depthCompareOp = desc->depth_compare, .stencilTestEnable = desc->stencil_test,
        .front = stencil_state(desc->stencil_front, desc), .back = stencil_state(desc->stencil_back, desc)
    };
    const uint32_t states[4] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = desc->stencil_test ? 4 : 3, .pDynamicStates = states
    };
    const VkGraphicsPipelineCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = has_fragment ? 2 : 1, .pStages = stages,
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &rasterization, .pMultisampleState = &multisample, .pColorBlendState = &blend,
        .pDepthStencilState = desc->depth_stencil_format != 0 ? &depth_stencil : NULL,
        .pDynamicState = &dynamic, .layout = pipeline->layout, .renderPass = pipeline->render_pass,
        .basePipelineIndex = -1
    };
    status = doe_vk_result(context, context->vk.vkCreateGraphicsPipelines(context->device, context->driver_cache, 1,
        &create, NULL, &pipeline->pipeline), "vkCreateGraphicsPipelines", error);
    doe_vk_driver_cache_record(context, status == DOE_VK_OK);
    if (status != DOE_VK_OK) goto failed;
    context->vk.vkDestroyShaderModule(context->device, vertex, NULL);
    if (fragment != 0) context->vk.vkDestroyShaderModule(context->device, fragment, NULL);
    *out = pipeline;
    return doe_vk_fail(DOE_VK_OK, "graphics ready", error);
failed:
    if (vertex != 0) context->vk.vkDestroyShaderModule(context->device, vertex, NULL);
    if (fragment != 0) context->vk.vkDestroyShaderModule(context->device, fragment, NULL);
    doe_vk_pipeline_release(pipeline);
    return status;
}
