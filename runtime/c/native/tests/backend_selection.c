/* SPDX-License-Identifier: MIT */
#include "consumer.h"

static void unavailable(WGPURequestAdapterStatus status, WGPUAdapter adapter,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPURequestAdapterStatus_Unavailable && !adapter);
    ++*(unsigned *)userdata;
}

int main(void)
{
    WGPUInstance instance = wgpuCreateInstance(NULL);
    unsigned replies = 0;
    WGPURequestAdapterCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=unavailable,.userdata1=&replies};
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_D3D12;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    options.backendType = WGPUBackendType_Metal;
    options.forceFallbackAdapter = WGPU_TRUE;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
#if !defined(__APPLE__)
    options.forceFallbackAdapter = WGPU_FALSE;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(replies == 3);
#else
    assert(replies == 2);
#endif
    wgpuInstanceRelease(instance);
    puts("PASS explicit backend selection: unsupported backend and Metal CPU request do not fall back; host loader absence remains unavailable");
}
