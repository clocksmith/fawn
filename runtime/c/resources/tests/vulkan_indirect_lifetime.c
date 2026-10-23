/* SPDX-License-Identifier: MIT
 * Actual C compiler -> GPU argument production -> guarded indirect execution.
 * Explicit software ICD only; no CPU argument validation or GPU emulation. */
#include "vulkan_fixture.h"
#include "doe_vk_indirect.h"
#include "doe_shader.h"

static void *shader_allocate(void *state, size_t size, size_t alignment)
{
    return allocate(state, NULL, size, alignment);
}
static void shader_deallocate(void *state, void *memory, size_t size, size_t alignment)
{
    deallocate(state, NULL, memory, size, alignment);
}

static doe_shader *compile(lifecycle *life, const char *source, size_t length,
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

static doe_shader *compile_file(lifecycle *life, const char *path, const char *compiler_identity)
{
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && length <= 8192 && fseek(file, 0, SEEK_SET) == 0);
    char source[8192];
    assert(fread(source, 1, (size_t)length, file) == (size_t)length && fclose(file) == 0);
    return compile(life, source, (size_t)length, compiler_identity);
}

static doe_resource_buffer *buffer(const doe_resource_config *config, lifecycle *life,
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

static doe_resource_pipeline *pipeline(const doe_resource_config *config, lifecycle *life,
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

static doe_resource_bind_group *group(doe_resource_layout *layout,
    doe_resource_buffer *buffer, const doe_resource_binding_limits *limits)
{
    doe_resource_binding binding = {.kind = DOE_BIND_BUFFER, .resource.buffer = {
        buffer, 0, doe_resource_buffer_description(buffer)->size}};
    doe_resource_bind_group *result;
    assert(doe_resource_bind_group_create(layout, &binding, 1, limits, &result) == DOE_RESOURCE_OK);
    return result;
}

static void lower(doe_vk_commands *commands, doe_vk_dispatch_validator *validator,
    const doe_resource_command *record)
{
    if (record->kind == DOE_COMMAND_COPY_BUFFER) {
        const doe_resource_buffer_copy *copy = &record->value.copy;
        require(doe_vk_commands_copy(commands, doe_resource_buffer_native(copy->source), copy->source_offset,
            doe_resource_buffer_native(copy->destination), copy->destination_offset, copy->size), "copy actual counter");
        return;
    }
    assert(record->kind == DOE_COMMAND_DISPATCH || record->kind == DOE_COMMAND_DISPATCH_INDIRECT);
    assert(record->value.dispatch.group_count == 1 && !record->value.dispatch.dynamic_offsets);
    doe_resource_binding resolved;
    assert(doe_resource_bind_group_resolve(record->value.dispatch.groups[0], NULL, 0,
        &resolved, 1) == DOE_RESOURCE_OK);
    assert(resolved.kind == DOE_BIND_BUFFER && resolved.binding == 0);
    doe_vk_binding_resource binding = {.kind = DOE_VK_BINDING_STORAGE, .value.buffer = {
        doe_resource_buffer_native(resolved.resource.buffer.buffer),
        resolved.resource.buffer.offset, resolved.resource.buffer.size}};
    doe_vk_pipeline *native = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    if (record->kind == DOE_COMMAND_DISPATCH_INDIRECT)
        require(doe_vk_commands_dispatch_indirect(commands, validator, native, &binding, 1,
            doe_resource_buffer_native(record->value.dispatch.indirect),
            record->value.dispatch.indirect_offset), "GPU guarded indirect dispatch");
    else
        require(doe_vk_commands_dispatch_resources(commands, native, &binding, 1,
            record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z), "GPU argument producer");
}

int main(int argc, char **argv)
{
    if (argc != 5 || !argv[3][0] || strcmp(argv[4], "--allow-software")) return 2;
    const char *compiler_identity = argv[3];
    const doe_vk_options options = {.allow_cpu_adapter = true};
    doe_vk_context *context;
    require(doe_vk_context_create(&options, &context, NULL), "create explicit software context");
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    if (!caps.software_adapter) {
        doe_vk_context_release(context);
        return 77;
    }
    printf("Resource indirect Vulkan consumer: %s; C ABI9 shaders, software only\n", caps.device_name);
    lifecycle life = {0};
    const doe_resource_config config = {.allocator = {&life, NULL, allocate, deallocate},
        .device = {context, alive, retain, release}};
    const uint32_t requested_limits[] = {2, 3, 4};
    char source[DOE_VK_INDIRECT_SOURCE_CAPACITY];
    size_t length;
    require(doe_vk_dispatch_validator_source(requested_limits, source, sizeof(source), &length), "guard source");
    doe_shader *shader = compile(&life, source, length, compiler_identity);
    doe_shader_view view;
    assert(doe_shader_borrow(shader, &view) == DOE_SHADER_OK);
    doe_vk_dispatch_validator_desc desc = {.spirv = view.words, .word_count = view.counts.words};
    memcpy(desc.max_workgroup_count, requested_limits, sizeof(desc.max_workgroup_count));
    doe_vk_dispatch_validator *validator;
    require(doe_vk_dispatch_validator_create(context, &desc, &validator, NULL), "guard owner");
    doe_shader_release(shader);

    /* COPY_SRC is a native implementation capability, not new public usage. */
    doe_resource_buffer *arguments = buffer(&config, &life, 80,
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect,
        DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_INDIRECT | DOE_VK_BUFFER_COPY_SRC);
    doe_resource_buffer *counter = buffer(&config, &life, 4,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC);
    doe_resource_buffer *readback = buffer(&config, &life, 4, WGPUBufferUsage_CopyDst, DOE_VK_BUFFER_COPY_DST);
    assert(!(doe_resource_buffer_description(arguments)->usage & WGPUBufferUsage_CopySrc));
    doe_vk_buffer *native_arguments = doe_resource_buffer_native(arguments);
    doe_vk_buffer *native_readback = doe_resource_buffer_native(readback);
    const doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Compute,
        .buffer_type = WGPUBufferBindingType_Storage, .minimum_size = 4};
    doe_resource_layout *layout;
    doe_resource_pipeline_layout *pipeline_layout;
    assert(doe_resource_layout_create(&config, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_layout_create(&config, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    doe_resource_pipeline *producer = pipeline(&config, &life, pipeline_layout, argv[1], compiler_identity);
    doe_resource_pipeline *consumer = pipeline(&config, &life, pipeline_layout, argv[2], compiler_identity);
    doe_resource_pipeline_layout_release(pipeline_layout);
    const doe_resource_binding_limits limits = {caps.uniform_offset_alignment, caps.storage_offset_alignment,
        caps.max_uniform_buffer_range, caps.max_storage_buffer_range};
    doe_resource_bind_group *producer_group = group(layout, arguments, &limits);
    doe_resource_bind_group *consumer_group = group(layout, counter, &limits);
    doe_resource_layout_release(layout);
    const doe_resource_dispatch_group write_arguments = {producer_group, NULL, 0};
    doe_resource_encoder *rejected;
    assert(doe_resource_encoder_create(&config, &rejected) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_dispatch_indirect(rejected, producer, &write_arguments, 1,
        arguments, 4) == DOE_RESOURCE_USAGE);
    doe_resource_encoder_release(rejected);

    doe_resource_encoder *encoder;
    doe_resource_compute_pass *pass;
    assert(doe_resource_encoder_create(&config, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_set_pipeline(pass, producer) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_set_group(pass, 0, &write_arguments) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_dispatch(pass, 1, 1, 1) == DOE_RESOURCE_OK);
    const doe_resource_dispatch_group write_counter = {consumer_group, NULL, 0};
    assert(doe_resource_compute_pass_set_pipeline(pass, consumer) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_set_group(pass, 0, &write_counter) == DOE_RESOURCE_OK);
    const uint64_t offsets[] = {4, 20, 36, 52, 68, 4};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); i++)
        assert(doe_resource_compute_pass_dispatch_indirect(pass, arguments, offsets[i]) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_compute_pass_release(pass);
    const doe_resource_buffer_copy copy = {counter, readback, 0, 0, 4};
    assert(doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_bind_group_release(producer_group);
    doe_resource_bind_group_release(consumer_group);
    doe_resource_pipeline_release(producer);
    doe_resource_pipeline_release(consumer);
    doe_resource_buffer_release(arguments);
    doe_resource_buffer_release(counter);
    doe_resource_buffer_release(readback);
    assert(!life.native_releases);
    doe_vk_commands *native_commands;
    require(doe_vk_commands_create(context, &native_commands, NULL), "native commands");
    doe_vk_context_release(context);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 8);
    for (size_t i = 0; i < count; i++) lower(native_commands, validator, &records[i]);
    doe_vk_dispatch_validator_release(validator); /* Command leases own scratch and both pipelines. */
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    require(doe_vk_commands_submit(native_commands, NULL), "submit actual indirect stream");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_vk_commands_release(native_commands) == DOE_VK_BUSY);
    uint32_t actual;
    assert(doe_vk_buffer_read(native_readback, 0, &actual, sizeof(actual)) == DOE_VK_BUSY);
    assert(!life.native_releases);
    require(doe_vk_commands_wait(native_commands, UINT64_C(5000000000), NULL), "wait indirect stream");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(life.native_releases == 5 && !life.allocations);
    require(doe_vk_buffer_read(native_readback, 0, &actual, sizeof(actual)), "actual invocation count");
    assert(actual == 4);
    const uint32_t expected[] = {
        0xa5a5a5a5, 2, 1, 1, 0xa5a5a5a5, 3, 1, 1,
        0xa5a5a5a5, 1, 4, 1, 0xa5a5a5a5, 1, 1, 5, 0xa5a5a5a5, 0, 1, 1
    };
    uint32_t produced[20];
    require(doe_vk_buffer_read(native_arguments, 0, produced, sizeof(produced)), "original GPU arguments");
    assert(!memcmp(produced, expected, sizeof(expected)));
    require(doe_vk_commands_release(native_commands), "retire exact native resources");
    puts("PASS GPU-produced valid/over-requested-limit XYZ/zero/valid indirect arguments, exact four atomic invocations, unchanged source/padding, canonical usage-conflict rejection, pending guards and five terminal finalizers");
    return 0;
}
