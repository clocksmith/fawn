/* SPDX-License-Identifier: MIT
 * Same-path replacement must not execute one shader and attest another. */
#define main doe_cli_identity_unused_main
#include "../src/main.c"
#undef main
#include "runtime_internal.h"
#include <assert.h>

static size_t allocation, fail_allocation;
void *__real_malloc(size_t size);
void *__real_realloc(void *memory, size_t size);
void *__wrap_malloc(size_t size)
{
    if (fail_allocation && ++allocation == fail_allocation) return NULL;
    return __real_malloc(size);
}
void *__wrap_realloc(void *memory, size_t size)
{
    if (fail_allocation && ++allocation == fail_allocation) return NULL;
    return __real_realloc(memory, size);
}
static unsigned group_creations, view_creations;
static bool reject_group;
WGPUBindGroup __real_wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor *descriptor);
WGPUBindGroup __wrap_wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor *descriptor)
{
    ++group_creations;
    if (reject_group) return NULL;
    return __real_wgpuDeviceCreateBindGroup(device, descriptor);
}
WGPUTextureView __real_wgpuTextureCreateView(WGPUTexture texture, const WGPUTextureViewDescriptor *descriptor);
WGPUTextureView __wrap_wgpuTextureCreateView(WGPUTexture texture, const WGPUTextureViewDescriptor *descriptor)
{
    ++view_creations;
    return __real_wgpuTextureCreateView(texture, descriptor);
}

static void binding_reuse(doe_runtime *runtime, doe_command *command)
{
    doe_trace_execution execution;
    doe_kernel_binding *binding = command->as.dispatch.bindings;
    pipeline_entry *pipeline = &runtime->pipelines[1];
    WGPUBindGroup saved = pipeline->bound_groups[0];
    WGPUBuffer saved_buffer = pipeline->bound_resources[0].buffer;
    unsigned groups = group_creations;
    doe_output_oracle oracle = {.group=0,.binding=0,.dispatch_count=1};
    char actual[65], expected[65];
    assert(runtime_validate_oracle(runtime, command, &oracle, actual, &execution));
    uint32_t value = 22;
    uint8_t digest[32];
    doe_sha256(&value, sizeof(value), digest);
    doe_sha256_hex(digest, expected);
    assert(!strcmp(actual, expected) && group_creations == groups + 1);
    assert(pipeline->bound_groups[0] == saved && pipeline->bound_resources[0].buffer == saved_buffer);
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 1);
    /* Stream handle names do not invalidate groups when the native object is
     * identical; clearing/recreating that handle must invalidate them. */
    runtime->buffers.entries[0].handle = binding->handle = 2;
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 1);
    runtime_buffer_table_clear(&runtime->buffers);
    assert(runtime_create_buffer(runtime, 2, 512, &execution));
    reject_group = true;
    assert(!doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 2);
    assert(pipeline->bound_groups[0] == saved && pipeline->bound_resources[0].buffer == saved_buffer);
    reject_group = false;
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 3);
    assert(pipeline->bound_resources[0].buffer != saved_buffer);
    binding->buffer_size = 8;
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 4);
    binding->buffer_offset = 256;
    binding->buffer_size = 4;
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 5);
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 5);
    binding->buffer_offset = 0;
    binding->buffer_size = UINT64_MAX;
    assert(doe_runtime_execute(runtime, command, &execution) && group_creations == groups + 6);
}

