/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>

/* Native procedure boundary only: counts compute pipeline creation and
 * destruction so sharing, bounded retention and teardown are exact. */
static struct {
    unsigned pipelines_created, pipelines_destroyed;
    unsigned layouts_created, layouts_destroyed;
    unsigned sets_created, sets_destroyed;
    unsigned modules_created, modules_destroyed;
    uint64_t next_handle;
} mock;

static VkResult create_set_layout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorSetLayout *out) {
    (void)device; (void)info; (void)allocator;
    ++mock.sets_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_set_layout(VkDevice device, VkDescriptorSetLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(layout != 0);
    ++mock.sets_destroyed;
}
static VkResult create_layout(VkDevice device, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *out) {
    (void)device; (void)info; (void)allocator;
    ++mock.layouts_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_layout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(layout != 0);
    ++mock.layouts_destroyed;
}
static VkResult create_module(VkDevice device, const VkShaderModuleCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkShaderModule *out) {
    (void)device; (void)info; (void)allocator;
    ++mock.modules_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_module(VkDevice device, VkShaderModule module, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(module != 0);
    ++mock.modules_destroyed;
}
static VkResult create_pipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *out) {
    (void)device; (void)cache; (void)info; (void)allocator;
    assert(count == 1);
    ++mock.pipelines_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_pipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pipeline != 0);
    ++mock.pipelines_destroyed;
}

static doe_vk_context *context(void) {
    doe_vk_context *value = calloc(1, sizeof(*value));
    assert(value);
    value->references = 1;
    VkPhysicalDeviceLimits *limits = &value->properties.limits;
    limits->maxBoundDescriptorSets = 4;
    limits->maxPerStageResources = 16;
    limits->maxPerStageDescriptorUniformBuffers = limits->maxDescriptorSetUniformBuffers = 8;
    limits->maxPerStageDescriptorStorageBuffers = limits->maxDescriptorSetStorageBuffers = 8;
    value->vk = (doe_vk_functions){
        .vkCreateDescriptorSetLayout = create_set_layout,
        .vkDestroyDescriptorSetLayout = destroy_set_layout,
        .vkCreatePipelineLayout = create_layout,
        .vkDestroyPipelineLayout = destroy_layout,
        .vkCreateShaderModule = create_module,
        .vkDestroyShaderModule = destroy_module,
        .vkCreateComputePipelines = create_pipelines,
        .vkDestroyPipeline = destroy_pipeline
    };
    return value;
}

/* Minimal valid SPIR-V header words; word 5 distinguishes shaders. */
static void shader(uint32_t words[6], uint32_t variant) {
    const uint32_t header[6] = {0x07230203u, 0x00010300u, 0, 8, 0, variant};
    memcpy(words, header, sizeof(header));
}

static doe_vk_pipeline *create(doe_vk_context *ctx, uint32_t variant, const char *entry,
    const doe_vk_binding_layout *layout, size_t count) {
    uint32_t words[6];
    shader(words, variant);
    doe_vk_pipeline *pipeline = NULL;
    assert(doe_vk_pipeline_create(ctx, words, 6, entry, layout, count, &pipeline, NULL) == DOE_VK_OK && pipeline);
    return pipeline;
}

int main(void) {
    const doe_vk_binding_layout storage[2] = {{0, 0, DOE_VK_BINDING_STORAGE}, {0, 1, DOE_VK_BINDING_STORAGE}};
    const doe_vk_binding_layout uniform[2] = {{0, 0, DOE_VK_BINDING_UNIFORM}, {0, 1, DOE_VK_BINDING_STORAGE}};
    doe_vk_context *ctx = context();

    /* Identical creation input shares one native pipeline; any difference does not. */
    doe_vk_pipeline *first = create(ctx, 1, "main", storage, 2);
    doe_vk_pipeline *same = create(ctx, 1, "main", storage, 2);
    assert(same == first && first->references == 2 && mock.pipelines_created == 1);
    assert(ctx->references == 2); /* One context reference per native pipeline. */
    doe_vk_pipeline *words = create(ctx, 2, "main", storage, 2);
    doe_vk_pipeline *entry = create(ctx, 1, "other", storage, 2);
    doe_vk_pipeline *layout = create(ctx, 1, "main", uniform, 2);
    doe_vk_pipeline *fewer = create(ctx, 1, "main", storage, 1);
    assert(words != first && entry != first && layout != first && fewer != first);
    assert(mock.pipelines_created == 5);

    /* A fully released pipeline is retained without its context reference and
     * returns, with that reference, for the same input. */
    doe_vk_pipeline_release(same);
    doe_vk_pipeline_release(first);
    assert(mock.pipelines_destroyed == 0 && ctx->retained_pipelines == 1 && ctx->references == 5);
    doe_vk_pipeline *again = create(ctx, 1, "main", storage, 2);
    assert(again == first && again->references == 1 && mock.pipelines_created == 5);
    assert(ctx->retained_pipelines == 0 && ctx->references == 6);
    doe_vk_pipeline_release(again);
    doe_vk_pipeline_release(words);
    doe_vk_pipeline_release(entry);
    doe_vk_pipeline_release(layout);
    doe_vk_pipeline_release(fewer);
    assert(ctx->retained_pipelines == 5 && ctx->references == 1 && mock.pipelines_destroyed == 0);

    /* Retention is bounded: the oldest released pipeline is destroyed first. */
    for (uint32_t variant = 100; variant < 100 + DOE_VK_RETAINED_PIPELINES; ++variant)
        doe_vk_pipeline_release(create(ctx, variant, "main", storage, 2));
    assert(ctx->retained_pipelines == DOE_VK_RETAINED_PIPELINES);
    assert(mock.pipelines_destroyed == 5 && mock.pipelines_created == 5 + DOE_VK_RETAINED_PIPELINES);
    const unsigned created = mock.pipelines_created;
    doe_vk_pipeline_release(create(ctx, 100, "main", storage, 2)); /* Still retained. */
    assert(mock.pipelines_created == created);
    doe_vk_pipeline_release(create(ctx, 1, "main", storage, 2)); /* Evicted earlier. */
    assert(mock.pipelines_created == created + 1 && mock.pipelines_destroyed == 6);

    /* A live pipeline keeps the context; the final context release destroys
     * every retained pipeline with it, so nothing native leaks. */
    doe_vk_pipeline *live = create(ctx, 7, "main", storage, 2);
    doe_vk_context_release(ctx);
    assert(live->context == ctx && live->references == 1 && ctx->references == 1);
    ctx->lost = true; /* A lost context destroys at release instead of retaining. */
    doe_vk_pipeline_release(live); /* Last reference: destroys it and then the context. */
    assert(mock.pipelines_destroyed == mock.pipelines_created);
    assert(mock.layouts_destroyed == mock.layouts_created && mock.sets_destroyed == mock.sets_created);
    assert(mock.modules_destroyed == mock.modules_created);
    puts("pipeline sharing: identical input shares, bounded retention, teardown destroys all");
    return 0;
}
