/* SPDX-License-Identifier: MIT */
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>

static unsigned expected_stages, modules, destroyed_modules, live_layouts, live_passes, live_pipelines;
static VkResult pipeline_result;
static VkBool32 expected_restart;
static void format_properties(VkPhysicalDevice physical, uint32_t format, VkFormatProperties *out) {
    (void)physical;
    assert(format == DOE_VK_FORMAT_D32_FLOAT);
    *out = (VkFormatProperties){.optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT};
}
static VkResult image_properties(VkPhysicalDevice physical, uint32_t format, uint32_t type,
    uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *out) {
    (void)physical;
    assert(format == DOE_VK_FORMAT_D32_FLOAT && type == DOE_VK_TEXTURE_2D &&
        tiling == VK_IMAGE_TILING_OPTIMAL && usage == DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT && flags == 0);
    *out = (VkImageFormatProperties){.sampleCounts = 1};
    return 0;
}
static VkResult create_layout(VkDevice device, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *out) {
    (void)device;
    (void)allocator;
    assert(info->setLayoutCount == 0);
    *out = 11;
    ++live_layouts;
    return 0;
}
static void destroy_layout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(layout == 11 && live_layouts == 1);
    --live_layouts;
}
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device;
    (void)allocator;
    assert(info->attachmentCount == 1 && info->pAttachments[0].format == DOE_VK_FORMAT_D32_FLOAT);
    assert(info->subpassCount == 1 && info->pSubpasses[0].colorAttachmentCount == 0);
    assert(info->pSubpasses[0].pColorAttachments == NULL && info->pSubpasses[0].pResolveAttachments == NULL);
    assert(info->pSubpasses[0].pDepthStencilAttachment->attachment == 0);
    *out = 12;
    ++live_passes;
    return 0;
}
static void destroy_pass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(pass == 12 && live_passes == 1);
    --live_passes;
}
static VkResult create_shader(VkDevice device, const VkShaderModuleCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkShaderModule *out) {
    (void)device;
    (void)allocator;
    assert(info->codeSize == 20);
    *out = 20 + ++modules;
    return 0;
}
static void destroy_shader(VkDevice device, VkShaderModule shader, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(shader > 20 && shader <= 20 + modules);
    ++destroyed_modules;
}
static VkResult create_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *out) {
    (void)device;
    (void)allocator;
    assert(cache == 0 && count == 1 && info->stageCount == expected_stages);
    assert(info->pStages[0].stage == VK_SHADER_STAGE_VERTEX_BIT);
    /* WebGPU strips restart at the all-ones index; lists never enable restart. */
    assert(info->pInputAssemblyState->primitiveRestartEnable == expected_restart);
    if (expected_stages == 2) assert(info->pStages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT);
    const VkPipelineColorBlendStateCreateInfo *blend = info->pColorBlendState;
    assert(blend->attachmentCount == 0 && blend->pAttachments == NULL);
    const VkPipelineDepthStencilStateCreateInfo *depth = info->pDepthStencilState;
    assert(depth->depthTestEnable && depth->depthWriteEnable && depth->depthCompareOp == 1);
    if (pipeline_result != 0) return pipeline_result;
    *out = 13;
    ++live_pipelines;
    return 0;
}
static void destroy_pipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(pipeline == 13 && live_pipelines == 1);
    --live_pipelines;
}
static void rejected(doe_vk_context *context, const doe_vk_render_pipeline_desc *desc) {
    doe_vk_pipeline *sentinel = (doe_vk_pipeline *)context, *out = sentinel;
    unsigned before = modules;
    assert(doe_vk_render_pipeline_create(context, desc, &out, NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(out == sentinel && modules == before && context->references == 1);
}
int main(void) {
    doe_vk_context context = {.references = 1};
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.caps.max_color_attachments = 8;
    context.properties.limits.framebufferDepthSampleCounts = 1;
    context.vk.vkGetPhysicalDeviceFormatProperties = format_properties;
    context.vk.vkGetPhysicalDeviceImageFormatProperties = image_properties;
    context.vk.vkCreatePipelineLayout = create_layout;
    context.vk.vkDestroyPipelineLayout = destroy_layout;
    context.vk.vkCreateRenderPass = create_pass;
    context.vk.vkDestroyRenderPass = destroy_pass;
    context.vk.vkCreateShaderModule = create_shader;
    context.vk.vkDestroyShaderModule = destroy_shader;
    context.vk.vkCreateGraphicsPipelines = create_pipeline;
    context.vk.vkDestroyPipeline = destroy_pipeline;
    /* Header-only words test native packet/ownership, not shader validation/execution. */
    const uint32_t words[5] = {0x07230203, 0x00010300, 0, 1, 0};
    const doe_vk_shader shader = {words, 5, "main"};
    doe_vk_render_pipeline_desc desc = {.vertex = shader, .topology = DOE_VK_TRIANGLES,
        .depth_stencil_format = DOE_VK_FORMAT_D32_FLOAT, .depth_test = true,
        .depth_write = true, .depth_compare = 1};
    desc.fragment.word_count = 1;
    rejected(&context, &desc);
    desc.fragment.word_count = 0;
    desc.color_format = DOE_VK_FORMAT_RGBA8_UNORM;
    rejected(&context, &desc);
    desc.color_format = 0;
    desc.color_write_mask = 15;
    rejected(&context, &desc);
    desc.color_write_mask = 0;
    desc.blend_enabled = true;
    rejected(&context, &desc);
    desc.blend_enabled = false;
    desc.alpha_to_coverage = true;
    rejected(&context, &desc);
    desc.alpha_to_coverage = false;
    desc.depth_stencil_format = 0;
    rejected(&context, &desc);
    desc.depth_stencil_format = DOE_VK_FORMAT_D32_FLOAT;
    for (expected_stages = 1; expected_stages <= 2; ++expected_stages) {
        desc.fragment = expected_stages == 2 ? shader : (doe_vk_shader){0};
        doe_vk_pipeline *pipeline = NULL;
        assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_OK);
        assert(pipeline->color_count == 0 && pipeline->depth_stencil_format == DOE_VK_FORMAT_D32_FLOAT);
        assert(context.references == 2 && modules == destroyed_modules);
        doe_vk_pipeline_release(pipeline);
        assert(context.references == 1 && live_pipelines == 0 && live_passes == 0 && live_layouts == 0);
    }
    expected_stages = 1;
    desc.fragment = (doe_vk_shader){0};
    const doe_vk_topology topologies[] = {DOE_VK_LINES, DOE_VK_LINE_STRIP, DOE_VK_TRIANGLE_STRIP};
    for (size_t i = 0; i < sizeof(topologies) / sizeof(topologies[0]); ++i) {
        desc.topology = topologies[i];
        expected_restart = topologies[i] != DOE_VK_LINES;
        doe_vk_pipeline *pipeline = NULL;
        assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_OK);
        doe_vk_pipeline_release(pipeline);
    }
    desc.topology = DOE_VK_TRIANGLES;
    expected_restart = 0;
    pipeline_result = -1;
    doe_vk_pipeline *sentinel = (doe_vk_pipeline *)&context, *out = sentinel;
    assert(doe_vk_render_pipeline_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && modules == destroyed_modules && context.references == 1);
    assert(live_pipelines == 0 && live_passes == 0 && live_layouts == 0);
    puts("PASS genuine depth-only native pipeline packets, optional fragment stage, strip-only primitive restart, no color state/attachment, malformed or contradictory admission, output preservation and native failure cleanup");
    return 0;
}