static void texture_reuse(doe_runtime *runtime, doe_command *command)
{
    doe_kernel_binding *original = command->as.dispatch.bindings;
    doe_kernel_binding bindings[2] = {original[0],
        {.binding=1,.handle=3,.resource=DOE_RESOURCE_TEXTURE,.sample_type=WGPUTextureSampleType_Float}};
    bindings[0].buffer_size = 4;
    command->as.dispatch.bindings = bindings;
    command->as.dispatch.binding_count = 2;
    unsigned groups = group_creations, views = view_creations;
    doe_trace_execution execution;
    doe_texture_desc desc = {.handle=3,.width=1,.height=1,.depth=1,.format=DOE_FORMAT_RGBA8_UNORM,
        .usage=WGPUTextureUsage_TextureBinding};
    assert(runtime_ensure_texture(runtime, &desc, &execution));
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(group_creations == groups + 1 && view_creations == views + 1);
    pipeline_entry *pipeline = &runtime->pipelines[2];
    WGPUTexture first = pipeline->bound_resources[1].texture;
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(group_creations == groups + 1 && view_creations == views + 1);
    desc.width = 2;
    assert(runtime_ensure_texture(runtime, &desc, &execution));
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(group_creations == groups + 2 && view_creations == views + 2);
    assert(pipeline->bound_resources[1].texture != first);
    desc.format = DOE_FORMAT_R8_UNORM;
    assert(runtime_ensure_texture(runtime, &desc, &execution));
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(group_creations == groups + 3 && view_creations == views + 3);
    assert(pipeline->bound_resources[1].format == WGPUTextureFormat_R8Unorm);
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(group_creations == groups + 3 && view_creations == views + 3);
    /* Signature remains part of pipeline identity even for unused bindings. */
    bindings[1].sample_type = WGPUTextureSampleType_UnfilterableFloat;
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(runtime->pipeline_count == 4 && group_creations == groups + 4 && view_creations == views + 4);
    command->as.dispatch.binding_count = 1;
    command->as.dispatch.bindings = original;
    assert(doe_runtime_execute(runtime, command, &execution));
    assert(group_creations == groups + 4 && view_creations == views + 4);
}

