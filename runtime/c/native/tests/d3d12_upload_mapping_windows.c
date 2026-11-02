/* SPDX-License-Identifier: MIT
 * Public WARP mapping/upload consumer; private typed metadata only verifies heap
 * placement and lease identity. Pixel/buffer results come from real GPU copies. */
#include "consumer.h"
#include "../src/d3d12_frontend.h"
#include "../../resources/src/internal.h"
#include "../../backends/d3d12/src/internal.h"
#include "../../backends/d3d12/src/windows_internal.h"
static native_buffer *payload(WGPUBuffer handle)
{ return ((doe_resource_buffer *)handle)->base.native.handle; }
static void copy_to(state *s,WGPUBuffer source,WGPUBuffer destination,uint64_t size)
{
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,source,0,destination,0,size); submit(s,encoder);
}
static void write_reply(WGPUMapAsyncStatus status,WGPUStringView message,void *data,void *unused)
{
    (void)message; (void)unused; *(WGPUMapAsyncStatus *)data=status;
}
static void writable_mapping(state *s)
{
    WGPUBufferDescriptor desc={.size=32,.usage=WGPUBufferUsage_MapWrite|WGPUBufferUsage_CopySrc,.mappedAtCreation=WGPU_TRUE};
    WGPUBuffer source=wgpuDeviceCreateBuffer(s->device,&desc); assert(source);
    native_buffer *native=payload(source);
    assert(native->handle.d3d12->memory==DOE_DX_UPLOAD && !native->mapping);
    assert(native->handle.d3d12->allocation->lent_write);
    uint32_t *words=wgpuBufferGetMappedRange(source,0,32); assert(words);
    for (unsigned i=0;i<8;++i) { assert(!words[i]); words[i]=i+11; }
    wgpuBufferUnmap(source); assert(!native->handle.d3d12->allocation->lent);
    WGPUBuffer readback=buffer(s,32,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    native_device *device=(native_device *)s->device;
    doe_dx_native_context *context=device->backend.value.d3d12.context->native;
    ID3D12Fence *gate=NULL;
    assert(ID3D12Device_CreateFence(context->device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&gate)==S_OK);
    assert(ID3D12CommandQueue_Wait(context->queue,gate,1)==S_OK);
    copy_to(s,source,readback,32);
    WGPUMapAsyncStatus status=WGPUMapAsyncStatus_Error;
    WGPUBufferMapCallbackInfo callback={.mode=WGPUCallbackMode_WaitAnyOnly,.callback=write_reply,.userdata1=&status};
    WGPUFuture future=wgpuBufferMapAsync(source,WGPUMapMode_Write,8,8,callback);
    assert(wgpuBufferGetMapState(source)==WGPUBufferMapState_Pending);
    wgpuBufferUnmap(source);
    assert(wgpuBufferGetMapState(source)==WGPUBufferMapState_Unmapped && !native->handle.d3d12->allocation->lent);
    assert(ID3D12Fence_Signal(gate,1)==S_OK);
    wait_future(s,future); assert(status==WGPUMapAsyncStatus_Aborted);
    work_done(s); ID3D12Fence_Release(gate);
    future=wgpuBufferMapAsync(source,WGPUMapMode_Write,8,8,callback);
    wait_future(s,future); assert(status==WGPUMapAsyncStatus_Success);
    assert(!native->mapping && native->handle.d3d12->allocation->lent_write &&
        native->handle.d3d12->allocation->lend_offset==8 && native->handle.d3d12->allocation->lend_size==8);
    uint32_t *part=wgpuBufferGetMappedRange(source,8,8); assert(part); part[0]=101; part[1]=102;
    wgpuBufferUnmap(source);
    const uint32_t *copied=map(s,readback,32);
    for (unsigned i=0;i<8;++i) assert(copied[i]==i+11);
    wgpuBufferUnmap(readback); copy_to(s,source,readback,32); copied=map(s,readback,32);
    for (unsigned i=0;i<8;++i) assert(copied[i]==(i==2 ? 101u : i==3 ? 102u : i+11));
    wgpuBufferUnmap(readback); wgpuBufferRelease(source); wgpuBufferRelease(readback);
    /* mappedAtCreation alone does not turn an actual storage/UAV into UPLOAD. */
    desc=(WGPUBufferDescriptor){.size=32,.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc,.mappedAtCreation=WGPU_TRUE};
    source=wgpuDeviceCreateBuffer(s->device,&desc); assert(source);
    native=payload(source); assert(native->handle.d3d12->memory==DOE_DX_DEFAULT && native->mapping);
    words=wgpuBufferGetMappedRange(source,0,32); assert(words);
    for (unsigned i=0;i<8;++i) { assert(!words[i]); words[i]=201+i; }
    wgpuBufferUnmap(source); readback=buffer(s,32,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    copy_to(s,source,readback,32); copied=map(s,readback,32);
    for (unsigned i=0;i<8;++i) assert(copied[i]==201+i);
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback); wgpuBufferRelease(source);
    printf("PASS direct MapWrite UPLOAD: zeroed mutable mapped-at-creation, exact deferred MapAsync span/cancellation, GPU copy snapshots, DEFAULT storage shadow preserved %s\n",adapter_class_scope());
}
static void sliced_writes(state *s)
{
    native_device *device=(native_device *)s->device;
    doe_dx_context *context=device->backend.value.d3d12.context;
    WGPUBuffer source=buffer(s,128,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer readback=buffer(s,128,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    uint64_t before=context->next_fence;
    uint32_t value=0;
    for (unsigned i=0;i<20;++i) {
        value=i+1; wgpuQueueWriteBuffer(s->queue,source,4u*i,&value,4); value=UINT32_MAX;
    }
    value=777; wgpuQueueWriteBuffer(s->queue,source,4,&value,4); value=UINT32_MAX;
    /* Only the initial deferred zero fill submitted; repeated writes ordered
     * inside the same open command instead of forcing 20 extra submissions. */
    assert(context->next_fence==before+1);
    doe_dx_buffer *page=context->upload_staging;
    assert(page && page->upload_slices && page->upload_used>=84 && page->native->upload_mapping);
    copy_to(s,source,readback,128);
    const uint32_t *copied=map(s,readback,128);
    for (unsigned i=0;i<32;++i) assert(copied[i]==(i==1 ? 777u : i<20 ? i+1 : 0u));
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback); wgpuBufferRelease(source);
    printf("PASS upload slices: 21 caller snapshots/overlaps on one persistent page, one initial zero submission then one adopted upload+copy submission, exact bytes/padding %s\n",adapter_class_scope());
}
int main(int argc,char **argv)
{
    if (argc!=2 || strcmp(argv[1],"--warp")) { fputs("usage: d3d12_upload_mapping_windows.exe --warp\n",stderr); return 2; }
    assert(_putenv_s("DOE_C_TEST_BACKEND","d3d12")==0 && _putenv_s("DOE_C_TEST_ADAPTER","software")==0);
    consumer_line_output(); state s={0}; consumer_init(&s);
    writable_mapping(&s); sliced_writes(&s); work_done(&s); consumer_finish(&s);
    printf("PASS public D3D12 mapping/upload consumer %s\n",adapter_class_scope()); return 0;
}
