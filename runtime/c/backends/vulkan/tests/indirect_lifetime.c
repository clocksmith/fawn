#include "../src/indirect_internal.h"
#include <assert.h>
#include <stdio.h>

/* Native packet/lifetime observation only, not a CPU shader implementation. */
static unsigned sequence, barriers, pipelines_destroyed, buffers_destroyed, pools_destroyed;
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command; (void)source; (void)destination; (void)flags;
    (void)memory; (void)buffers; (void)images;
    assert(memory_count == 1 && buffer_count == 0 && image_count == 0);
    ++barriers;
}
static void copy_buffer(VkCommandBuffer command, VkBuffer source, VkBuffer destination,
    uint32_t count, const VkBufferCopy *copy) {
    (void)command;
    assert(sequence++ == 0 && source == 5 && destination == 3 && count == 1);
    assert(copy->srcOffset == 4 && copy->dstOffset == 0 && copy->size == 12);
}
static VkResult create_pool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorPool *out) {
    (void)device; (void)allocator;
    assert(info->maxSets == 1 && info->poolSizeCount == 1 && info->pPoolSizes[0].descriptorCount == 1);
    *out = 9;
    return 0;
}
static VkResult allocate_sets(VkDevice device, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *out) {
    (void)device;
    assert(info->descriptorPool == 9 && info->descriptorSetCount == 1 && info->pSetLayouts[0] == 7);
    *out = 10;
    return 0;
}
static void update_sets(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
    uint32_t copies, const void *copy) {
    (void)device; (void)copy;
    assert(sequence++ == 1 && count == 1 && copies == 0 && writes[0].dstSet == 10);
    assert(writes[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    assert(writes[0].pBufferInfo->buffer == 3 && writes[0].pBufferInfo->offset == 0 && writes[0].pBufferInfo->range == 12);
}
static void bind_pipeline(VkCommandBuffer command, int32_t point, VkPipeline pipeline) {
    (void)command;
    assert(point == VK_PIPELINE_BIND_POINT_COMPUTE);
    if (pipeline == 8) assert(sequence++ == 2);
    else assert(pipeline == 18 && sequence++ == 4);
}
static void bind_sets(VkCommandBuffer command, int32_t point, VkPipelineLayout layout,
    uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t offsets, const uint32_t *values) {
    (void)command; (void)values;
    assert(point == VK_PIPELINE_BIND_POINT_COMPUTE && layout == 6 && first == 0);
    assert(count == 1 && sets[0] == 10 && offsets == 0);
}
static void dispatch(VkCommandBuffer command, uint32_t x, uint32_t y, uint32_t z) {
    (void)command;
    assert(sequence++ == 3 && x == 1 && y == 1 && z == 1);
}
static void dispatch_indirect(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset) {
    (void)command;
    assert(sequence++ == 5 && buffer == 3 && offset == 0 && barriers == 4);
}
static void destroy_pipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pipeline == 8 || pipeline == 18);
    ++pipelines_destroyed;
}
static void destroy_layout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(layout == 6);
}
static void destroy_set(VkDevice device, VkDescriptorSetLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(layout == 7);
}
static void destroy_buffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(buffer == 3);
    ++buffers_destroyed;
}
static void destroy_pool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pool == 9);
    ++pools_destroyed;
}
static VkResult wait_lost(VkDevice device, uint32_t count, const VkFence *fences,
    VkBool32 all, uint64_t timeout) {
    (void)device; (void)fences; (void)timeout;
    assert(count == 1 && all == 1);
    return -4;
}
int main(void) {
    doe_vk_context context = {.references = 5}; /* Caller, two pipelines, scratch, commands. */
    context.caps.max_workgroup_count[0] = context.caps.max_workgroup_count[1] = context.caps.max_workgroup_count[2] = 1;
    context.caps.storage_offset_alignment = 16;
    context.caps.max_storage_buffer_range = 12;
    context.vk = (doe_vk_functions){.vkCmdPipelineBarrier = barrier, .vkCmdCopyBuffer = copy_buffer,
        .vkCreateDescriptorPool = create_pool, .vkAllocateDescriptorSets = allocate_sets,
        .vkUpdateDescriptorSets = update_sets, .vkCmdBindPipeline = bind_pipeline,
        .vkCmdBindDescriptorSets = bind_sets, .vkCmdDispatch = dispatch, .vkCmdDispatchIndirect = dispatch_indirect,
        .vkDestroyPipeline = destroy_pipeline, .vkDestroyPipelineLayout = destroy_layout,
        .vkDestroyDescriptorSetLayout = destroy_set, .vkDestroyBuffer = destroy_buffer,
        .vkDestroyDescriptorPool = destroy_pool, .vkWaitForFences = wait_lost};
    doe_vk_dispatch_validator *validator = calloc(1, sizeof(*validator));
    doe_vk_buffer *scratch = calloc(1, sizeof(*scratch));
    doe_vk_pipeline *sanitize = calloc(1, sizeof(*sanitize)), *target = calloc(1, sizeof(*target));
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(validator != NULL && scratch != NULL && sanitize != NULL && target != NULL && commands != NULL);
    *validator = (doe_vk_dispatch_validator){1, sanitize, scratch};
    *scratch = (doe_vk_buffer){.context = &context, .references = 1, .size = 12, .buffer = 3,
        .usage = DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_DST | DOE_VK_BUFFER_INDIRECT};
    *sanitize = (doe_vk_pipeline){.context = &context, .references = 1, .pipeline = 8,
        .layout = 6, .group_count = 1, .binding_count = 1, .sets = {7}, .bindings = {{0, 0, DOE_VK_BINDING_STORAGE}}};
    *target = (doe_vk_pipeline){.context = &context, .references = 1, .pipeline = 18};
    *commands = (doe_vk_commands){.context = &context, .state = DOE_VK_RECORDING, .uses_active = true};
    doe_vk_buffer args = {.context = &context, .references = 1, .size = 16, .buffer = 5,
        .usage = DOE_VK_BUFFER_COPY_SRC | DOE_VK_BUFFER_INDIRECT};
    doe_vk_commands bad = {.context = &context, .state = DOE_VK_RECORDING};
    assert(doe_vk_commands_dispatch_indirect(&bad, validator, target, NULL, 0, &args, 8) == DOE_VK_INVALID_ARGUMENT);
    bad.state = DOE_VK_RECORDING;
    args.usage = DOE_VK_BUFFER_INDIRECT;
    assert(doe_vk_commands_dispatch_indirect(&bad, validator, target, NULL, 0, &args, 4) == DOE_VK_INVALID_ARGUMENT);
    args.usage |= DOE_VK_BUFFER_COPY_SRC;
    bad.state = DOE_VK_RECORDING;
    target->graphics = true;
    assert(doe_vk_commands_dispatch_indirect(&bad, validator, target, NULL, 0, &args, 4) == DOE_VK_INVALID_ARGUMENT);
    target->graphics = false;
    bad.state = DOE_VK_RECORDING;
    target->binding_count = 1;
    assert(doe_vk_commands_dispatch_indirect(&bad, validator, target, NULL, 1, &args, 4) == DOE_VK_INVALID_ARGUMENT);
    target->binding_count = 0;
    assert(sequence == 0 && barriers == 0 && bad.buffers == NULL && bad.dispatches == NULL);
    assert(doe_vk_commands_dispatch_indirect(commands, validator, target, NULL, 0, &args, 4) == DOE_VK_OK);
    assert(sequence == 6 && args.references == 2 && scratch->references == 2 && target->references == 2);
    doe_vk_dispatch_validator_release(validator);
    doe_vk_pipeline_release(target);
    assert(buffers_destroyed == 0 && pipelines_destroyed == 0);
    commands->state = DOE_VK_UNCERTAIN;
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    commands->state = DOE_VK_SUBMITTED;
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_DEVICE_LOST);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(buffers_destroyed == 1 && pipelines_destroyed == 2 && pools_destroyed == 1);
    assert(context.references == 1 && args.references == 1 && args.content_status == DOE_VK_DEVICE_LOST);
    puts("PASS indirect copy/guard/target-bind/dispatch ordering, exact12-byte scratch, barriers, last-byte and usage/type/late-target rejection before native work, early validator release and uncertain/failed retirement");
    return 0;
}
