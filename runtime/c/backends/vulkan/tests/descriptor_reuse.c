/* SPDX-License-Identifier: MIT */
#include "../src/texture_internal.h"
#include "../include/doe_vk_bindings.h"
#include <assert.h>
#include <stdio.h>

/* Built three ways: the DEFAULT resource profile (exact per-use pool),
 * DOE_VK_TEST_PROFILE_AMD (DESCRIPTOR_BLOCKS), and DOE_VK_TEST_PROFILE_CANDIDATE
 * (DESCRIPTOR_BLOCKS | DESCRIPTOR_REUSE). */
#if defined(DOE_VK_TEST_PROFILE_CANDIDATE)
#define TEST_PROFILE (DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS | DOE_VK_RESOURCE_DESCRIPTOR_REUSE)
#define TEST_REUSE 1
#elif defined(DOE_VK_TEST_PROFILE_AMD)
#define TEST_PROFILE DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS
#define TEST_REUSE 0
#else
#define TEST_PROFILE 0u
#define TEST_REUSE 0
#endif

/* Native procedure boundary only: counts descriptor work per recorded dispatch. */
static struct {
    unsigned pools_created, pools_destroyed, sets_allocated, sets_freed, updates;
    unsigned barriers, dispatches, pipeline_binds, set_binds;
    uint32_t last_max_sets, last_per_type;
    uint64_t next_handle;
} mock;

static VkResult create_pool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorPool *out) {
    (void)device; (void)allocator;
    ++mock.pools_created;
    mock.last_max_sets = info->maxSets;
    mock.last_per_type = info->poolSizeCount ? info->pPoolSizes[0].descriptorCount : 0;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_pool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pool != 0);
    ++mock.pools_destroyed;
}
static VkResult free_sets(VkDevice device, VkDescriptorPool pool, uint32_t count, const VkDescriptorSet *sets) {
    (void)device; (void)sets;
    assert(pool != 0 && count == 1);
    ++mock.sets_freed;
    return 0;
}
static VkResult allocate_sets(VkDevice device, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets) {
    (void)device;
    for (uint32_t i = 0; i < info->descriptorSetCount; ++i) sets[i] = ++mock.next_handle;
    ++mock.sets_allocated;
    return 0;
}
static void update_sets(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
    uint32_t copies, const void *copy) {
    (void)device; (void)count; (void)writes; (void)copies; (void)copy;
    ++mock.updates;
}
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination, VkFlags flags,
    uint32_t memory, const void *memory_barriers, uint32_t buffers, const void *buffer_barriers,
    uint32_t images, const void *image_barriers) {
    (void)command; (void)source; (void)destination; (void)flags; (void)memory; (void)memory_barriers;
    (void)buffers; (void)buffer_barriers; (void)images; (void)image_barriers;
    ++mock.barriers;
}
static void bind_pipeline(VkCommandBuffer command, int32_t point, VkPipeline pipeline) {
    (void)command; (void)point; (void)pipeline;
    ++mock.pipeline_binds;
}
static void bind_sets(VkCommandBuffer command, int32_t point, VkPipelineLayout layout, uint32_t first,
    uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic, const uint32_t *offsets) {
    (void)command; (void)point; (void)layout; (void)first; (void)count; (void)sets; (void)dynamic; (void)offsets;
    ++mock.set_binds;
}
static void dispatch(VkCommandBuffer command, uint32_t x, uint32_t y, uint32_t z) {
    (void)command; (void)x; (void)y; (void)z;
    ++mock.dispatches;
}

static void release_uses(doe_vk_commands *commands) {
    doe_vk_textures_release(commands);
    while (commands->dispatches != NULL) {
        doe_vk_dispatch_use *use = commands->dispatches;
        commands->dispatches = use->next;
        doe_vk_dispatch_release(commands->context, use);
    }
    while (commands->buffers != NULL) {
        doe_vk_buffer_use *use = commands->buffers;
        commands->buffers = use->next;
        --use->buffer->recorded_uses;
        --use->buffer->references;
        free(use);
    }
}

static void noop_destroy_u64(VkDevice device, uint64_t handle, const VkAllocationCallbacks *allocator) {
    (void)device; (void)handle; (void)allocator;
}

