/* SPDX-License-Identifier: MIT
 * Bounded real Windows WARP shared-handle transfer/fence fixture. CPU software
 * execution only; no shader, presentation or physical-hardware qualification. */
#include "../src/internal.h"
#include "../src/windows_internal.h"
#include "doe_d3d12_shared.h"
#include <assert.h>
#include <stdio.h>
static void check(doe_dx_status status,const doe_dx_error *error)
{
    if (status!=DOE_DX_OK) {
        fprintf(stderr,"status=%d native=%ld operation=%s\n",(int)status,(long)error->native_code,
            error->operation ? error->operation : "none");
        abort();
    }
}
static HANDLE producer(doe_dx_context *context)
{
    D3D12_HEAP_PROPERTIES heap={0}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask=1; heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc={0}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=2; desc.Height=2; desc.DepthOrArraySize=1; desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource *resource=NULL; HANDLE handle=NULL;
    HRESULT hr=ID3D12Device_CreateCommittedResource(context->native->device,&heap,D3D12_HEAP_FLAG_SHARED,
        &desc,D3D12_RESOURCE_STATE_COMMON,NULL,&IID_ID3D12Resource,(void **)&resource);
    assert(hr==S_OK);
    hr=ID3D12Device_CreateSharedHandle(context->native->device,(ID3D12DeviceChild *)resource,NULL,GENERIC_ALL,NULL,&handle);
    assert(hr==S_OK); ID3D12Resource_Release(resource); return handle;
}
static HANDLE keyed_producer(void)
{
    ID3D11Device *device=NULL; ID3D11DeviceContext *immediate=NULL;
    HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,NULL,0,D3D11_SDK_VERSION,&device,NULL,&immediate);
    assert(hr==S_OK);
    D3D11_TEXTURE2D_DESC desc={0}; desc.Width=2; desc.Height=2;
    desc.MipLevels=1; desc.ArraySize=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ID3D11Texture2D *texture=NULL;
    hr=ID3D11Device_CreateTexture2D(device,&desc,NULL,&texture); assert(hr==S_OK);
    IDXGIResource1 *resource=NULL;
    hr=ID3D11Texture2D_QueryInterface(texture,&IID_IDXGIResource1,(void **)&resource); assert(hr==S_OK);
    HANDLE handle=NULL;
    hr=IDXGIResource1_CreateSharedHandle(resource,NULL,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,NULL,&handle);
    assert(hr==S_OK);
    IDXGIResource1_Release(resource); ID3D11Texture2D_Release(texture);
    ID3D11DeviceContext_Release(immediate); ID3D11Device_Release(device); return handle;
}
int main(int argc,char **argv)
{
    if (argc!=2 || strcmp(argv[1],"--warp")) { fputs("usage: native_shared.exe --warp\n",stderr); return 2; }
    doe_dx_options options={.adapter_selection=DOE_DX_SELECT_WARP,.allow_software_adapter=true};
    doe_dx_context *context=NULL; doe_dx_error error={0};
    check(doe_dx_context_create(&options,&context,&error),&error);
    doe_dx_caps caps; check(doe_dx_context_caps(context,&caps),&error);
    assert(caps.software_adapter && caps.shared_texture_import && caps.shared_fence_import);
    printf("adapter=%s software=1; scope=WARP shared import/zero/readback/fence\n",caps.name);
    HANDLE handle=producer(context);
    doe_dx_texture *texture=NULL;
    check(doe_dx_shared_texture_import(context,handle,false,&texture,&error),&error);
    CloseHandle(handle); /* Imported resource owns its lease after caller close. */
    doe_dx_shared_fence *wrong=NULL;
    handle=producer(context);
    assert(doe_dx_shared_fence_import(context,handle,&wrong,&error)!=DOE_DX_OK && !wrong);
    CloseHandle(handle);
    ID3D12Fence *producer_fence=NULL;
    HRESULT hr=ID3D12Device_CreateFence(context->native->device,0,D3D12_FENCE_FLAG_SHARED,
        &IID_ID3D12Fence,(void **)&producer_fence); assert(hr==S_OK);
    handle=NULL;
    hr=ID3D12Device_CreateSharedHandle(context->native->device,(ID3D12DeviceChild *)producer_fence,NULL,GENERIC_ALL,NULL,&handle);
    assert(hr==S_OK);
    doe_dx_shared_fence *wait=NULL;
    check(doe_dx_shared_fence_import(context,handle,&wait,&error),&error); CloseHandle(handle);
    assert(doe_dx_shared_fence_wait(wait,7,0,&error)==DOE_DX_PENDING);
    assert(doe_dx_shared_fence_wait(wait,UINT64_MAX,0,&error)==DOE_DX_INVALID_ARGUMENT);
    hr=ID3D12Fence_Signal(producer_fence,7); assert(hr==S_OK); ID3D12Fence_Release(producer_fence);
    check(doe_dx_shared_fence_wait(wait,7,1000,&error),&error);
    doe_dx_shared_fence_release(wait);
    doe_dx_texture_footprint footprint;
    check(doe_dx_texture_copy_footprint(texture,(doe_dx_texture_layout){0},&footprint),&error);
    assert(footprint.span==264 && footprint.row_bytes==8 && footprint.row_pitch==256);
    doe_dx_buffer *readback=NULL;
    check(doe_dx_buffer_create_readback_zeroed(context,footprint.span,&readback,&error),&error);
    doe_dx_commands *commands=NULL;
    check(doe_dx_commands_create(context,&commands,&error),&error);
    assert(doe_dx_commands_initialize_texture(commands,texture,&error)==DOE_DX_INVALID_STATE);
    check(doe_dx_shared_texture_acquire(texture,&error),&error);
    check(doe_dx_shared_texture_begin(texture,false,false,&error),&error);
    check(doe_dx_commands_initialize_texture(commands,texture,&error),&error);
    check(doe_dx_commands_read_texture(commands,texture,readback,(doe_dx_texture_layout){0},&error),&error);
    doe_dx_shared_fence *release=NULL; uint64_t value=UINT64_MAX; bool initialized=false;
    assert(doe_dx_shared_texture_release_point(texture,&release,&value,&initialized,&error)==DOE_DX_BUSY);
    assert(!release && value==UINT64_MAX && !initialized);
    check(doe_dx_commands_submit(commands,&error),&error);
    check(doe_dx_shared_texture_release_point(texture,&release,&value,&initialized,&error),&error);
    assert(value && initialized && doe_dx_shared_fence_handle(release));
    check(doe_dx_shared_texture_end(texture,&error),&error);
    doe_dx_commands_release(commands); commands=NULL;
    check(doe_dx_commands_create(context,&commands,&error),&error);
    assert(doe_dx_commands_initialize_texture(commands,texture,&error)==DOE_DX_INVALID_STATE);
    doe_dx_commands_release(commands);
    check(doe_dx_shared_fence_wait(release,value,5000,&error),&error);
    check(doe_dx_context_poll(context,0,&error),&error);
    unsigned char bytes[264]; memset(bytes,0xCD,sizeof(bytes));
    check(doe_dx_buffer_read(readback,0,bytes,sizeof(bytes),&error),&error);
    for (size_t i=0;i<sizeof(bytes);++i) assert(bytes[i]==0);
    doe_dx_shared_fence *duplicate=NULL;
    check(doe_dx_shared_fence_import(context,doe_dx_shared_fence_handle(release),&duplicate,&error),&error);
    doe_dx_shared_fence_release(release);
    check(doe_dx_shared_fence_wait(duplicate,value,0,&error),&error);
    doe_dx_shared_fence_release(duplicate);
    doe_dx_buffer_release(readback); doe_dx_texture_release(texture);
    handle=keyed_producer(); texture=NULL;
    check(doe_dx_shared_texture_import(context,handle,true,&texture,&error),&error); CloseHandle(handle);
    assert(doe_dx_shared_texture_keyed(texture));
    check(doe_dx_shared_texture_acquire(texture,&error),&error);
    check(doe_dx_shared_texture_begin(texture,false,false,&error),&error);
    commands=NULL; check(doe_dx_commands_create(context,&commands,&error),&error);
    check(doe_dx_commands_initialize_texture(commands,texture,&error),&error);
    check(doe_dx_commands_submit(commands,&error),&error);
    assert(doe_dx_shared_texture_end(texture,&error)==DOE_DX_BUSY);
    doe_dx_commands_release(commands);
    check(doe_dx_context_poll(context,5000,&error),&error);
    check(doe_dx_shared_texture_end(texture,&error),&error);
    check(doe_dx_shared_texture_acquire(texture,&error),&error);
    check(doe_dx_shared_texture_begin(texture,true,false,&error),&error);
    check(doe_dx_shared_texture_end(texture,&error),&error);
    doe_dx_texture_release(texture);
    check(doe_dx_context_destroy(context),&error);
    puts("PASS WARP: borrowed-handle close, wrong-type rejection, timeout/retry, inactive access, unresolved export, queued zero/readback, retained exported fence, D3D11On12 keyed acquire/retire/release/reacquire; no physical claim");
    return 0;
}
