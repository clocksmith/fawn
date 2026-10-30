/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "vk_abi_subgroup.h"

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

bool doe_vk_shader_valid(const doe_vk_shader *shader) {
    return shader != NULL && shader->words != NULL && shader->word_count >= 5 &&
        shader->word_count <= SIZE_MAX / sizeof(uint32_t) &&
        (uintptr_t)shader->words % _Alignof(uint32_t) == 0 &&
        shader->entrypoint != NULL && shader->entrypoint[0] != '\0' &&
        shader->words[0] == UINT32_C(0x07230203) && shader->words[3] != 0 && shader->words[4] == 0;
}

doe_vk_status doe_vk_shader_module(doe_vk_context *context, const doe_vk_shader *shader,
    VkShaderModule *out, doe_vk_error *error) {
    if (!doe_vk_shader_valid(shader)) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "SPIR-V shader", error);
    const VkShaderModuleCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader->word_count * sizeof(uint32_t), .pCode = shader->words };
    return doe_vk_result(context, context->vk.vkCreateShaderModule(context->device, &info, NULL, out), "vkCreateShaderModule", error);
}

doe_vk_status doe_vk_pipeline_layout_create(doe_vk_pipeline *pipeline,
    const doe_vk_binding_layout *layout, size_t count, uint32_t stages, doe_vk_error *error) {
    doe_vk_context *context = pipeline->context;
    doe_vk_status status = validate_layout(context, layout, count, &pipeline->group_count);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "pipeline binding layout", error);
    pipeline->binding_count = (uint32_t)count;
    if (count != 0) memcpy(pipeline->bindings, layout, count * sizeof(*layout));
    for (uint32_t group = 0; group < pipeline->group_count; ++group) {
        VkDescriptorSetLayoutBinding bindings[DOE_VK_MAX_BINDINGS];
        uint32_t group_bindings = 0;
        for (size_t i = 0; i < count; ++i) {
            if (layout[i].group != group) continue;
            bindings[group_bindings++] = (VkDescriptorSetLayoutBinding){
                .binding = layout[i].binding,
                .descriptorType = doe_vk_descriptor_type(layout[i].kind),
                .descriptorCount = 1,
                .stageFlags = stages
            };
        }
        const VkDescriptorSetLayoutCreateInfo set = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = group_bindings,
            .pBindings = group_bindings != 0 ? bindings : NULL
        };
        status = doe_vk_result(context, context->vk.vkCreateDescriptorSetLayout(context->device, &set, NULL, &pipeline->sets[group]), "vkCreateDescriptorSetLayout", error);
        if (status != DOE_VK_OK) return status;
    }
    if (pipeline->immediate_bytes > 64 || pipeline->immediate_bytes % 4 ||
        pipeline->immediate_bytes > context->properties.limits.maxPushConstantsSize ||
        (pipeline->immediate_stages & ~stages) ||
        ((pipeline->immediate_bytes != 0) != (pipeline->immediate_stages != 0)))
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "immediate pipeline range", error);
    const VkPushConstantRange immediate = {.stageFlags=pipeline->immediate_stages,.size=pipeline->immediate_bytes};
    const VkPipelineLayoutCreateInfo pipeline_layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = pipeline->immediate_bytes ? 1u : 0u,
        .pPushConstantRanges = pipeline->immediate_bytes ? &immediate : NULL,
        .setLayoutCount = pipeline->group_count,
        .pSetLayouts = pipeline->group_count != 0 ? pipeline->sets : NULL
    };
    status = doe_vk_result(context, context->vk.vkCreatePipelineLayout(context->device, &pipeline_layout, NULL, &pipeline->layout), "vkCreatePipelineLayout", error);
    if (status != DOE_VK_OK) return status;
    return DOE_VK_OK;
}

/* A compute pipeline is fully determined by its creation input and never
 * mutated afterwards, so an identical request shares it: repeated creation of
 * the same shader (one WebGPU pipeline per call, as helpers do) neither
 * recompiles nor re-specializes it in the driver. The context also keeps the
 * DOE_VK_RETAINED_PIPELINES most recently released ones, which hold no context
 * reference and are destroyed with the device. Exact comparison of the SPIR-V
 * words, entry point and binding layout, never a hash alone. */
static bool same_layout(const doe_vk_pipeline *pipeline, const doe_vk_binding_layout *layout, size_t count) {
    if (pipeline->binding_count != count) return false;
    for (size_t i = 0; i < count; ++i) {
        const doe_vk_binding_layout *a = &pipeline->bindings[i], *b = &layout[i];
        if (a->group != b->group || a->binding != b->binding || a->kind != b->kind) return false;
    }
    return true;
}