static void cross_recording_reuse_and_eviction(void) {
    doe_vk_context context = { .references = 1 };
    context.caps.resource_profile = DOE_VK_RESOURCE_DESCRIPTOR_BLOCKS | DOE_VK_RESOURCE_DESCRIPTOR_REUSE;
    context.caps.max_workgroup_count[0] = context.caps.max_workgroup_count[1] = context.caps.max_workgroup_count[2] = 64;
    context.caps.storage_offset_alignment = 16;
    context.caps.max_storage_buffer_range = 1u << 20;
    context.vk = (doe_vk_functions){
        .vkCreateDescriptorPool = create_pool, .vkDestroyDescriptorPool = destroy_pool,
        .vkAllocateDescriptorSets = allocate_sets, .vkFreeDescriptorSets = free_sets,
        .vkUpdateDescriptorSets = update_sets, .vkCmdPipelineBarrier = barrier,
        .vkCmdBindPipeline = bind_pipeline, .vkCmdBindDescriptorSets = bind_sets,
        .vkCmdDispatch = dispatch, .vkDestroyBuffer = noop_destroy_u64,
        .vkDestroyImage = noop_destroy_u64, .vkDestroyImageView = noop_destroy_u64,
        .vkDestroySampler = noop_destroy_u64, .vkDestroyPipeline = noop_destroy_u64
    };
    const unsigned base_pools = mock.pools_created, base_alloc = mock.sets_allocated;
    const unsigned base_freed = mock.sets_freed, base_updates = mock.updates;

    doe_vk_pipeline pipeline = { .context = &context, .references = 1, .binding_count = 1, .group_count = 1 };
    pipeline.bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_STORAGE};
    doe_vk_buffer *heap_buf = calloc(1, sizeof(*heap_buf));
    assert(heap_buf != NULL && doe_vk_context_retain(&context) == DOE_VK_OK);
    *heap_buf = (doe_vk_buffer){ .context = &context, .references = 1,
        .usage = DOE_VK_BUFFER_STORAGE, .size = 1024, .buffer = 91 };
    doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = {heap_buf, 0, 64} };

    /* 33 one-shot recordings of the same dispatch allocate and write once. */
    for (unsigned step = 0; step < 33; ++step) {
        doe_vk_commands commands = { .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
        assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
        release_uses(&commands);
    }
    assert(mock.pools_created == base_pools + 1 && mock.last_max_sets == 32 && mock.last_per_type == 128);
    assert(mock.sets_allocated == base_alloc + 1 && mock.updates == base_updates + 1);
    assert(mock.sets_freed == base_freed && context.retained_dispatch_count == 1);

    /* Releasing the buffer evicts the cached descriptor set synchronously. */
    doe_vk_buffer_release(heap_buf);
    assert(mock.sets_freed == base_freed + 1 && context.retained_dispatch_count == 0);

    /* Texture view release, texture eviction, sampler release and pipeline destroy each evict. */
    uint8_t initialized = VK_IMAGE_ASPECT_COLOR_BIT;
    doe_vk_texture *tex = calloc(1, sizeof(*tex));
    assert(tex != NULL && doe_vk_context_retain(&context) == DOE_VK_OK);
    *tex = (doe_vk_texture){ .context = &context, .references = 1, .image = 92, .subresource_count = 1,
        .initialized = calloc(1, 1), .layout = VK_IMAGE_LAYOUT_GENERAL,
        .desc = { .format = DOE_VK_FORMAT_RGBA8_UNORM, .dimension = DOE_VK_TEXTURE_2D,
            .width = 1, .height = 1, .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_SAMPLED } };
    tex->initialized[0] = initialized;
    doe_vk_texture_view *view = calloc(1, sizeof(*view));
    assert(view != NULL && doe_vk_texture_retain(tex) == DOE_VK_OK);
    *view = (doe_vk_texture_view){ .texture = tex, .references = 1, .view = 93,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT, .desc = { .mip_count = 1, .layer_count = 1 } };
    doe_vk_sampler *sampler = calloc(1, sizeof(*sampler));
    assert(sampler != NULL && doe_vk_context_retain(&context) == DOE_VK_OK);
    *sampler = (doe_vk_sampler){ .context = &context, .references = 1, .sampler = 94 };

    doe_vk_pipeline *gfx = calloc(1, sizeof(*gfx));
    assert(gfx != NULL && doe_vk_context_retain(&context) == DOE_VK_OK);
    *gfx = (doe_vk_pipeline){ .context = &context, .references = 1, .graphics = true,
        .pipeline = 95, .binding_count = 2, .group_count = 1 };
    gfx->bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_SAMPLED_TEXTURE};
    gfx->bindings[1] = (doe_vk_binding_layout){0, 1, DOE_VK_BINDING_SAMPLER};
    doe_vk_binding_resource gfx_bindings[2] = {
        { .kind = DOE_VK_BINDING_SAMPLED_TEXTURE, .value.view = view },
        { .kind = DOE_VK_BINDING_SAMPLER, .value.sampler = sampler }
    };
    doe_vk_dispatch_use *prepared = NULL;
    doe_vk_commands commands = { .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
    assert(doe_vk_prepare_graphics_resources(&commands, gfx, gfx_bindings, 2, &prepared) == DOE_VK_OK);
    release_uses(&commands);
    assert(context.retained_dispatch_count == 1 && mock.sets_freed == base_freed + 1);

    /* Evicting the underlying texture evicts the cached view descriptor set. */
    assert(doe_vk_texture_evict(tex) == DOE_VK_OK);
    assert(context.retained_dispatch_count == 0 && mock.sets_freed == base_freed + 2);
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(tex);

    /* Sampler and pipeline destruction evict cached entries, including in-flight drop. */
    doe_vk_pipeline sampler_pipe = { .context = &context, .references = 1, .binding_count = 1, .group_count = 1 };
    sampler_pipe.bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_SAMPLER};
    doe_vk_binding_resource sbind = { .kind = DOE_VK_BINDING_SAMPLER, .value.sampler = sampler };
    commands = (doe_vk_commands){ .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
    assert(doe_vk_commands_dispatch_resources(&commands, &sampler_pipe, &sbind, 1, 1, 1, 1) == DOE_VK_OK);
    /* Caller drops its sampler reference while commands still holds one. */
    doe_vk_sampler_release(sampler);
    assert(sampler->references == 1 && context.retained_dispatch_count == 0);
    release_uses(&commands);
    /* Releasing commands drops the final sampler reference and immediately evicts. */
    assert(context.retained_dispatch_count == 0 && mock.sets_freed == base_freed + 3);

    doe_vk_buffer stack_buf = { .context = &context, .references = 1,
        .usage = DOE_VK_BUFFER_STORAGE, .size = 4096, .buffer = 96 };
    gfx->binding_count = 1;
    gfx->bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_STORAGE};
    doe_vk_binding_resource gbuf = { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = {&stack_buf, 0, 64} };
    commands = (doe_vk_commands){ .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
    assert(doe_vk_prepare_graphics_resources(&commands, gfx, &gbuf, 1, &prepared) == DOE_VK_OK);
    release_uses(&commands);
    assert(context.retained_dispatch_count == 1);
    doe_vk_pipeline_release(gfx);
    assert(context.retained_dispatch_count == 0 && mock.sets_freed == base_freed + 4);

    /* LRU bound: 17 distinct bindings keep the newest 16 and free the oldest 1. */
    const unsigned lru_freed = mock.sets_freed;
    for (unsigned slot = 0; slot < 17; ++slot) {
        doe_vk_binding_resource r = { .kind = DOE_VK_BINDING_STORAGE,
            .value.buffer = {&stack_buf, (uint64_t)slot * 16u, 64} };
        commands = (doe_vk_commands){ .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
        assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &r, 1, 1, 1, 1) == DOE_VK_OK);
        release_uses(&commands);
    }
    assert(context.retained_dispatch_count == DOE_VK_RETAINED_DISPATCHES && mock.sets_freed == lru_freed + 1);
    const unsigned after_lru_alloc = mock.sets_allocated;
    doe_vk_binding_resource warm_slot = { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = {&stack_buf, 16u, 64} };
    commands = (doe_vk_commands){ .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &warm_slot, 1, 1, 1, 1) == DOE_VK_OK);
    release_uses(&commands);
    assert(mock.sets_allocated == after_lru_alloc && mock.sets_freed == lru_freed + 1);

    /* Pool teardown clears retained dispatches without per-set vkFreeDescriptorSets. */
    const unsigned clear_freed = mock.sets_freed, clear_destroyed = mock.pools_destroyed;
    doe_vk_descriptor_blocks_clear(&context);
    assert(mock.sets_freed == clear_freed && mock.pools_destroyed == clear_destroyed + 1);
    assert(context.retained_dispatch_count == 0 && context.references == 1);
}

