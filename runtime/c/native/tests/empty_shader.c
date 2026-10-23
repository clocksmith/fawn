/* SPDX-License-Identifier: MIT
 * Genuine empty compiler artifact: module succeeds, pipeline entry selection fails.
 * Preserves the earlier failing public reproduction without a synthetic binary. */
#include "consumer.h"
static void empty_info(WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUCompilationInfoRequestStatus_Success && info && !info->messageCount);
    ++*(unsigned *)userdata;
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,NULL,requested).id && s.device);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"",0};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s.device,&descriptor);
    assert(shader);
    unsigned callbacks = 0;
    WGPUCompilationInfoCallbackInfo info = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=empty_info,.userdata1=&callbacks};
    assert(wgpuShaderModuleGetCompilationInfo(shader,info).id && callbacks == 1);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module = shader;
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(s.device,&pipeline_desc));
    expect_validation(&s);
    wgpuShaderModuleRelease(shader);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS valid empty WGSL has no compilation diagnostics; missing pipeline entry rejects %s\n", adapter_class_scope());
}
