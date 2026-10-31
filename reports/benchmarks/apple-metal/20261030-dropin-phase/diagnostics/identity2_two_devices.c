/* DIAGNOSTIC ONLY: two devices of one Metal adapter each build a compute pipeline. */
#include "webgpu.h"
#include <stdio.h>
#include <string.h>
static WGPUAdapter adapter; static WGPUDevice device;
static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) { (void)s;(void)m;(void)u1;(void)u2; adapter = a; }
static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void *u1, void *u2) { (void)s;(void)m;(void)u1;(void)u2; device = d; }
static const char *src = "@group(0) @binding(0) var<storage,read_write> b:array<u32>; @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) i:vec3<u32>){ if(i.x<arrayLength(&b)){b[i.x]=b[i.x]+1u;} }";
int main(void) {
    WGPUInstance instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions o = WGPU_REQUEST_ADAPTER_OPTIONS_INIT; o.backendType = WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT; ac.mode = WGPUCallbackMode_AllowSpontaneous; ac.callback = on_adapter;
    wgpuInstanceRequestAdapter(instance, &o, ac);
    if (!adapter) return 2;
    WGPUDevice devices[2];
    for (int k = 0; k < 2; ++k) {
        WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT; dc.mode = WGPUCallbackMode_AllowSpontaneous; dc.callback = on_device;
        device = NULL; wgpuAdapterRequestDevice(adapter, NULL, dc); if (!device) return 3; devices[k] = device;
        WGPUShaderSourceWGSL w = WGPU_SHADER_SOURCE_WGSL_INIT; w.code = (WGPUStringView){src, strlen(src)};
        WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT; md.nextInChain = &w.chain;
        WGPUShaderModule m = wgpuDeviceCreateShaderModule(device, &md);
        WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT; pd.compute.module = m; pd.compute.entryPoint = (WGPUStringView){"main", 4};
        WGPUComputePipeline p = wgpuDeviceCreateComputePipeline(device, &pd);
        printf("device %d pipeline %s\n", k, p ? "ok" : "failed");
        wgpuComputePipelineRelease(p); wgpuShaderModuleRelease(m);
    }
    for (int k = 0; k < 2; ++k) wgpuDeviceRelease(devices[k]);
    wgpuAdapterRelease(adapter); wgpuInstanceRelease(instance);
    puts("released");
    return 0;
}