static void graphics_reuse(doe_vk_context *context) {
    unsigned allocated = mock.sets_allocated, updates = mock.updates;
    unsigned barriers = mock.barriers;
    doe_vk_pipeline pipeline = { .context = context, .references = 1,
        .graphics = true, .binding_count = 1, .group_count = 1 };
    pipeline.bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_STORAGE};
    doe_vk_pipeline other = pipeline;
    doe_vk_buffer buffer = { .context = context, .references = 1,
        .usage = DOE_VK_BUFFER_STORAGE, .size = 256, .buffer = 7 };
    doe_vk_commands commands = { .context = context, .state = DOE_VK_RECORDING,
        .uses_active = true };
    doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_STORAGE,
        .value.buffer = {&buffer, 0, 64} };
    doe_vk_dispatch_use *first = NULL, *next = NULL;
    assert(doe_vk_prepare_resources(&commands, &pipeline, &binding, 1, &first) == DOE_VK_OK);
    assert(doe_vk_prepare_resources(&commands, &pipeline, &binding, 1, &next) == DOE_VK_OK);
    assert(next == first && mock.sets_allocated == allocated + 1 && mock.updates == updates + 1);
    assert(mock.barriers == barriers + 2 && buffer.recorded_uses == 1);
    binding.value.buffer.offset = 64;
    assert(doe_vk_prepare_resources(&commands, &pipeline, &binding, 1, &next) == DOE_VK_OK);
    assert(next != first);
    binding.value.buffer.size = 128;
    assert(doe_vk_prepare_resources(&commands, &pipeline, &binding, 1, &next) == DOE_VK_OK);
    assert(doe_vk_prepare_resources(&commands, &other, &binding, 1, &next) == DOE_VK_OK);
    assert(mock.sets_allocated == allocated + 4 && mock.updates == updates + 4);
    buffer.content_status = DOE_VK_OUT_OF_MEMORY;
    assert(doe_vk_prepare_resources(&commands, &other, &binding, 1, &next) == DOE_VK_OUT_OF_MEMORY);
    assert(commands.state == DOE_VK_POISONED && mock.sets_allocated == allocated + 4);
    buffer.content_status = DOE_VK_OK;
    release_uses(&commands);
    assert(buffer.references == 1 && buffer.recorded_uses == 0);
    assert(pipeline.references == 1 && other.references == 1);

    /* Sampler identity is reusable independently of per-use synchronization. */
    doe_vk_sampler sampler = { .context = context, .references = 1, .sampler = 11 };
    pipeline.bindings[0].kind = DOE_VK_BINDING_SAMPLER;
    binding.kind = DOE_VK_BINDING_SAMPLER;
    binding.value.sampler = &sampler;
    commands.state = DOE_VK_RECORDING;
    assert(doe_vk_prepare_resources(&commands, &pipeline, &binding, 1, &first) == DOE_VK_OK);
    assert(doe_vk_prepare_resources(&commands, &pipeline, &binding, 1, &next) == DOE_VK_OK);
    assert(next == first && mock.sets_allocated == allocated + 5);
    release_uses(&commands);
    assert(sampler.references == 1 && pipeline.references == 1);
    if (TEST_PROFILE != 0u) doe_vk_descriptor_blocks_clear(context);
    assert(mock.pools_destroyed == mock.pools_created);
}

