/* SPDX-License-Identifier: MIT
 * Host native-packet contract for indexed color slots: sparse VK_ATTACHMENT_UNUSED
 * references, per-slot resolve references and attachment order, per-slot blend
 * state, the independentBlend rule, legacy/slot mixing and slot limits. Packets
 * only; real execution is the public multiple-attachment client. */
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>

/* Copied out of the callee's frame: its pointers do not outlive the call. */
static VkRenderPassCreateInfo pass_info;
static uint32_t pass_color_count;
static VkAttachmentDescription pass_attachments[17];
static VkAttachmentReference color_refs[8], resolve_refs[8];
static bool resolve_present;
static VkPipelineColorBlendAttachmentState blend_states[8];
static uint32_t blend_count;
static unsigned passes, pipelines;

static void format_properties(VkPhysicalDevice physical, uint32_t format, VkFormatProperties *out) {
    (void)physical;
    (void)format;
    *out = (VkFormatProperties){.optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT};
}
static VkResult image_properties(VkPhysicalDevice physical, uint32_t format, uint32_t type,
    uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *out) {
    (void)physical; (void)format; (void)type; (void)tiling; (void)usage; (void)flags;
    *out = (VkImageFormatProperties){.sampleCounts = 1 | 4};
    return 0;
}
static VkResult create_layout(VkDevice device, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *out) {
    (void)device; (void)info; (void)allocator;
    *out = 11;
    return 0;
}
static void destroy_layout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device; (void)layout; (void)allocator;
}
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device; (void)allocator;
    pass_info = *info;
    pass_color_count = info->pSubpasses[0].colorAttachmentCount;
    memcpy(pass_attachments, info->pAttachments, info->attachmentCount * sizeof(*pass_attachments));
    const VkSubpassDescription *subpass = info->pSubpasses;
    memcpy(color_refs, subpass->pColorAttachments, subpass->colorAttachmentCount * sizeof(*color_refs));
    resolve_present = subpass->pResolveAttachments != NULL;
    if (resolve_present)
        memcpy(resolve_refs, subpass->pResolveAttachments, subpass->colorAttachmentCount * sizeof(*resolve_refs));
    ++passes;
    *out = 12;
    return 0;
}
static void destroy_pass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks *allocator) {
    (void)device; (void)pass; (void)allocator;
}
static VkResult create_shader(VkDevice device, const VkShaderModuleCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkShaderModule *out) {
    (void)device; (void)info; (void)allocator;
    *out = 21;
    return 0;
}
static void destroy_shader(VkDevice device, VkShaderModule shader, const VkAllocationCallbacks *allocator) {
    (void)device; (void)shader; (void)allocator;
}
static VkResult create_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *out) {
    (void)device; (void)cache; (void)count; (void)allocator;
    blend_count = info->pColorBlendState->attachmentCount;
    memcpy(blend_states, info->pColorBlendState->pAttachments, blend_count * sizeof(*blend_states));
    ++pipelines;
    *out = 13;
    return 0;
}
static void destroy_pipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)device; (void)pipeline; (void)allocator;
}

static doe_vk_context fake_context(void) {
    doe_vk_context context = {.references = 1};
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.caps.max_color_attachments = 8;
    context.caps.independent_blend = true;
    context.properties.limits.framebufferColorSampleCounts = 1 | 4;
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
    return context;
}

static doe_vk_render_pipeline_desc sparse_desc(void) {
    static const uint32_t words[5] = {0x07230203, 0x00010300, 0, 1, 0};
    const doe_vk_shader shader = {words, 5, "main"};
    doe_vk_render_pipeline_desc desc = {.vertex = shader, .fragment = shader, .topology = DOE_VK_TRIANGLES,
        .color_target_count = 3};
    desc.color_targets[0] = (doe_vk_color_target){.format = DOE_VK_FORMAT_RGBA8_UNORM, .write_mask = 15,
        .blend_enabled = true, .color_source_factor = 1, .color_destination_factor = 1, .alpha_destination_factor = 1};
    desc.color_targets[2] = (doe_vk_color_target){.format = DOE_VK_FORMAT_R32_UINT, .write_mask = 1};
    return desc;
}

