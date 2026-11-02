/* SPDX-License-Identifier: MIT
 * Real public native DLL/WARP consumer. SDK queue waits force the frontend's
 * unlocked EndAccess interval; they do not simulate completion or pixel data. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "consumer.h"
#include "doe_native_shared_memory.h"
#include "../src/internal.h"
#include "../../backends/d3d12/src/internal.h"
#include "../../backends/d3d12/src/windows_internal.h"
#include <stdlib.h>

static ID3D12Device *producer_device(void)
{
    IDXGIFactory4 *factory=NULL; IDXGIAdapter *adapter=NULL; ID3D12Device *device=NULL;
    assert(CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory)==S_OK);
    assert(IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void **)&adapter)==S_OK);
    assert(D3D12CreateDevice((IUnknown *)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void **)&device)==S_OK);
    IDXGIAdapter_Release(adapter); IDXGIFactory4_Release(factory); return device;
}
static HANDLE producer_texture(ID3D12Device *device)
{
    D3D12_HEAP_PROPERTIES heap={0}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc={0}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=desc.Height=2; desc.DepthOrArraySize=desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource *resource=NULL; HANDLE handle=NULL;
    assert(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_SHARED,&desc,
        D3D12_RESOURCE_STATE_COMMON,NULL,&IID_ID3D12Resource,(void **)&resource)==S_OK);
    assert(ID3D12Device_CreateSharedHandle(device,(ID3D12DeviceChild *)resource,NULL,GENERIC_ALL,NULL,&handle)==S_OK);
    ID3D12Resource_Release(resource); return handle;
}
static HANDLE producer_keyed_texture(void)
{
    ID3D11Device *device=NULL; ID3D11DeviceContext *immediate=NULL;
    assert(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,NULL,0,D3D11_SDK_VERSION,&device,NULL,&immediate)==S_OK);
    D3D11_TEXTURE2D_DESC desc={0}; desc.Width=desc.Height=2; desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ID3D11Texture2D *texture=NULL; IDXGIResource1 *resource=NULL; HANDLE handle=NULL;
    assert(ID3D11Device_CreateTexture2D(device,&desc,NULL,&texture)==S_OK);
    assert(ID3D11Texture2D_QueryInterface(texture,&IID_IDXGIResource1,(void **)&resource)==S_OK);
    assert(IDXGIResource1_CreateSharedHandle(resource,NULL,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,NULL,&handle)==S_OK);
    IDXGIResource1_Release(resource); ID3D11Texture2D_Release(texture);
    ID3D11DeviceContext_Release(immediate); ID3D11Device_Release(device); return handle;
}
static WGPUSharedTextureMemory import_memory(state *s,HANDLE handle,bool keyed)
{
    WGPUSharedTextureMemoryDXGISharedHandleDescriptor dxgi={.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_DXGI_MEMORY},
        .handle=handle,.useKeyedMutex=keyed ? WGPU_TRUE : WGPU_FALSE};
    WGPUSharedTextureMemoryDescriptor desc={.nextInChain=&dxgi.chain};
    WGPUSharedTextureMemory memory=wgpuDeviceImportSharedTextureMemory(s->device,&desc);
    assert(memory); CloseHandle(handle); return memory;
}
static WGPUTexture shared_texture(WGPUSharedTextureMemory memory)
{
    WGPUSharedTextureMemoryProperties properties={0};
    assert(wgpuSharedTextureMemoryGetProperties(memory,&properties)==WGPUStatus_Success);
    assert(properties.size.width==2 && properties.size.height==2 && properties.format==WGPUTextureFormat_RGBA8Unorm);
    WGPUTextureDescriptor desc=WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size=properties.size; desc.format=properties.format;
    desc.usage=WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst|WGPUTextureUsage_RenderAttachment;
    assert(desc.dimension==WGPUTextureDimension_Undefined);
    WGPUTexture image=wgpuSharedTextureMemoryCreateTexture(memory,&desc); assert(image);
    assert(desc.dimension==WGPUTextureDimension_Undefined && wgpuTextureGetDimension(image)==WGPUTextureDimension_2D);
    desc.dimension=WGPUTextureDimension_1D; assert(!wgpuSharedTextureMemoryCreateTexture(memory,&desc));
    desc.dimension=WGPUTextureDimension_3D; assert(!wgpuSharedTextureMemoryCreateTexture(memory,&desc));
    desc.dimension=WGPUTextureDimension_2D;
    WGPUTexture explicit_2d=wgpuSharedTextureMemoryCreateTexture(memory,&desc); assert(explicit_2d);
    wgpuTextureRelease(explicit_2d);
    return image;
}
static void wait_external(ID3D12Device *device,HANDLE handle,uint64_t value)
{
    ID3D12Fence *fence=NULL;
    assert(ID3D12Device_OpenSharedHandle(device,handle,&IID_ID3D12Fence,(void **)&fence)==S_OK);
    ULONGLONG start=GetTickCount64();
    while (ID3D12Fence_GetCompletedValue(fence)<value) { assert(GetTickCount64()-start<5000); Sleep(1); }
    assert(ID3D12Fence_GetCompletedValue(fence)!=UINT64_MAX); ID3D12Fence_Release(fence);
}
static void import_copy_and_fence(state *s,ID3D12Device *producer)
{
    WGPUSharedTextureMemory memory=import_memory(s,producer_texture(producer),false);
    WGPUTexture image=shared_texture(memory),alias=shared_texture(memory);
    WGPUSharedTextureMemoryBeginAccessDescriptor begin={.initialized=WGPU_FALSE};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryBeginAccess(memory,alias,&begin)==WGPUStatus_Error);
    WGPUBuffer readback=buffer(s,264,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUTexelCopyTextureInfo from={.texture=image};
    WGPUTexelCopyBufferInfo to={.buffer=readback,.layout={.bytesPerRow=256,.rowsPerImage=2}};
    WGPUExtent3D size={2,2,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&to,&size); submit(s,encoder);
    WGPUSharedTextureMemoryEndAccessState end={0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,alias,&end)==WGPUStatus_Error);
    assert(wgpuSharedTextureMemoryEndAccess(memory,image,&end)==WGPUStatus_Success);
    assert(end.initialized && end.fenceCount==1 && end.signaledValueCount==1);
    WGPUSharedFenceDXGISharedHandleExportInfo dxgi={.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_DXGI_FENCE_EXPORT}};
    WGPUSharedFenceExportInfo info={.nextInChain=&dxgi.chain};
    wgpuSharedFenceExportInfo(end.fences[0],&info); assert(info.type==DOE_SHARED_FENCE_DXGI && dxgi.handle);
    HANDLE independent=NULL;
    assert(DuplicateHandle(GetCurrentProcess(),dxgi.handle,GetCurrentProcess(),&independent,0,FALSE,DUPLICATE_SAME_ACCESS));
    WGPUSharedFenceDXGISharedHandleDescriptor source={.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_DXGI_FENCE_DESCRIPTOR},.handle=independent};
    WGPUSharedFenceDescriptor fence_desc={.nextInChain=&source.chain};
    WGPUSharedFence retained=wgpuDeviceImportSharedFence(s->device,&fence_desc); assert(retained);
    uint64_t value=end.signaledValues[0];
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    wait_external(producer,independent,value); CloseHandle(independent);
    begin=(WGPUSharedTextureMemoryBeginAccessDescriptor){.initialized=WGPU_TRUE,.fenceCount=1,.fences=&retained,
        .signaledValueCount=1,.signaledValues=&value};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Success);
    wgpuSharedFenceRelease(retained);
    end=(WGPUSharedTextureMemoryEndAccessState){0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,image,&end)==WGPUStatus_Success);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
    const unsigned char *pixels=map(s,readback,264);
    for (size_t i=0;i<264;++i) assert(pixels[i]==0);
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
    wgpuTextureDestroy(alias); wgpuTextureRelease(alias); wgpuTextureRelease(image);
    wgpuSharedTextureMemoryRelease(memory);
    printf("PASS public DXGI Import/Create/Begin/queue-copy/End, actual zero readback, alias rejection, independent exported/imported fence lifetime %s\n",adapter_class_scope());
}
static void queue_waits_and_cached_export(state *s,ID3D12Device *producer)
{
    WGPUSharedTextureMemory memory=import_memory(s,producer_texture(producer),false);
    WGPUTexture image=shared_texture(memory);
    ID3D12Fence *source=NULL; HANDLE handle=NULL;
    assert(ID3D12Device_CreateFence(producer,0,D3D12_FENCE_FLAG_SHARED,&IID_ID3D12Fence,(void **)&source)==S_OK);
    assert(ID3D12Device_CreateSharedHandle(producer,(ID3D12DeviceChild *)source,NULL,GENERIC_ALL,NULL,&handle)==S_OK);
    state foreign={0};
    WGPUFeatureName features[]={(WGPUFeatureName)DOE_SHARED_FEATURE_DXGI_MEMORY,(WGPUFeatureName)DOE_SHARED_FEATURE_DXGI_FENCE};
    consumer_init_features(&foreign,features,2);
    WGPUSharedFenceDXGISharedHandleDescriptor dxgi={.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_DXGI_FENCE_DESCRIPTOR},.handle=handle};
    WGPUSharedFenceDescriptor desc={.nextInChain=&dxgi.chain};
    WGPUSharedFence fence=wgpuDeviceImportSharedFence(foreign.device,&desc); assert(fence); CloseHandle(handle);
    uint64_t value=7;
    WGPUSharedTextureMemoryBeginAccessDescriptor begin={.initialized=WGPU_TRUE,.fenceCount=1,.fences=&fence,
        .signaledValueCount=1,.signaledValues=&value};
    /* The producer remains unsignaled throughout Begin and both Ends. A CPU
     * producer wait could not return success here. No consumer GPU work is needed
     * to preserve the incoming dependency in the outgoing timeline point. */
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Success);
    wgpuSharedFenceRelease(fence); consumer_finish(&foreign);
    WGPUSharedTextureMemoryEndAccessState first={0},second={0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,image,&first)==WGPUStatus_Success && first.signaledValues[0]);
    WGPUSharedFenceDXGISharedHandleExportInfo one={.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_DXGI_FENCE_EXPORT}};
    WGPUSharedFenceExportInfo export_one={.nextInChain=&one.chain}; wgpuSharedFenceExportInfo(first.fences[0],&export_one);
    ID3D12Fence *outgoing=NULL;
    assert(ID3D12Device_OpenSharedHandle(producer,one.handle,&IID_ID3D12Fence,(void **)&outgoing)==S_OK);
    assert(ID3D12Fence_GetCompletedValue(outgoing)<first.signaledValues[0]);
    begin=(WGPUSharedTextureMemoryBeginAccessDescriptor){.initialized=WGPU_TRUE};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Success);
    assert(wgpuSharedTextureMemoryEndAccess(memory,image,&second)==WGPUStatus_Success);
    WGPUSharedFenceDXGISharedHandleExportInfo two={.chain={.sType=(WGPUSType)DOE_SHARED_STYPE_DXGI_FENCE_EXPORT}};
    WGPUSharedFenceExportInfo export_two={.nextInChain=&two.chain}; wgpuSharedFenceExportInfo(second.fences[0],&export_two);
    assert(one.handle==two.handle && second.signaledValues[0]>=first.signaledValues[0]);
    assert(ID3D12Fence_Signal(source,7)==S_OK); ID3D12Fence_Release(source);
    wait_external(producer,two.handle,second.signaledValues[0]); ID3D12Fence_Release(outgoing);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(first); wgpuSharedTextureMemoryEndAccessStateFreeMembers(second);
    wgpuTextureRelease(image); wgpuSharedTextureMemoryRelease(memory); work_done(s);
    printf("PASS imported GPU queue wait: unsignaled producer never CPU-blocks Begin/End, zero-work End forwards wait, foreign fence/device release safe, one cached timeline export handle %s\n",adapter_class_scope());
}
typedef struct end_call {
    WGPUSharedTextureMemory memory; WGPUTexture texture;
    WGPUSharedTextureMemoryEndAccessState output;
    WGPUStatus result; HANDLE finished;
} end_call;
static DWORD WINAPI end_thread(void *data)
{
    end_call *call=data;
    call->result=wgpuSharedTextureMemoryEndAccess(call->memory,call->texture,&call->output);
    SetEvent(call->finished); return 0;
}
static void canary(const end_call *call)
{
    assert(call->output.initialized==63 && call->output.fenceCount==71 && !call->output.fences &&
        call->output.signaledValueCount==81 && !call->output.signaledValues);
}
static void concurrent_end(state *s,bool destroy_during_wait)
{
    WGPUSharedTextureMemory memory=import_memory(s,producer_keyed_texture(),true);
    WGPUTexture image=shared_texture(memory);
    /* White-box fault coordination only: use the retained canonical context's
     * real SDK queue to park its GPU work. All shared access calls are public. */
    native_device *native=(native_device *)s->device;
    doe_dx_native_context *context=native->backend.value.d3d12.context->native;
    ID3D12Fence *gate=NULL;
    assert(ID3D12Device_CreateFence(context->device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&gate)==S_OK);
    assert(ID3D12CommandQueue_Wait(context->queue,gate,1)==S_OK);
    WGPUSharedTextureMemoryBeginAccessDescriptor begin={.initialized=WGPU_FALSE};
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Success);
    end_call calls[2]={0}; HANDLE threads[2],events[2];
    for (unsigned i=0;i<2;++i) {
        calls[i]=(end_call){.memory=memory,.texture=image,.output={.initialized=63,.fenceCount=71,.signaledValueCount=81},
            .finished=CreateEventW(NULL,TRUE,FALSE,NULL)};
        assert(calls[i].finished); events[i]=calls[i].finished;
        threads[i]=CreateThread(NULL,0,end_thread,&calls[i],0,NULL); assert(threads[i]);
    }
    DWORD first=WaitForMultipleObjects(2,events,FALSE,2000);
    assert(first==WAIT_OBJECT_0 || first==WAIT_OBJECT_0+1);
    unsigned rejected=(unsigned)(first-WAIT_OBJECT_0),waiting=1u-rejected;
    assert(calls[rejected].result==WGPUStatus_Error); canary(&calls[rejected]);
    assert(WaitForSingleObject(events[waiting],0)==WAIT_TIMEOUT);
    /* The end reservation blocks a fresh access while its owner wait unlocks. */
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Error);
    if (destroy_during_wait) wgpuTextureDestroy(image);
    assert(ID3D12Fence_Signal(gate,1)==S_OK);
    assert(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
    if (destroy_during_wait) { assert(calls[waiting].result==WGPUStatus_Error); canary(&calls[waiting]); }
    else {
        assert(calls[waiting].result==WGPUStatus_Success && calls[waiting].output.fenceCount==1);
        wgpuSharedTextureMemoryEndAccessStateFreeMembers(calls[waiting].output);
    }
    for (unsigned i=0;i<2;++i) { CloseHandle(threads[i]); CloseHandle(events[i]); }
    ID3D12Fence_Release(gate); wgpuTextureRelease(image);
    /* Destruction rejected the pending End without forgetting the keyed lease;
     * canonical finalization releases it, so the memory can be accessed again. */
    image=shared_texture(memory); begin.initialized=WGPU_TRUE;
    assert(wgpuSharedTextureMemoryBeginAccess(memory,image,&begin)==WGPUStatus_Success);
    WGPUSharedTextureMemoryEndAccessState end={0};
    assert(wgpuSharedTextureMemoryEndAccess(memory,image,&end)==WGPUStatus_Success);
    wgpuSharedTextureMemoryEndAccessStateFreeMembers(end); wgpuTextureRelease(image);
    wgpuSharedTextureMemoryRelease(memory);
    printf("PASS public keyed End reservation: second End/Begin reject, output canaries unchanged, %s, keyed reacquire %s\n",
        destroy_during_wait ? "destroy-during-wait rejects" : "one eventual End succeeds",adapter_class_scope());
}
int main(int argc,char **argv)
{
    if (argc!=2 || strcmp(argv[1],"--warp")) { fputs("usage: d3d12_shared_memory_windows.exe --warp\n",stderr); return 2; }
    assert(_putenv_s("DOE_C_TEST_BACKEND","d3d12")==0 && _putenv_s("DOE_C_TEST_ADAPTER","software")==0);
    consumer_line_output(); state s={0};
    WGPUFeatureName features[]={(WGPUFeatureName)DOE_SHARED_FEATURE_DXGI_MEMORY,(WGPUFeatureName)DOE_SHARED_FEATURE_DXGI_FENCE};
    consumer_init_features(&s,features,2);
    ID3D12Device *producer=producer_device();
    import_copy_and_fence(&s,producer); queue_waits_and_cached_export(&s,producer); concurrent_end(&s,false); concurrent_end(&s,true);
    work_done(&s); ID3D12Device_Release(producer); consumer_finish(&s);
    printf("PASS public native DLL shared-memory WARP consumer %s\n",adapter_class_scope()); return 0;
}