static void mixed_reuse(doe_vk_context *context) {
    const unsigned allocated = mock.sets_allocated, barriers = mock.barriers;
    doe_vk_pipeline pipeline = {.context=context,.references=1,.graphics=true,.binding_count=3,.group_count=1};
    pipeline.bindings[0]=(doe_vk_binding_layout){0,0,DOE_VK_BINDING_STORAGE};
    pipeline.bindings[1]=(doe_vk_binding_layout){0,1,DOE_VK_BINDING_SAMPLED_TEXTURE};
    pipeline.bindings[2]=(doe_vk_binding_layout){0,2,DOE_VK_BINDING_SAMPLER};
    doe_vk_buffer buffer={.context=context,.references=1,.usage=DOE_VK_BUFFER_STORAGE,.size=256,.buffer=41};
    uint8_t initialized=VK_IMAGE_ASPECT_COLOR_BIT;
    doe_vk_texture texture={.context=context,.references=1,.subresource_count=1,.initialized=&initialized,
        .layout=VK_IMAGE_LAYOUT_GENERAL,.desc={.format=DOE_VK_FORMAT_RGBA8_UNORM,.dimension=DOE_VK_TEXTURE_2D,
            .width=1,.height=1,.depth_or_layers=1,.mip_levels=1,.usage=DOE_VK_TEXTURE_SAMPLED}};
    doe_vk_texture_view view={.texture=&texture,.references=1,.view=42,.aspects=VK_IMAGE_ASPECT_COLOR_BIT,
        .desc={.mip_count=1,.layer_count=1}};
    doe_vk_texture_view other_view=view; other_view.view=43;
    doe_vk_sampler sampler={.context=context,.references=1,.sampler=44}, other_sampler=sampler;
    other_sampler.sampler=45;
    doe_vk_binding_resource bindings[3]={{.kind=DOE_VK_BINDING_STORAGE,.value.buffer={&buffer,0,64}},
        {.kind=DOE_VK_BINDING_SAMPLED_TEXTURE,.value.view=&view},{.kind=DOE_VK_BINDING_SAMPLER,.value.sampler=&sampler}};
    doe_vk_commands commands={.context=context,.state=DOE_VK_RECORDING,.uses_active=true};
    doe_vk_dispatch_use *first=NULL,*next=NULL;
    assert(doe_vk_prepare_graphics_resources(&commands,&pipeline,bindings,3,&first)==DOE_VK_OK);
    assert(doe_vk_prepare_graphics_resources(&commands,&pipeline,bindings,3,&next)==DOE_VK_OK);
    assert(first==next && mock.sets_allocated==allocated+1 && mock.barriers==barriers);
    assert(view.references==2 && sampler.references==2 && texture.command_uses==1);
    /* A layout change does not rewrite immutable descriptors, but must still
     * transition at the next preparation, before the render pass begins. */
    commands.textures->layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    assert(doe_vk_prepare_graphics_resources(&commands,&pipeline,bindings,3,&next)==DOE_VK_OK);
    assert(next==first && commands.textures->layout==VK_IMAGE_LAYOUT_GENERAL && mock.barriers==barriers+1);
    bindings[1].value.view=&other_view;
    assert(doe_vk_prepare_graphics_resources(&commands,&pipeline,bindings,3,&next)==DOE_VK_OK && next!=first);
    bindings[2].value.sampler=&other_sampler;
    assert(doe_vk_prepare_graphics_resources(&commands,&pipeline,bindings,3,&next)==DOE_VK_OK);
    assert(mock.sets_allocated==allocated+3);
    commands.textures->initialized[0]=0;
    assert(doe_vk_prepare_graphics_resources(&commands,&pipeline,bindings,3,&next)==DOE_VK_INVALID_STATE);
    assert(commands.state==DOE_VK_POISONED && mock.sets_allocated==allocated+3);
    release_uses(&commands);
    assert(texture.references==1 && texture.command_uses==0 && texture.recording_owner==NULL);
    assert(view.references==1 && other_view.references==1 && sampler.references==1 && other_sampler.references==1);
    if (TEST_PROFILE) doe_vk_descriptor_blocks_clear(context);
}

