/* SPDX-License-Identifier: MIT
 * Exact artifact-owner composition for bounded one-storage compute fixtures. */
#ifndef DOE_RESOURCE_VULKAN_COMPUTE_FIXTURE_H
#define DOE_RESOURCE_VULKAN_COMPUTE_FIXTURE_H
#include "vulkan_fixture.h"
#include "doe_shader.h"

static inline void *shader_allocate(void *state, size_t size, size_t alignment)
{
    return allocate(state, NULL, size, alignment);
}
static inline void shader_deallocate(void *state, void *memory, size_t size, size_t alignment)
{
    deallocate(state, NULL, memory, size, alignment);
}

static inline doe_shader *compile(lifecycle *life, const char *source, size_t length,
    const char *compiler_identity)
{
    doe_shader_options options = {
        .allocator = {life, shader_allocate, shader_deallocate},
        .capacities = {.typed = {.source_bytes = 8192, .tokens = 4096,
            .statements = 256, .expressions = 1024, .types = 64, .members = 64,
            .resources = 8, .access_steps = 1024, .constants = 128, .constant_edges = 256,
            .ir_instructions = 8192, .ir_operands = 2048, .target_declarations = 1024,
            .traversal_items = 193, .words = 32768, .expression_depth = 64,
            .control_depth = 32, .type_syntax_depth = 32},
            .entries = 8, .entry_inputs = 40, .entry_references = 64},
        .selection = {DOE_COMPILER_SELECT_NAME, 0, "main", 4},
        .compiler_identity = compiler_identity, .compiler_identity_size = strlen(compiler_identity)
    };
    doe_shader *shader;
    doe_shader_error error;
    doe_shader_status status = doe_shader_create(&options, source, length, &shader, &error);
    if (status != DOE_SHADER_OK) {
        fprintf(stderr, "indirect fixture compiler: %s\n", error.diagnostic.message);
        exit(1);
    }
    doe_shader_view view;
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    assert(view.counts.entries == 1 && view.counts.resources == 1);
    assert(view.entries[0].stage == DOE_COMPILER_STAGE_COMPUTE);
    for (size_t i = 0; i < 3; i++) assert(view.entries[0].workgroup_size[i] == 1);
    assert(view.resources[0].group == 0 && view.resources[0].binding == 0 &&
        view.resources[0].kind == DOE_COMPILER_STORAGE_BUFFER && view.resources[0].access == DOE_COMPILER_READ_WRITE);
    assert(view.interface_count == 0 && view.handle_count == 0);
    return shader;
}

static inline doe_shader *compile_file(lifecycle *life, const char *path, const char *compiler_identity)
{
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && length <= 8192 && fseek(file, 0, SEEK_SET) == 0);
    char source[8192];
    assert(fread(source, 1, (size_t)length, file) == (size_t)length && fclose(file) == 0);
    return compile(life, source, (size_t)length, compiler_identity);
}

static inline doe_resource_buffer *buffer(const doe_resource_config *config, lifecycle *life,
    uint64_t size, WGPUBufferUsage usage, uint32_t native_usage)
{
    doe_vk_buffer *native;
    require(doe_vk_buffer_create(config->device.identity, size, native_usage, DOE_VK_MEMORY_HOST,
        &native, NULL), "buffer create");
    doe_resource_buffer *result;
    const doe_resource_buffer_desc desc = {size, usage, false};
    assert(doe_resource_buffer_create(config, &desc,
        (doe_resource_native){native, life, release_buffer}, &result) == DOE_RESOURCE_OK);
    return result;
}

static inline doe_resource_pipeline *pipeline(const doe_resource_config *config, lifecycle *life,
    doe_resource_pipeline_layout *layout, const char *path, const char *compiler_identity)
{
    doe_shader *shader = compile_file(life, path, compiler_identity);
    doe_shader_view view;
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    doe_vk_pipeline *native;
    const doe_vk_binding_layout binding = {0, 0, DOE_VK_BINDING_STORAGE};
    require(doe_vk_pipeline_create(config->device.identity, view.words, view.counts.words, "main",
        &binding, 1, &native, NULL), "fixture compute pipeline");
    doe_shader_release(shader);
    doe_resource_pipeline *result;
    const uint32_t workgroup[] = {1, 1, 1};
    assert(doe_resource_pipeline_create(config, layout,
        (doe_resource_native){native, life, release_pipeline}, workgroup, &result) == DOE_RESOURCE_OK);
    return result;
}

static inline doe_resource_bind_group *group(doe_resource_layout *layout,
    doe_resource_buffer *buffer, const doe_resource_binding_limits *limits)
{
    doe_resource_binding binding = {.kind = DOE_BIND_BUFFER, .resource.buffer = {
        buffer, 0, doe_resource_buffer_description(buffer)->size}};
    doe_resource_bind_group *result;
    assert(doe_resource_bind_group_create(layout, &binding, 1, limits, &result) == DOE_RESOURCE_OK);
    return result;
}

#endif