static void unlink_compute(doe_vk_context *context, doe_vk_pipeline *pipeline) {
    for (doe_vk_pipeline **link = &context->compute_pipelines; *link; link = &(*link)->next_compute) {
        if (*link != pipeline) continue;
        *link = pipeline->next_compute;
        pipeline->next_compute = NULL;
        return;
    }
}

static void destroy_objects(doe_vk_context *context, doe_vk_pipeline *pipeline) {
    free(pipeline->spirv);
    free(pipeline->entrypoint);
    if (pipeline->pipeline != 0) context->vk.vkDestroyPipeline(context->device, pipeline->pipeline, NULL);
    if (pipeline->render_pass != 0) context->vk.vkDestroyRenderPass(context->device, pipeline->render_pass, NULL);
    if (pipeline->layout != 0) context->vk.vkDestroyPipelineLayout(context->device, pipeline->layout, NULL);
    for (uint32_t i = 0; i < pipeline->group_count; ++i) {
        if (pipeline->sets[i] != 0) context->vk.vkDestroyDescriptorSetLayout(context->device, pipeline->sets[i], NULL);
    }
    free(pipeline);
}

void doe_vk_pipeline_cache_clear(doe_vk_context *context) {
    while (context->compute_pipelines != NULL) {
        doe_vk_pipeline *pipeline = context->compute_pipelines;
        context->compute_pipelines = pipeline->next_compute;
        destroy_objects(context, pipeline); /* Released: no live owner remains. */
    }
    context->retained_pipelines = 0;
}

static doe_vk_pipeline *shared_compute(doe_vk_context *context, const doe_vk_shader *shader,
    const doe_vk_binding_layout *layout, size_t count, uint32_t subgroup, uint32_t immediate_bytes) {
    for (doe_vk_pipeline *pipeline = context->compute_pipelines; pipeline; pipeline = pipeline->next_compute) {
        if (pipeline->immediate_bytes == immediate_bytes && pipeline->required_subgroup_size == subgroup && pipeline->word_count == shader->word_count && same_layout(pipeline, layout, count) &&
            strcmp(pipeline->entrypoint, shader->entrypoint) == 0 &&
            memcmp(pipeline->spirv, shader->words, shader->word_count * sizeof(uint32_t)) == 0)
            return pipeline;
    }
    return NULL;
}

static bool keep_input(doe_vk_pipeline *pipeline, const doe_vk_shader *shader) {
    const size_t name = strlen(shader->entrypoint) + 1;
    pipeline->spirv = malloc(shader->word_count * sizeof(uint32_t));
    pipeline->entrypoint = malloc(name);
    if (pipeline->spirv == NULL || pipeline->entrypoint == NULL) return false;
    memcpy(pipeline->spirv, shader->words, shader->word_count * sizeof(uint32_t));
    memcpy(pipeline->entrypoint, shader->entrypoint, name);
    pipeline->word_count = shader->word_count;
    return true;
}