int main(void) {
    doe_vk_context context = { .references = 1 };
    context.caps.resource_profile = TEST_PROFILE;
    context.caps.max_workgroup_count[0] = context.caps.max_workgroup_count[1] = context.caps.max_workgroup_count[2] = 64;
    context.caps.storage_offset_alignment = 16;
    context.caps.max_storage_buffer_range = 1u << 20;
    context.vk = (doe_vk_functions){
        .vkCreateDescriptorPool = create_pool, .vkDestroyDescriptorPool = destroy_pool,
        .vkAllocateDescriptorSets = allocate_sets, .vkFreeDescriptorSets = free_sets,
        .vkUpdateDescriptorSets = update_sets,
        .vkCmdPipelineBarrier = barrier, .vkCmdBindPipeline = bind_pipeline,
        .vkCmdBindDescriptorSets = bind_sets, .vkCmdDispatch = dispatch
    };
    doe_vk_pipeline pipeline = { .context = &context, .references = 1, .binding_count = 1, .group_count = 1 };
    pipeline.bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_STORAGE};
    doe_vk_pipeline other = pipeline;
    doe_vk_buffer buffer = { .context = &context, .references = 1, .usage = DOE_VK_BUFFER_STORAGE, .size = 256, .buffer = 5 };
    doe_vk_commands commands = { .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
    doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = {&buffer, 0, 64} };

    /* Repeated identical dispatches share the latest use's descriptor sets;
     * each still records its barrier, binds and dispatch. */
    for (unsigned i = 0; i < 3; ++i)
        assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    assert(mock.pools_created == 1 && mock.sets_allocated == 1 && mock.updates == 1);
    assert(mock.barriers == 3 && mock.pipeline_binds == 3 && mock.set_binds == 3 && mock.dispatches == 3);
    assert(pipeline.references == 2 && buffer.recorded_uses == 1);

    /* A different range or pipeline gets its own sets, and a use is shared
     * only with the immediately preceding one. */
    binding.value.buffer.offset = 64;
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    assert(doe_vk_commands_dispatch_resources(&commands, &other, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    binding.value.buffer.offset = 0;
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    /* Distinct uses get an exact pool each (DEFAULT), allocate sets from the
     * context block (AMD), or also reuse prior dispatches across and within
     * recordings (CANDIDATE). */
    const unsigned expected_sets = TEST_REUSE ? 3u : 4u;
    if (TEST_PROFILE != 0u)
        assert(mock.pools_created == 1 && mock.sets_allocated == expected_sets &&
            mock.updates == expected_sets && mock.dispatches == 6);
    else assert(mock.pools_created == 4 && mock.sets_allocated == 4 && mock.updates == 4 && mock.dispatches == 6);

    /* Validation still runs for a shared use. */
    buffer.content_status = DOE_VK_OUT_OF_MEMORY;
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OUT_OF_MEMORY);
    assert(commands.state == DOE_VK_POISONED && mock.dispatches == 6);
    buffer.content_status = DOE_VK_OK;

    release_uses(&commands);
    if (TEST_REUSE) {
        assert(mock.sets_freed == 0 && context.retained_dispatch_count == expected_sets && mock.pools_destroyed == 0);
        doe_vk_descriptor_blocks_clear(&context);
        assert(mock.sets_freed == 0 && context.retained_dispatch_count == 0);
    } else if (TEST_PROFILE != 0u) {
        assert(mock.sets_freed == mock.sets_allocated && mock.pools_destroyed == 0);
        doe_vk_descriptor_blocks_clear(&context);
    } else assert(mock.sets_freed == 0);
    assert(pipeline.references == 1 && other.references == 1);
    assert(mock.pools_destroyed == mock.pools_created);
    assert(buffer.recorded_uses == 0 && buffer.references == 1);
    graphics_reuse(&context);
    mixed_reuse(&context);
    if (TEST_REUSE) cross_recording_reuse_and_eviction();
    printf("PASS identical consecutive buffer dispatches share descriptor sets; ranges, pipelines and validation stay per dispatch; %s\n",
        TEST_REUSE ? "DESCRIPTOR_REUSE: 32-set blocks, cross-recording cache, synchronous eviction, zero-free pool clear" :
        TEST_PROFILE != 0u ? "DESCRIPTOR_BLOCKS: sets come from and return to one context block" :
        "DEFAULT: one exact pool per use, destroyed at release");
    return 0;
}