static void source_hash(unsigned value, char source[256], char hash[65])
{
    int length = snprintf(source, 256, "@group(0) @binding(0) var<storage, read_write> out:array<u32>; "
        "@compute @workgroup_size(1) fn main(){out[0]=%uu;}", value);
    assert(length > 0 && length < 256);
    uint8_t digest[32];
    doe_sha256(source, (size_t)length, digest);
    doe_sha256_hex(digest, hash);
}
static void replace_source(const char *path, const char *source)
{
    FILE *out = doe_host_fopen(path, "wb");
    assert(out && fwrite(source, 1, strlen(source), out) == strlen(source));
    assert(!fclose(out));
}
static void snapshot_faults(void)
{
    for (size_t fail_at = 1; fail_at <= 5; fail_at++) {
        doe_runtime runtime = {0};
        size_t index = SIZE_MAX;
        fail_allocation = fail_at;
        allocation = 0;
        assert(!runtime_record_kernel_source(&runtime, "module", "path", "main", "abc", 3, &index));
        fail_allocation = 0;
        assert(!runtime.kernel_source_count && index == SIZE_MAX);
        runtime_kernel_sources_clear(&runtime);
    }
    doe_runtime runtime = {0};
    size_t index;
    char source[] = "abc";
    assert(runtime_record_kernel_source(&runtime, "module", "path", "main", source, 3, &index));
    source[0] = 'x';
    assert(!memcmp(runtime.kernel_sources[0].bytes, "abc", 3));
    fail_allocation = 1;
    allocation = 0;
    assert(runtime_record_kernel_source(&runtime, "module", "path", "main", "abc", 3, &index));
    assert(!allocation && runtime.kernel_source_count == 1);
    fail_allocation = 0;
    for (size_t i = 1; i < 8; i++) {
        char name[16];
        snprintf(name, sizeof(name), "module%zu", i);
        assert(runtime_record_kernel_source(&runtime, name, "path", "main", "abc", 3, &index));
    }
    const char *saved = runtime.kernel_sources[0].bytes;
    allocation = 0;
    fail_allocation = 5; /* New record copies succeed; existing table growth fails. */
    assert(!runtime_record_kernel_source(&runtime, "ninth", "path", "main", "xyz", 3, &index));
    fail_allocation = 0;
    assert(runtime.kernel_source_count == 8 && runtime.kernel_sources[0].bytes == saved);
    assert(!memcmp(saved, "abc", 3));
    runtime_kernel_sources_clear(&runtime);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    snapshot_faults();
    char source[3][256], hashes[3][65];
    for (unsigned i = 0; i < 3; i++) source_hash((i + 1) * 11, source[i], hashes[i]);
    replace_source(argv[1], source[0]);
    doe_runtime_options options = {.adapter=DOE_ADAPTER_SOFTWARE,.upload_submit_every=1};
    char message[256];
    doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
    if (!runtime) fprintf(stderr, "%s\n", message);
    assert(runtime);
    doe_kernel_binding binding = {.binding=0,.handle=1,.resource=DOE_RESOURCE_BUFFER,
        .buffer_type=DOE_BINDING_STORAGE,.buffer_size=4};
    doe_command command = {.kind=DOE_COMMAND_KERNEL_DISPATCH,.label="identity"};
    command.as.dispatch.kernel = argv[1];
    command.as.dispatch.entry_point = "main";
    command.as.dispatch.x = command.as.dispatch.y = command.as.dispatch.z = 1;
    command.as.dispatch.repeat = 1;
    command.as.dispatch.bindings = &binding;
    command.as.dispatch.binding_count = 1;
    doe_trace_execution execution;
    assert(doe_runtime_prewarm_kernel(runtime, &command, &execution));
    char *source_workspace = runtime->kernel_workspace.bytes;
    char *path_workspace = runtime->kernel_workspace.paths[0];
    replace_source(argv[1], source[1]);
    for (size_t i = 0; i < 2; i++) {
        assert(doe_runtime_execute(runtime, &command, &execution));
        assert(!strcmp(execution.shader_source_sha256, hashes[1]));
        assert(group_creations == 1 && !view_creations);
        assert(runtime->kernel_workspace.bytes == source_workspace && runtime->kernel_workspace.paths[0] == path_workspace);
        doe_host_output row_output;
        FILE *row = doe_host_output_open(&row_output);
        const doe_trace_identity identity = {"doe-c-runtime", "doe_c_vulkan"};
        uint64_t hash = doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, command.label,
            "kernel_dispatch", command.as.dispatch.kernel, NULL, &command.semantic);
        assert(row && doe_trace_write_row(row, &identity, 0, 0, hash, DOE_TRACE_HASH_SEED,
            &command, NULL, &execution));
        char *row_bytes;
        size_t row_size;
        assert(doe_host_output_close(&row_output, &row_bytes, &row_size));
        assert(strstr(row_bytes, "\"shaderSourceSha256\":") && strstr(row_bytes, hashes[1]));
        free(row_bytes);
        execution.ok = false;
        row = doe_host_output_open(&row_output);
        assert(row && doe_trace_write_row(row, &identity, 0, 0, hash, DOE_TRACE_HASH_SEED,
            &command, NULL, &execution));
        assert(doe_host_output_close(&row_output, &row_bytes, &row_size));
        assert(!strstr(row_bytes, "shaderSourceSha256"));
        free(row_bytes);
        execution.ok = true;
        uint8_t *bytes;
        size_t size;
        assert(doe_runtime_read_buffer(runtime, 1, &bytes, &size, &execution) && size == 4);
        uint32_t result;
        memcpy(&result, bytes, sizeof(result));
        free(bytes);
        assert(result == 22);
    }
    assert(runtime->pipeline_count == 2 && doe_runtime_source_receipt_count(runtime) == 2);
    binding_reuse(runtime, &command);
    texture_reuse(runtime, &command);
    assert(doe_runtime_source_receipt_count(runtime) == 2);
    doe_command barrier = {.kind=DOE_COMMAND_BARRIER,.label="barrier"};
    assert(doe_runtime_execute(runtime, &barrier, &execution) && !execution.shader_source_sha256[0]);
    replace_source(argv[1], source[2]);
    for (size_t i = 0; i < 2; i++) {
        doe_runtime_source_receipt receipt;
        assert(doe_runtime_source_receipt_at(runtime, i, &receipt));
        assert(!strcmp(receipt.sha256, hashes[i]) && receipt.byte_length == strlen(source[i]));
        assert(!strcmp(receipt.path, argv[1]) && !strcmp(receipt.entry_point, "main"));
    }
    doe_host_output output;
    FILE *out = doe_host_output_open(&output);
    assert(out && write_shader_receipts(out, runtime));
    char *receipt_bytes;
    size_t receipt_size;
    assert(doe_host_output_close(&output, &receipt_bytes, &receipt_size));
    assert(strstr(receipt_bytes, hashes[0]) && strstr(receipt_bytes, hashes[1]) && !strstr(receipt_bytes, hashes[2]));
    free(receipt_bytes);
    doe_runtime_close(runtime);
    assert(!remove(argv[1]));
    puts("PASS exact source cache identity, same-path replacement, immutable receipts, transactional binding/view reuse, isolated oracle and allocation rollback; CPU Vulkan");
    return 0;
}
