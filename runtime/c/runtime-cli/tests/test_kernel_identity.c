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
    replace_source(argv[1], source[1]);
    for (size_t i = 0; i < 2; i++) {
        assert(doe_runtime_execute(runtime, &command, &execution));
        assert(!strcmp(execution.shader_source_sha256, hashes[1]));
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
    puts("PASS exact source cache identity, same-path replacement, immutable receipts and allocation rollback; CPU Vulkan");
    return 0;
}