doe_vk_status doe_vk_pipeline_create_blob(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, const char *entrypoint,
    const doe_vk_binding_layout *layout, size_t binding_count, uint32_t required_subgroup_size,
    uint32_t immediate_bytes, doe_vk_pipeline_blob *blob, doe_vk_pipeline **out, doe_vk_error *error) {
    if (blob) { blob->produced = NULL; blob->produced_size = 0; }
    const doe_vk_shader shader = { spirv, word_count, entrypoint };
    if (context == NULL || out == NULL || !doe_vk_shader_valid(&shader))
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "pipeline descriptor", error);
    if (required_subgroup_size && (!context->caps.subgroup_size_control ||
        !(context->caps.subgroup_required_stages & VK_SHADER_STAGE_COMPUTE_BIT) ||
        (required_subgroup_size & (required_subgroup_size - 1)) ||
        required_subgroup_size < context->caps.subgroup_min_size ||
        required_subgroup_size > context->caps.subgroup_max_size))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "required subgroup size", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "pipeline context", error);
    if (binding_count <= DOE_VK_MAX_BINDINGS && (binding_count == 0 || layout != NULL)) {
        doe_vk_pipeline *shared = shared_compute(context, &shader, layout, binding_count, required_subgroup_size, immediate_bytes);
        if (shared != NULL && shared->references == 0) {
            /* A retained pipeline takes its context reference back. */
            if (doe_vk_context_retain(context) == DOE_VK_OK) {
                shared->references = 1;
                --context->retained_pipelines;
                *out = shared;
                return doe_vk_fail(DOE_VK_OK, "pipeline ready", error);
            }
        } else if (shared != NULL && doe_vk_pipeline_retain(shared) == DOE_VK_OK) {
            *out = shared;
            return doe_vk_fail(DOE_VK_OK, "pipeline ready", error);
        }
    }
    doe_vk_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (pipeline == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "pipeline owner", error);
    doe_vk_status status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(pipeline);
        return doe_vk_fail(status, "pipeline context retain", error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->required_subgroup_size = required_subgroup_size;
    pipeline->immediate_bytes = immediate_bytes;
    pipeline->immediate_stages = immediate_bytes ? VK_SHADER_STAGE_COMPUTE_BIT : 0;
    VkShaderModule module = 0;
    status = doe_vk_pipeline_layout_create(pipeline, layout, binding_count, VK_SHADER_STAGE_COMPUTE_BIT, error);
    if (status != DOE_VK_OK) goto failed;
    if (!keep_input(pipeline, &shader)) {
        status = doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "pipeline creation input", error);
        goto failed;
    }
    status = doe_vk_shader_module(context, &shader, &module, error);
    if (status != DOE_VK_OK) goto failed;
    const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
        .requiredSubgroupSize = required_subgroup_size};
    const VkComputePipelineCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .pNext = required_subgroup_size ? &subgroup : NULL,
            .module = module,
            .pName = (const uint8_t *)entrypoint
        },
        .layout = pipeline->layout,
        .basePipelineIndex = -1
    };
    VkPipelineCache cache = context->driver_cache;
    const bool temporary = blob && context->pipeline_blobs;
    bool loaded = false;
    if (temporary) {
        status = doe_vk_pipeline_blob_open(context, blob, &cache, &loaded, error);
        if (status != DOE_VK_OK) goto failed;
    }
    status = doe_vk_result(context, context->vk.vkCreateComputePipelines(context->device, cache, 1, &create, NULL, &pipeline->pipeline), "vkCreateComputePipelines", error);
    if (temporary) doe_vk_pipeline_blob_close(context, cache, loaded, status == DOE_VK_OK, blob);
    else doe_vk_driver_cache_record(context, status == DOE_VK_OK);
    if (status != DOE_VK_OK) goto failed;
    context->vk.vkDestroyShaderModule(context->device, module, NULL);
    pipeline->next_compute = context->compute_pipelines;
    context->compute_pipelines = pipeline;
    *out = pipeline;
    return doe_vk_fail(DOE_VK_OK, "pipeline ready", error);
failed:
    if (module != 0) context->vk.vkDestroyShaderModule(context->device, module, NULL);
    doe_vk_pipeline_release(pipeline);
    return status;
}

doe_vk_status doe_vk_pipeline_create_with_immediates(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, const char *entrypoint,
    const doe_vk_binding_layout *layout, size_t binding_count, uint32_t required_subgroup_size,
    uint32_t immediate_bytes, doe_vk_pipeline **out, doe_vk_error *error) {
    return doe_vk_pipeline_create_blob(context,spirv,word_count,entrypoint,layout,binding_count,
        required_subgroup_size,immediate_bytes,NULL,out,error);
}

doe_vk_status doe_vk_pipeline_create_with_subgroup(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, const char *entrypoint,
    const doe_vk_binding_layout *layout, size_t binding_count, uint32_t required_subgroup_size,
    doe_vk_pipeline **out, doe_vk_error *error) {
    return doe_vk_pipeline_create_with_immediates(context,spirv,word_count,entrypoint,
        layout,binding_count,required_subgroup_size,0,out,error);
}

doe_vk_status doe_vk_pipeline_create(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, const char *entrypoint,
    const doe_vk_binding_layout *layout, size_t binding_count,
    doe_vk_pipeline **out, doe_vk_error *error) {
    return doe_vk_pipeline_create_with_subgroup(context, spirv, word_count, entrypoint,
        layout, binding_count, 0, out, error);
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
    unlink_compute(context, pipeline);
    /* Only a completely created compute pipeline is retained; a failed creation
     * (no native pipeline) and a lost context destroy now. */
    if (pipeline->spirv == NULL || pipeline->pipeline == 0 || context->lost) {
        destroy_objects(context, pipeline);
        doe_vk_context_release(context);
        return;
    }
    /* Retain it as the most recent released pipeline; evict the oldest beyond
     * the bound. It drops its context reference last, so a final context
     * release destroys it with the other retained pipelines. */
    pipeline->next_compute = context->compute_pipelines;
    context->compute_pipelines = pipeline;
    if (++context->retained_pipelines > DOE_VK_RETAINED_PIPELINES) {
        doe_vk_pipeline *oldest = NULL;
        for (doe_vk_pipeline *entry = context->compute_pipelines; entry; entry = entry->next_compute)
            if (entry->references == 0) oldest = entry;
        unlink_compute(context, oldest);
        destroy_objects(context, oldest);
        --context->retained_pipelines;
    }
    doe_vk_context_release(context);
}