static void pipeline_packets(void) {
    doe_vk_context context = fake_context();
    doe_vk_render_pipeline_desc desc = sparse_desc();
    doe_vk_pipeline *pipeline = NULL;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_OK);
    assert(pipeline->color_count == 3 && pipeline->color_formats[1] == 0 &&
        pipeline->color_formats[2] == DOE_VK_FORMAT_R32_UINT);
    /* Pipeline-compatible pass: two used attachments, slot 1 unused, no resolve. */
    assert(pass_info.attachmentCount == 2 && pass_color_count == 3 && !resolve_present);
    assert(color_refs[0].attachment == 0 && color_refs[1].attachment == VK_ATTACHMENT_UNUSED &&
        color_refs[2].attachment == 1 && pass_attachments[1].format == DOE_VK_FORMAT_R32_UINT);
    assert(blend_count == 3 && blend_states[0].blendEnable && blend_states[0].colorWriteMask == 15 &&
        !blend_states[2].blendEnable && blend_states[2].colorWriteMask == 1 && blend_states[1].colorWriteMask == 0);
    doe_vk_pipeline_release(pipeline);
    /* Without independentBlend, differing used slots are unsupported... */
    context.caps.independent_blend = false;
    unsigned before = pipelines;
    pipeline = NULL;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_UNSUPPORTED && !pipeline);
    /* ...and identical used slots give unused slots that same state. */
    desc.color_targets[2] = desc.color_targets[0];
    desc.color_targets[2].format = DOE_VK_FORMAT_R32_FLOAT;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_OK && pipelines == before + 1);
    assert(memcmp(&blend_states[0], &blend_states[1], sizeof(blend_states[0])) == 0 &&
        memcmp(&blend_states[0], &blend_states[2], sizeof(blend_states[0])) == 0);
    doe_vk_pipeline_release(pipeline);
    context.caps.independent_blend = true;
    /* Unused slots carry nothing; legacy fields never mix with slots; limits hold. */
    desc = sparse_desc();
    desc.color_targets[1].write_mask = 1;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_INVALID_ARGUMENT);
    desc = sparse_desc();
    desc.color_format = DOE_VK_FORMAT_RGBA8_UNORM;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_INVALID_ARGUMENT);
    desc = sparse_desc();
    desc.color_target_count = 9;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_INVALID_ARGUMENT);
    desc = sparse_desc();
    context.caps.max_color_attachments = 2;
    assert(doe_vk_render_pipeline_create(&context, &desc, &pipeline, NULL) == DOE_VK_UNSUPPORTED);
    assert(pipelines == before + 1 && context.references == 1);
}

/* Resolve attachments follow colors and depth, referenced from their own slots. */
static void resolved_pass_packets(void) {
    doe_vk_context context = fake_context();
    doe_vk_render_colors colors = {.count = 3};
    doe_vk_texture_view views[3] = {{0}};
    colors.slots[0] = (doe_vk_color_attachment){.view = &views[0], .clear = true, .discard = true};
    colors.slots[2] = (doe_vk_color_attachment){.view = &views[1], .resolve_target = &views[2]};
    const doe_vk_render_layout layout = {.color_count = 3,
        .colors = {DOE_VK_FORMAT_RGBA8_UNORM, 0, DOE_VK_FORMAT_BGRA8_UNORM}, .samples = 4};
    VkRenderPass pass = 0;
    assert(doe_vk_render_pass_create(&context, &layout, &colors, NULL, &pass, NULL) == DOE_VK_OK && pass == 12);
    assert(pass_info.attachmentCount == 3 && resolve_present);
    assert(color_refs[0].attachment == 0 && color_refs[1].attachment == VK_ATTACHMENT_UNUSED && color_refs[2].attachment == 1);
    assert(resolve_refs[0].attachment == VK_ATTACHMENT_UNUSED && resolve_refs[1].attachment == VK_ATTACHMENT_UNUSED &&
        resolve_refs[2].attachment == 2);
    assert(pass_attachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
        pass_attachments[0].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE && pass_attachments[0].samples == 4);
    assert(pass_attachments[1].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && pass_attachments[1].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    assert(pass_attachments[2].format == DOE_VK_FORMAT_BGRA8_UNORM && pass_attachments[2].samples == 1 &&
        pass_attachments[2].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    /* Descriptor normalization: legacy slot 0 never mixes with slots; unused
     * slots carry no operations. */
    doe_vk_render_pass_desc desc = {.color = &views[0], .color_attachment_count = 1};
    doe_vk_render_colors normalized;
    assert(doe_vk_render_colors_normalize(&desc, &normalized) == DOE_VK_INVALID_ARGUMENT);
    desc = (doe_vk_render_pass_desc){.color_attachment_count = 2};
    desc.color_attachments[1].clear = true;
    assert(doe_vk_render_colors_normalize(&desc, &normalized) == DOE_VK_INVALID_ARGUMENT);
    desc = (doe_vk_render_pass_desc){.color = &views[0], .discard_color = true};
    assert(doe_vk_render_colors_normalize(&desc, &normalized) == DOE_VK_OK && normalized.count == 1 &&
        normalized.slots[0].view == &views[0] && normalized.slots[0].discard && !normalized.slots[0].clear);
}

int main(void) {
    pipeline_packets();
    resolved_pass_packets();
    puts("PASS indexed color-slot native packets: sparse unused references, per-slot blend/write masks, independentBlend rule and unused-slot state, per-slot resolve order and load/store, legacy/slot mixing and slot-limit rejection; host only");
    return 0;
}
