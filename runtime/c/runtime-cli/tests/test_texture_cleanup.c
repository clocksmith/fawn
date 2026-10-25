/* SPDX-License-Identifier: MIT
 * Observe real WebGPU view ownership when a later buffer binding fails.
 * Link wrapping counts only this CLI's acquisitions/releases, not the driver. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t acquired, released;
static bool fail_buffer;
WGPUTextureView __real_wgpuTextureCreateView(WGPUTexture texture, WGPUTextureViewDescriptor const *descriptor);
void __real_wgpuTextureViewRelease(WGPUTextureView view);
WGPUBuffer __real_wgpuDeviceCreateBuffer(WGPUDevice device, WGPUBufferDescriptor const *descriptor);

WGPUTextureView __wrap_wgpuTextureCreateView(WGPUTexture texture, WGPUTextureViewDescriptor const *descriptor)
{
    WGPUTextureView view = __real_wgpuTextureCreateView(texture, descriptor);
    if (view) acquired++;
    return view;
}

void __wrap_wgpuTextureViewRelease(WGPUTextureView view)
{
    if (view) released++;
    __real_wgpuTextureViewRelease(view);
}

WGPUBuffer __wrap_wgpuDeviceCreateBuffer(WGPUDevice device, WGPUBufferDescriptor const *descriptor)
{
    return fail_buffer ? NULL : __real_wgpuDeviceCreateBuffer(device, descriptor);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    doe_runtime_options options = {.adapter = DOE_ADAPTER_SOFTWARE, .upload_submit_every = 1, .kernel_root = argv[1]};
    char message[256];
    doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
    if (!runtime) fprintf(stderr, "%s\n", message);
    assert(runtime);
    doe_kernel_binding bindings[] = {
        {.binding = 0, .handle = 1, .resource = DOE_RESOURCE_TEXTURE, .sample_type = WGPUTextureSampleType_Float,
            .has_format = true, .format = DOE_FORMAT_RGBA8_UNORM},
        {.binding = 1, .handle = 2, .resource = DOE_RESOURCE_STORAGE_TEXTURE, .storage_access = WGPUStorageTextureAccess_WriteOnly,
            .has_format = true, .format = DOE_FORMAT_RGBA8_UNORM},
        {.binding = 2, .handle = 3, .resource = DOE_RESOURCE_BUFFER, .buffer_type = DOE_BINDING_STORAGE,
            .buffer_size = UINT64_MAX},
    };
    doe_command command = {.kind = DOE_COMMAND_KERNEL_DISPATCH, .label = "kernel_dispatch"};
    command.as.dispatch.kernel = "texture_sample_to_storage_64";
    command.as.dispatch.entry_point = "main";
    command.as.dispatch.x = command.as.dispatch.y = command.as.dispatch.z = 1;
    command.as.dispatch.repeat = 1;
    command.as.dispatch.bindings = bindings;
    command.as.dispatch.binding_count = 3;
    for (int scenario = 0; scenario < 3; scenario++) {
        acquired = released = 0;
        if (scenario == 1) {
            bindings[2].buffer_size = 16;
            bindings[2].buffer_offset = UINT64_MAX - 3;
        } else if (scenario == 2) {
            bindings[2].buffer_offset = 0;
            fail_buffer = true;
        }
        doe_trace_execution execution;
        assert(!doe_runtime_execute(runtime, &command, &execution));
        if (acquired != 2 || released != acquired)
            fprintf(stderr, "scenario %d: %s; acquired=%zu released=%zu\n", scenario, execution.status_code, acquired, released);
        assert(acquired == 2 && released == acquired);
        assert(!strcmp(execution.status_code, scenario == 2 ? "buffer_create_failed" : "kernel_binding_size"));
    }
    const doe_runtime_adapter *adapter = doe_runtime_adapter_info(runtime);
    printf("PASS texture views released after missing size, overflow and buffer allocation failure: %s (%s, %s)\n",
        adapter->device, adapter->adapter_type, adapter->backend);
    doe_runtime_close(runtime);
    return 0;
}
