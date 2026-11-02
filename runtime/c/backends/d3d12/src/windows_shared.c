/* SPDX-License-Identifier: MIT
 * DXGI imports use official SDK interfaces and Dawn 758e5e6a's admitted shape.
 * Resource and queue retirement remain in the existing texture/command owners. */
#include "internal.h"
#include "windows_internal.h"
#include "../include/doe_d3d12_shared.h"
#ifdef _WIN32
struct doe_dx_shared_fence {
    doe_dx_context *context;
    ID3D12Fence *fence;
    HANDLE handle;
};
static doe_dx_status wrap_fence(doe_dx_context *context,ID3D12Fence *native,
    doe_dx_shared_fence **out,doe_dx_error *error)
{
    if (context->objects==SIZE_MAX) { ID3D12Fence_Release(native); return DOE_DX_RANGE; }
    doe_dx_shared_fence *fence=calloc(1,sizeof(*fence));
    if (!fence) { ID3D12Fence_Release(native); return DOE_DX_OUT_OF_MEMORY; }
    HRESULT hr=ID3D12Device_CreateSharedHandle(context->native->device,(ID3D12DeviceChild *)native,
        NULL,GENERIC_ALL,NULL,&fence->handle);
    if (FAILED(hr)) { ID3D12Fence_Release(native); free(fence); return doe_dx_hresult(hr,"shared.fence.handle",error); }
    fence->context=context; fence->fence=native;
    ++context->objects; *out=fence;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_shared_fence_import(doe_dx_context *context,void *handle,
    doe_dx_shared_fence **out,doe_dx_error *error)
{
    if (!context || !handle || handle==INVALID_HANDLE_VALUE || !out) return DOE_DX_INVALID_ARGUMENT;
    ID3D12Fence *native=NULL;
    HRESULT hr=ID3D12Device_OpenSharedHandle(context->native->device,handle,&IID_ID3D12Fence,(void **)&native);
    if (FAILED(hr)) return doe_dx_hresult(hr,"shared.fence.open",error);
    return wrap_fence(context,native,out,error);
}
void *doe_dx_shared_fence_handle(const doe_dx_shared_fence *fence) { return fence ? fence->handle : NULL; }
void doe_dx_shared_fence_release(doe_dx_shared_fence *fence)
{
    if (!fence) return;
    CloseHandle(fence->handle); ID3D12Fence_Release(fence->fence);
    --fence->context->objects; free(fence);
}
doe_dx_status doe_dx_shared_fence_wait(doe_dx_shared_fence *fence,uint64_t value,uint32_t wait_ms,doe_dx_error *error)
{
    if (!fence || value==UINT64_MAX) return DOE_DX_INVALID_ARGUMENT;
    ULONGLONG start=GetTickCount64();
    for (;;) {
        UINT64 completed=ID3D12Fence_GetCompletedValue(fence->fence);
        if (completed==UINT64_MAX) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"shared.fence.removed",error);
        if (completed>=value) return DOE_DX_OK;
        if (GetTickCount64()-start>=wait_ms) return doe_dx_fail(DOE_DX_PENDING,0,"shared.fence.wait",error);
        /* No event registration survives a timeout; the caller retains this
         * fence and its original device while waiting outside serialization. */
        Sleep(1);
    }
}
static doe_dx_status keyed_open(doe_dx_native_context *context,doe_dx_native_texture *texture,doe_dx_error *error)
{
    texture->on12_module=LoadLibraryExW(L"d3d11.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!texture->on12_module) return doe_dx_fail(DOE_DX_UNSUPPORTED,(int32_t)GetLastError(),"shared.on12.module",error);
    FARPROC address=GetProcAddress(texture->on12_module,"D3D11On12CreateDevice");
    PFN_D3D11ON12_CREATE_DEVICE create=NULL;
    _Static_assert(sizeof(create)==sizeof(address),"SDK function pointer size");
    memcpy(&create,&address,sizeof(create));
    if (!create) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"shared.on12.entry",error);
    IUnknown *queue=(IUnknown *)context->queue;
    ID3D11Device *device=NULL;
    HRESULT hr=create((IUnknown *)context->device,0,NULL,0,&queue,1,0,&device,&texture->on12_context,NULL);
    if (FAILED(hr)) return doe_dx_hresult(hr,"shared.on12.create",error);
    hr=ID3D11Device_QueryInterface(device,&IID_ID3D11On12Device,(void **)&texture->on12);
    ID3D11Device_Release(device);
    if (FAILED(hr)) return doe_dx_hresult(hr,"shared.on12.interface",error);
    D3D11_RESOURCE_FLAGS flags={0}; flags.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    hr=ID3D11On12Device_CreateWrappedResource(texture->on12,(IUnknown *)texture->resource,&flags,
        D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COMMON,&IID_ID3D11Resource,(void **)&texture->wrapped);
    if (FAILED(hr)) return doe_dx_hresult(hr,"shared.on12.wrap",error);
    hr=ID3D11Resource_QueryInterface(texture->wrapped,&IID_IDXGIKeyedMutex,(void **)&texture->keyed_mutex);
    return FAILED(hr) ? doe_dx_hresult(hr,"shared.keyed.interface",error) : DOE_DX_OK;
}
void doe_dx_shared_texture_native_close(doe_dx_native_texture *texture)
{
    if (texture->keyed_acquired) (void)IDXGIKeyedMutex_ReleaseSync(texture->keyed_mutex,0);
    if (texture->keyed_mutex) IDXGIKeyedMutex_Release(texture->keyed_mutex);
    if (texture->wrapped) {
        ID3D11Resource *wrapped=texture->wrapped;
        ID3D11On12Device_ReleaseWrappedResources(texture->on12,&wrapped,1);
        ID3D11Resource_Release(wrapped);
        ID3D11DeviceContext_Flush(texture->on12_context);
    }
    if (texture->on12_context) ID3D11DeviceContext_Release(texture->on12_context);
    if (texture->on12) ID3D11On12Device_Release(texture->on12);
    if (texture->on12_module) FreeLibrary(texture->on12_module);
}
doe_dx_status doe_dx_shared_texture_import(doe_dx_context *context,void *handle,bool keyed,
    doe_dx_texture **out,doe_dx_error *error)
{
    if (!context || !handle || handle==INVALID_HANDLE_VALUE || !out) return DOE_DX_INVALID_ARGUMENT;
    if (context->lost) return DOE_DX_DEVICE_LOST;
    if (context->objects==SIZE_MAX) return DOE_DX_RANGE;
    doe_dx_texture *texture=calloc(1,sizeof(*texture));
    doe_dx_native_texture *native=calloc(1,sizeof(*native));
    if (!texture || !native) { free(texture); free(native); return DOE_DX_OUT_OF_MEMORY; }
    HRESULT hr=ID3D12Device_OpenSharedHandle(context->native->device,handle,&IID_ID3D12Resource,(void **)&native->resource);
    doe_dx_status status=FAILED(hr) ? doe_dx_hresult(hr,"shared.texture.open",error) : DOE_DX_OK;
    if (status==DOE_DX_OK) ID3D12Resource_GetDesc(native->resource,&native->description);
    const D3D12_RESOURCE_DESC *desc=&native->description;
    if (status==DOE_DX_OK && (desc->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        !desc->Width || desc->Width>DOE_DX_MAX_TEXTURE_DIMENSION_2D || !desc->Height ||
        desc->Height>DOE_DX_MAX_TEXTURE_DIMENSION_2D || desc->DepthOrArraySize!=1 || desc->MipLevels!=1 ||
        desc->SampleDesc.Count!=1 || !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS))) status=DOE_DX_UNSUPPORTED;
    /* Browser presentation's color formats, including their linear/sRGB views.
     * Other SDK formats remain typed unsupported until their consumer is composed. */
    if (status==DOE_DX_OK) {
        switch (desc->Format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_TYPELESS: native->format=DOE_DX_RGBA8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS: native->format=DOE_DX_BGRA8_UNORM; break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: native->format=DOE_DX_RGBA16_FLOAT; break;
        case DXGI_FORMAT_R10G10B10A2_UNORM: native->format=DOE_DX_RGB10A2_UNORM; break;
        default: status=DOE_DX_UNSUPPORTED; break;
        }
    }
    if (status==DOE_DX_OK && keyed) status=keyed_open(context->native,native,error);
    if (status==DOE_DX_OK) status=doe_dx_texture_native_footprints(context->native,native,error);
    if (status!=DOE_DX_OK) { doe_dx_native_texture_destroy(native); free(texture); return status; }
    uint32_t usage=DOE_DX_TEXTURE_COPY_SRC|DOE_DX_TEXTURE_COPY_DST;
    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) usage|=DOE_DX_TEXTURE_SAMPLED;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) usage|=DOE_DX_TEXTURE_RENDER_ATTACHMENT;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) usage|=DOE_DX_TEXTURE_STORAGE;
    native->shared=true; texture->shared=true;
    texture->context=context; texture->native=native; texture->references=1; texture->samples=1;
    texture->desc=(doe_dx_texture_desc){(uint32_t)desc->Width,desc->Height,native->format,usage};
    texture->shape=(doe_dx_texture_shape){.image=texture->desc,.dimension=2,.depth_or_layers=1,.mip_levels=1,.samples=1};
    ++context->objects; *out=texture;
    return DOE_DX_OK;
}
doe_dx_commands *doe_dx_shared_texture_owner(const doe_dx_texture *texture)
{ return texture ? texture->owner : NULL; }
bool doe_dx_shared_texture_keyed(const doe_dx_texture *texture)
{ return texture && texture->native && texture->native->keyed_mutex; }
doe_dx_status doe_dx_shared_texture_acquire(doe_dx_texture *texture,doe_dx_error *error)
{
    if (!texture || !texture->native || !texture->native->shared) return DOE_DX_INVALID_ARGUMENT;
    doe_dx_native_texture *native=texture->native;
    if (!native->keyed_mutex) return DOE_DX_OK;
    if (native->keyed_acquired) return DOE_DX_BUSY;
    HRESULT hr=IDXGIKeyedMutex_AcquireSync(native->keyed_mutex,0,5000);
    if (hr!=S_OK) return doe_dx_fail(hr==WAIT_TIMEOUT ? DOE_DX_PENDING : DOE_DX_NATIVE_FAILURE,
        (int32_t)hr,"shared.keyed.acquire",error);
    native->keyed_acquired=true; return DOE_DX_OK;
}
doe_dx_status doe_dx_shared_texture_begin(doe_dx_texture *texture,bool initialized,bool read_only,doe_dx_error *error)
{
    if (!texture || !texture->native || !texture->native->shared || texture->evict_requested) return DOE_DX_INVALID_ARGUMENT;
    if (texture->owner || texture->shared_active) return doe_dx_fail(DOE_DX_BUSY,0,"shared.texture.pending",error);
    if (read_only && !initialized) return DOE_DX_INVALID_ARGUMENT;
    texture->initialized=initialized; texture->shared_read_only=read_only; texture->shared_active=true; return DOE_DX_OK;
}
doe_dx_status doe_dx_shared_texture_end(doe_dx_texture *texture,doe_dx_error *error)
{
    if (!texture || !texture->native || !texture->native->shared) return DOE_DX_INVALID_ARGUMENT;
    doe_dx_native_texture *native=texture->native;
    if (!native->keyed_mutex || !native->keyed_acquired) { texture->shared_active=false; return DOE_DX_OK; }
    if (texture->owner) return doe_dx_fail(DOE_DX_BUSY,0,"shared.keyed.pending",error);
    HRESULT hr=IDXGIKeyedMutex_ReleaseSync(native->keyed_mutex,0);
    if (hr!=S_OK) return doe_dx_hresult(hr,"shared.keyed.release",error);
    native->keyed_acquired=false; texture->shared_active=false; return DOE_DX_OK;
}
doe_dx_status doe_dx_shared_texture_release_point(doe_dx_texture *texture,
    doe_dx_shared_fence **out,uint64_t *value,bool *initialized,doe_dx_error *error)
{
    if (!texture || !texture->native || !texture->native->shared || !out || !value || !initialized) return DOE_DX_INVALID_ARGUMENT;
    doe_dx_commands *owner=texture->owner;
    if (owner && (owner->state!=DOE_DX_SUBMITTED || !owner->fence))
        return doe_dx_fail(DOE_DX_BUSY,0,"shared.texture.unresolved",error);
    uint64_t point=owner ? owner->fence : 0;
    bool ready=owner ? owner->textures[texture->owner_index].initialized : texture->initialized;
    ID3D12Fence *timeline=texture->context->native->fence;
    ID3D12Fence_AddRef(timeline);
    doe_dx_status status=wrap_fence(texture->context,timeline,out,error);
    if (status==DOE_DX_OK) { *value=point; *initialized=ready; }
    return status;
}
#endif
