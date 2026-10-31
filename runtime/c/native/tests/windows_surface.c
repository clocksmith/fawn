/* SPDX-License-Identifier: MIT
 * Real HWND / D3D12 public consumer, built but never run by the SDK producer.
 * Default adapter-class control explicitly requests WARP; physical is opt-in. */
#include "surface_consumer.h"
#include "../../runtime-cli/src/surface_platform.h"
int main(void)
{
    consumer_line_output();
    if(!getenv("DOE_C_TEST_BACKEND"))assert(_putenv_s("DOE_C_TEST_BACKEND","d3d12")==0);
    assert(adapter_class_backend()==WGPUBackendType_D3D12);
    state s={.instance=wgpuCreateInstance(NULL)};
    doe_surface_host *host=NULL;WGPUSurface surface=NULL;char message[512]={0};
    if(!surface_host_create(s.instance,&host,&surface,message,sizeof(message))){
        fprintf(stderr,"SKIP HWND host unavailable: %s\n",message);wgpuInstanceRelease(s.instance);return 77;}
    request_adapter(&s,surface);describe_adapter(s.adapter);
    WGPUTextureFormat format=choose_format(surface,s.adapter);
    assert(!acquire(surface).texture&&wgpuSurfacePresent(surface)==WGPUStatus_Error);
    configure(&s,surface,format,64,64);
    WGPURenderPipeline pipeline=quadrant_pipeline(&s,format);
    render_frame(&s,surface,format,pipeline,64,64,0,true);
    WGPUSurfaceTexture kept=acquire(surface);assert(kept.texture);
    WGPUTextureView view=wgpuTextureCreateView(kept.texture,NULL);assert(view);
    push_validation(&s);configure(&s,surface,WGPUTextureFormat_RGBA16Float,64,64);expect_validation(&s);
    WGPUSurfaceTexture same=acquire(surface);assert(same.texture==kept.texture);wgpuTextureRelease(same.texture);
    assert(wgpuSurfacePresent(surface)==WGPUStatus_Success);
    /* DXGI forbids ResizeBuffers while ANY backbuffer reference is retained. */
    push_validation(&s);configure(&s,surface,format,48,32);expect_validation(&s);
    wgpuTextureViewRelease(view);wgpuTextureRelease(kept.texture);
    assert(surface_host_resize(host,48,32,message,sizeof(message)));
    configure(&s,surface,format,48,32);render_frame(&s,surface,format,pipeline,48,32,1,true);
    kept=acquire(surface);assert(kept.texture);view=wgpuTextureCreateView(kept.texture,NULL);assert(view);
    wgpuSurfaceUnconfigure(surface);assert(!acquire(surface).texture);
    wgpuSurfaceRelease(surface);wgpuTextureRelease(kept.texture);wgpuTextureViewRelease(view);
    wgpuRenderPipelineRelease(pipeline);surface_host_release(host);
    wgpuQueueRelease(s.queue);wgpuDeviceRelease(s.device);wgpuAdapterRelease(s.adapter);wgpuInstanceRelease(s.instance);
    assert(!s.errors);printf("PASS HWND surface: pixel readback, retained-frame resize rejection, resize and teardown %s\n",
        adapter_class_scope());return 0;
}
