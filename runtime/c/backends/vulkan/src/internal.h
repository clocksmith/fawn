#ifndef DOE_VK_INTERNAL_H
#define DOE_VK_INTERNAL_H
#include "../include/doe_vk.h"
#include "../include/doe_vk_render.h"
#include "vk_abi_functions.h"
#include "vk_abi_constants.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

doe_vk_status doe_vk_load_symbol(void *library, const char *name,
    void *destination, size_t destination_size, doe_vk_error *error);

enum { DOE_VK_MAX_GROUPS = 4, DOE_VK_MAX_BINDINGS = 128 };
struct doe_vk_context {
    uint32_t references;
    bool lost;
    void *library;
    doe_vk_functions vk;
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    VkExtent3D transfer_granularity;
    doe_vk_caps caps;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
};
struct doe_vk_buffer {
    doe_vk_context *context;
    uint32_t references;
    uint32_t recorded_uses;
    doe_vk_status content_status;
    uint32_t usage;
    uint64_t size;
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
};
struct doe_vk_pipeline {
    doe_vk_context *context;
    uint32_t references;
    bool graphics;
    VkRenderPass render_pass;
    doe_vk_texture_format color_format;
    uint32_t vertex_count;
    doe_vk_vertex_layout vertices[DOE_VK_MAX_VERTEX_BUFFERS];
    uint32_t vertex_attribute_end[DOE_VK_MAX_VERTEX_BUFFERS];
    uint32_t binding_count;
    uint32_t group_count;
    doe_vk_binding_layout bindings[DOE_VK_MAX_BINDINGS];
    VkDescriptorSetLayout sets[DOE_VK_MAX_GROUPS];
    VkPipelineLayout layout;
    VkPipeline pipeline;
};
typedef struct doe_vk_buffer_use {
    doe_vk_buffer *buffer;
    struct doe_vk_buffer_use *next;
} doe_vk_buffer_use;
typedef struct doe_vk_dispatch_use {
    doe_vk_pipeline *pipeline;
    VkDescriptorPool pool;
    struct doe_vk_texture_view *views[DOE_VK_MAX_BINDINGS];
    struct doe_vk_sampler *samplers[DOE_VK_MAX_BINDINGS];
    struct doe_vk_dispatch_use *next;
} doe_vk_dispatch_use;
typedef enum doe_vk_command_state {
    DOE_VK_RECORDING,
    DOE_VK_POISONED,
    DOE_VK_SUBMITTED,
    DOE_VK_UNCERTAIN,
    DOE_VK_COMPLETE,
    DOE_VK_FAILED
} doe_vk_command_state;
struct doe_vk_commands {
    doe_vk_context *context;
    doe_vk_command_state state;
    doe_vk_status failure;
    bool uses_active;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    doe_vk_buffer_use *buffers;
    doe_vk_dispatch_use *dispatches;
    struct doe_vk_render_use *renders;
    struct doe_vk_query_use *queries;
    struct doe_vk_texture_use *textures;
};
doe_vk_status doe_vk_result(doe_vk_context *context, VkResult result,
    const char *operation, doe_vk_error *error);
doe_vk_status doe_vk_fail(doe_vk_status status, const char *operation,
    doe_vk_error *error);
bool doe_vk_range(uint64_t allocation, uint64_t offset, uint64_t size);
doe_vk_status doe_vk_memory_type(const VkPhysicalDeviceMemoryProperties *properties,
    uint32_t bits, uint32_t required, uint32_t preferred, uint32_t *out);
doe_vk_status doe_vk_poison(doe_vk_commands *commands, doe_vk_status status);
doe_vk_status doe_vk_use_buffer(doe_vk_commands *commands, doe_vk_buffer *buffer);
void doe_vk_barrier(doe_vk_commands *commands);
doe_vk_status doe_vk_recording(doe_vk_commands *commands);
void doe_vk_textures_complete(doe_vk_commands *commands, doe_vk_status status);
void doe_vk_textures_release(doe_vk_commands *commands);
bool doe_vk_shader_valid(const doe_vk_shader *shader);
doe_vk_status doe_vk_shader_module(doe_vk_context *context, const doe_vk_shader *shader,
    VkShaderModule *out, doe_vk_error *error);
doe_vk_status doe_vk_pipeline_layout_create(doe_vk_pipeline *pipeline,
    const doe_vk_binding_layout *layout, size_t count, uint32_t stages, doe_vk_error *error);
doe_vk_status doe_vk_bind_resources(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count);
void doe_vk_render_release(doe_vk_commands *commands);
void doe_vk_queries_complete(doe_vk_commands *commands, doe_vk_status status);
void doe_vk_queries_release(doe_vk_commands *commands);
uint32_t doe_vk_descriptor_type(doe_vk_binding_kind kind);
void doe_vk_dispatch_release(doe_vk_context *context, doe_vk_dispatch_use *use);
doe_vk_status doe_vk_bind_dispatch(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings,
    size_t binding_count);
#endif
