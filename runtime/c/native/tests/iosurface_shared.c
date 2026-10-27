/* SPDX-License-Identifier: MIT
 * Actual packed IOSurface producer bytes, native queue and newly signaled event.
 * Reuse the committed actual IOSurface fixture, not a synthetic surface. */
#include "consumer.h"
#include "doe_native_shared_memory.h"
#include "iosurface_fixture.h"
int main(void)
{
    const WGPUFeatureName features[]={(WGPUFeatureName)DOE_SHARED_FEATURE_IOSURFACE,
        (WGPUFeatureName)DOE_SHARED_FEATURE_MTL_EVENT};
    state s={0};consumer_init_features(&s,features,2);
    for(size_t i=0;i<2;++i)assert(wgpuAdapterHasFeature(s.adapter,features[i]) && wgpuDeviceHasFeature(s.device,features[i]));
    IOSurfaceRef surface=doe_test_iosurface_create(UINT32_C(0x42475241));assert(surface);
    assert(IOSurfaceLock(surface,0,NULL)==0);
    unsigned char *pixels=IOSurfaceGetBaseAddress(surface);size_t stride=IOSurfaceGetBytesPerRow(surface);
    assert(pixels && stride>=8);
    const unsigned char red[8]={0,0,255,255,0,0,255,255},green[8]={0,255,0,255,0,255,0,255};
    memcpy(pixels,red,8);memcpy(pixels+stride,green,8);
    assert(IOSurfaceUnlock(surface,0,NULL)==0);
    WGPUSharedTextureMemoryIOSurfaceDescriptor native={.chain={.sType=DOE_SHARED_STYPE_IOSURFACE},.ioSurface=surface};
    WGPUSharedTextureMemoryDescriptor import={.nextInChain=&native.chain};
    WGPUSharedTextureMemory memory=wgpuDeviceImportSharedTextureMemory(s.device,&import);assert(memory);CFRelease(surface);
    WGPUSharedTextureMemoryProperties properties={0};assert(wgpuSharedTextureMemoryGetProperties(memory,&properties)==WGPUStatus_Success);
    assert(properties.format==WGPUTextureFormat_BGRA8Unorm && properties.size.width==2 && properties.size.height==2);
    WGPUTextureDescriptor td={.dimension=WGPUTextureDimension_2D,.format=properties.format,.size=properties.size,.mipLevelCount=1,.sampleCount=1,.usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_TextureBinding};
    WGPUTexture texture=wgpuSharedTextureMemoryCreateTexture(memory,&td);assert(texture);
    WGPUSharedTextureMemoryBeginAccessDescriptor begin={.initialized=WGPU_TRUE};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,texture,&begin)==WGPUStatus_Success);
    puts("CHECKPOINT imported real producer surface and began access");fflush(stdout);
    WGPUBufferDescriptor bd={.size=512,.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead};
    WGPUBuffer readback=wgpuDeviceCreateBuffer(s.device,&bd);assert(readback);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s.device,NULL);assert(encoder);
    WGPUTexelCopyTextureInfo source={.texture=texture,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferInfo destination={.buffer=readback,.layout={.bytesPerRow=256,.rowsPerImage=2}};
    WGPUExtent3D extent={2,2,1};wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
    WGPUCommandBuffer command=wgpuCommandEncoderFinish(encoder,NULL);assert(command);wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s.queue,1,&command);wgpuCommandBufferRelease(command);
    puts("CHECKPOINT submitted original imported texture readback");fflush(stdout);
    WGPUSharedTextureMemoryEndAccessState end={0};assert(wgpuSharedTextureMemoryEndAccess(memory,texture,&end)==WGPUStatus_Success);
    assert(end.fenceCount==1 && end.signaledValueCount==1 && end.signaledValues[0]==1 && end.fences[0]);
    WGPUSharedFenceMTLSharedEventExportInfo event={.chain={.sType=DOE_SHARED_STYPE_MTL_EVENT_EXPORT}};
    WGPUSharedFenceExportInfo exported={.nextInChain=&event.chain};wgpuSharedFenceExportInfo(end.fences[0],&exported);
    assert(exported.type==DOE_SHARED_FENCE_MTL_EVENT && event.sharedEvent);
    wgpuTextureRelease(texture);wgpuSharedTextureMemoryRelease(memory);
    work_done(&s);
    WGPUBufferMapCallbackInfo callback={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=mapped,.userdata1=&s};
    assert(wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,512,callback).id && s.mapped==1);
    const unsigned char *bytes=wgpuBufferGetConstMappedRange(readback,0,512);assert(bytes);
    assert(!memcmp(bytes,red,8) && !memcmp(bytes+256,green,8));
    wgpuBufferUnmap(readback);wgpuBufferRelease(readback);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    consumer_finish(&s);
    printf("PASS actual IOSurface producer red/green BGRA pixels, Begin/End, queue readback, fresh exported MTLSharedEvent signal and early native/public releases %s\n",adapter_class_scope());
    return 0;
}
