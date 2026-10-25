/* SPDX-License-Identifier: MIT
 * Count real ABI calls to distinguish completed diagnostics from parser-only
 * success. Link wraps forward to the provider; no fake WebGPU implementation. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned compilations, pipelines, finishes, draws, submissions;
WGPUFuture __real_wgpuShaderModuleGetCompilationInfo(WGPUShaderModule, WGPUCompilationInfoCallbackInfo);
WGPUFuture __real_wgpuDeviceCreateRenderPipelineAsync(WGPUDevice, WGPURenderPipelineDescriptor const *, WGPUCreateRenderPipelineAsyncCallbackInfo);
WGPUCommandBuffer __real_wgpuCommandEncoderFinish(WGPUCommandEncoder, WGPUCommandBufferDescriptor const *);
void __real_wgpuRenderPassEncoderDraw(WGPURenderPassEncoder, uint32_t, uint32_t, uint32_t, uint32_t);
void __real_wgpuQueueSubmit(WGPUQueue, size_t, WGPUCommandBuffer const *);

WGPUFuture __wrap_wgpuShaderModuleGetCompilationInfo(WGPUShaderModule module, WGPUCompilationInfoCallbackInfo callback)
{
    compilations++;
    return __real_wgpuShaderModuleGetCompilationInfo(module, callback);
}

WGPUFuture __wrap_wgpuDeviceCreateRenderPipelineAsync(WGPUDevice device, WGPURenderPipelineDescriptor const *descriptor,
    WGPUCreateRenderPipelineAsyncCallbackInfo callback)
{
    pipelines++;
    return __real_wgpuDeviceCreateRenderPipelineAsync(device, descriptor, callback);
}

WGPUCommandBuffer __wrap_wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, WGPUCommandBufferDescriptor const *descriptor)
{
    finishes++;
    return __real_wgpuCommandEncoderFinish(encoder, descriptor);
}

void __wrap_wgpuRenderPassEncoderDraw(WGPURenderPassEncoder pass, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t instance)
{
    draws++;
    __real_wgpuRenderPassEncoderDraw(pass, vertices, instances, first, instance);
}

void __wrap_wgpuQueueSubmit(WGPUQueue queue, size_t count, WGPUCommandBuffer const *commands)
{
    submissions++;
    __real_wgpuQueueSubmit(queue, count, commands);
}

int main(void)
{
    doe_runtime_options options = {.adapter = DOE_ADAPTER_SOFTWARE, .upload_submit_every = 1};
    char message[256];
    doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
    if (!runtime) fprintf(stderr, "%s\n", message);
    assert(runtime);
    doe_command command = {.kind = DOE_COMMAND_ASYNC_DIAGNOSTICS, .label = "async_diagnostics",
        .as.diagnostics = {.mode = DOE_DIAG_FULL, .format = DOE_FORMAT_RGBA8_UNORM, .iterations = 2, .emulate = true}};
    doe_trace_execution execution;
    bool ok = doe_runtime_execute(runtime, &command, &execution);
    if (!ok) fprintf(stderr, "%s: %s\n", execution.status_code, execution.message);
    assert(ok);
    assert(compilations == 6 && pipelines == 6 && finishes == 6 && draws == 4 && submissions == 0);
    assert(execution.submit_count == 0 && execution.dispatch_count == 0);
    command.as.diagnostics.mode = DOE_DIAG_RESOURCE_TABLE;
    command.as.diagnostics.emulate = false;
    assert(!doe_runtime_execute(runtime, &command, &execution));
    assert(!strcmp(execution.status_code, "diagnostic_extension_unavailable"));
    assert(compilations == 6 && pipelines == 6 && finishes == 6 && draws == 4 && submissions == 0);
    const doe_runtime_adapter *adapter = doe_runtime_adapter_info(runtime);
    printf("PASS diagnostic real ABI calls: 6 async pipelines, 6 compilation callbacks, 6 recorded buffers, 4 draws, zero submits: %s (%s)\n",
        adapter->device, adapter->adapter_type);
    doe_runtime_close(runtime);
    return 0;
}
