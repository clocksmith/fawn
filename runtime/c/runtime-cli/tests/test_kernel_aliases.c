/* SPDX-License-Identifier: MIT
 * Observe the real bind-group boundary, then let native admission execute. */
#include "runtime_internal.h"
#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool observe_readonly;
static unsigned observed_groups;
static WGPUBindGroupLayout expected_layout;
static WGPUBuffer timed_input, timed_output;

WGPUBindGroup __real_wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor *descriptor);
WGPUBindGroup __wrap_wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor *descriptor)
{
    if (observe_readonly) {
        assert(descriptor->layout == expected_layout && descriptor->entryCount == 3);
        const WGPUBindGroupEntry *entries = descriptor->entries;
        assert(entries[0].buffer == entries[1].buffer && entries[0].buffer != entries[2].buffer);
        assert(entries[0].offset == 0 && entries[0].size == 512);
        assert(entries[1].offset == 256 && entries[1].size == 256);
        assert(entries[2].offset == 0 && entries[2].size == 4);
        if (observed_groups == 0) {
            timed_input = entries[0].buffer;
            timed_output = entries[2].buffer;
        } else {
            assert(entries[0].buffer != timed_input && entries[2].buffer != timed_output);
        }
        observed_groups++;
    }
    return __real_wgpuDeviceCreateBindGroup(device, descriptor);
}

static void write_shader(const char *path, bool writable)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fprintf(file,
        "@group(0) @binding(0) var<storage,%s> a:array<u32>;\n"
        "@group(0) @binding(1) var<storage,%s> b:array<u32>;\n"
        "@group(0) @binding(2) var<storage,read_write> out:array<u32>;\n"
        "@compute @workgroup_size(1) fn main(){%sout[0]=a[0]+b[0]+7u;}\n",
        writable ? "read_write" : "read", writable ? "read_write" : "read",
        writable ? "a[0]=1u;b[0]=2u;" : "") > 0);
    assert(!fclose(file));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char message[256];
    doe_runtime_options options = {.adapter = DOE_ADAPTER_SOFTWARE, .timestamp_mode = DOE_TIMESTAMP_OFF,
        .upload_submit_every = 1, .queue_wait_timeout_ns = UINT64_C(2000000000)};
    doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
    if (!runtime) fprintf(stderr, "%s\n", message);
    assert(runtime && !strcmp(doe_runtime_adapter_info(runtime)->adapter_type, "cpu"));
    doe_kernel_binding bindings[] = {
        {.binding=0, .handle=71, .resource=DOE_RESOURCE_BUFFER, .buffer_type=DOE_BINDING_READ_ONLY_STORAGE, .buffer_size=512},
        {.binding=1, .handle=71, .resource=DOE_RESOURCE_BUFFER, .buffer_type=DOE_BINDING_READ_ONLY_STORAGE, .buffer_offset=256, .buffer_size=256},
        {.binding=2, .handle=72, .resource=DOE_RESOURCE_BUFFER, .buffer_type=DOE_BINDING_STORAGE, .buffer_size=4},
    };
    doe_command command = {.kind=DOE_COMMAND_KERNEL_DISPATCH, .label="readonly-alias"};
    command.as.dispatch.kernel = argv[1];
    command.as.dispatch.entry_point = "main";
    command.as.dispatch.x = command.as.dispatch.y = command.as.dispatch.z = 1;
    command.as.dispatch.repeat = 1;
    command.as.dispatch.bindings = bindings;
    command.as.dispatch.binding_count = 3;
    write_shader(argv[1], false);
    doe_trace_execution execution;
    assert(doe_runtime_prewarm_kernel(runtime, &command, &execution));
    expected_layout = runtime->pipelines[0].groups[0];
    observe_readonly = true;
    assert(doe_runtime_execute(runtime, &command, &execution));
    assert(observed_groups == 1 && runtime->buffers.count == 2);
    uint32_t sentinel = 23;
    doe_command write = {.kind=DOE_COMMAND_BUFFER_WRITE};
    write.as.buffer_write.handle = 72;
    write.as.buffer_write.buffer_size = 4;
    write.as.buffer_write.words = &sentinel;
    write.as.buffer_write.word_count = 1;
    assert(doe_runtime_execute(runtime, &write, &execution));
    doe_output_oracle oracle = {.schema_version=1, .binding=2, .dispatch_count=1};
    char actual[65], expected[65];
    uint8_t digest[32], reference[] = {7,0,0,0};
    doe_sha256(reference, sizeof(reference), digest);
    doe_sha256_hex(digest, expected);
    assert(doe_runtime_validate_oracle(runtime, &command, &oracle, actual, &execution));
    assert(!strcmp(actual, expected) && observed_groups == 2 && runtime->buffers.count == 2);
    uint8_t *bytes = NULL;
    size_t size = 0;
    assert(doe_runtime_read_buffer(runtime, 72, &bytes, &size, &execution));
    assert(size == 4 && !memcmp(bytes, &sentinel, 4));
    free(bytes);
    observe_readonly = false;
    /* The same handle in two overlapping writable views must reach the real
     * native alias policy, not silently become two separate buffers. */
    write_shader(argv[1], true);
    bindings[0].buffer_type = bindings[1].buffer_type = DOE_BINDING_STORAGE;
    bindings[0].buffer_size = bindings[1].buffer_size = 4;
    bindings[1].buffer_offset = 0;
    assert(doe_runtime_prewarm_kernel(runtime, &command, &execution));
    bool accepted = doe_runtime_validate_oracle(runtime, &command, &oracle, actual, &execution);
    if (accepted) fprintf(stderr, "overlapping writable alias unexpectedly accepted\n");
    assert(!accepted);
    assert(doe_runtime_close(runtime));
    assert(remove(argv[1]) == 0);
    puts("PASS timed/proof shared readonly resource identity, exact ranges/layout, private isolation and native writable-alias rejection; CPU Vulkan only");
    return 0;
}
