/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_INTERNAL_H
#define DOE_VK_INTERNAL_H
#include "../include/doe_vk.h"
#include "../include/doe_vk_render.h"
#include "vk_abi_functions.h"
#include "vk_abi_constants.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

doe_vk_status doe_vk_choose_device(doe_vk_context *context, const doe_vk_options *options, doe_vk_error *error);
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
    /* Compute pipelines by exact creation input, most recent first: live
     * ones and up to DOE_VK_RETAINED_PIPELINES released ones; see pipeline.c. */
    struct doe_vk_pipeline *compute_pipelines;
    uint32_t retained_pipelines;
};
enum { DOE_VK_RETAINED_PIPELINES = 32 };
/* Destroys released compute pipelines; the context is releasing its device. */
void doe_vk_pipeline_cache_clear(doe_vk_context *context);
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
    uint32_t color_count; /* Slots, including unused zero formats. */
    doe_vk_texture_format color_formats[DOE_VK_MAX_COLOR_ATTACHMENTS];
    doe_vk_texture_format depth_stencil_format;
    bool depth_write, stencil_write, stencil_test;
    uint32_t sample_count;
    float blend_constant[4];
    uint32_t vertex_count;
    doe_vk_vertex_layout vertices[DOE_VK_MAX_VERTEX_BUFFERS];
    uint32_t vertex_attribute_end[DOE_VK_MAX_VERTEX_BUFFERS];
    uint32_t binding_count;
    uint32_t group_count;
    doe_vk_binding_layout bindings[DOE_VK_MAX_BINDINGS];
    VkDescriptorSetLayout sets[DOE_VK_MAX_GROUPS];
    VkPipelineLayout layout;
    VkPipeline pipeline;
    /* Compute only: the exact creation input this pipeline shares under, and
     * its link in the context's live compute pipelines. */
    uint32_t *spirv;
    size_t word_count;
    char *entrypoint;
    struct doe_vk_pipeline *next_compute;
};
typedef struct doe_vk_buffer_use {
    doe_vk_buffer *buffer;
    struct doe_vk_buffer_use *next;
} doe_vk_buffer_use;
typedef struct doe_vk_dispatch_use {
    doe_vk_pipeline *pipeline;
    VkDescriptorPool pool;
    VkDescriptorSet sets[DOE_VK_MAX_GROUPS];
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
    DOE_VK_FAILED,
    DOE_VK_READY /* Reusable recording after proven success; may be submitted again. */
} doe_vk_command_state;
struct doe_vk_commands {
    doe_vk_context *context;
    doe_vk_command_state state;
    bool reusable;
    uint32_t references; /* Holders: creator plus each pending reusable submission. */
    doe_vk_error failure;
    bool uses_active;
    /* The queue accepted (or refused) the latest submission: its texture and
     * query state is already published, so completion only poisons on failure. */
    bool committed;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    doe_vk_buffer_use *buffers;
    doe_vk_dispatch_use *dispatches;
    struct doe_vk_render_use *renders;
    struct doe_vk_query_use *queries;
    struct doe_vk_texture_use *textures;
    struct doe_vk_surface *surface;
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
doe_vk_status doe_vk_prepare_resources(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count, doe_vk_dispatch_use **out);
void doe_vk_bind_prepared_resources(doe_vk_commands *commands, const doe_vk_dispatch_use *use);
doe_vk_status doe_vk_bind_resources(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count);
void doe_vk_render_release(doe_vk_commands *commands);
void doe_vk_queries_complete(doe_vk_commands *commands, doe_vk_status status);
/* Reusable recordings: every written or resolved query index was reset first by
 * the same recording, so each run's meaning is independent of prior pool state. */
bool doe_vk_queries_self_contained(const doe_vk_commands *commands);
/* Drops pool ownership (seal), or re-acquires it with health checks before a
 * resubmission; acquisition is all-or-nothing and leaves no owner on failure. */
void doe_vk_queries_detach(doe_vk_commands *commands);
doe_vk_status doe_vk_queries_acquire(doe_vk_commands *commands);
void doe_vk_queries_release(doe_vk_commands *commands);
uint32_t doe_vk_descriptor_type(doe_vk_binding_kind kind);
void doe_vk_dispatch_release(doe_vk_context *context, doe_vk_dispatch_use *use);
doe_vk_status doe_vk_bind_dispatch(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings,
    size_t binding_count);
#endif
