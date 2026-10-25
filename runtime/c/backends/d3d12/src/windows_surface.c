/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#include "surface_native.h"
#ifdef _WIN32
#include <dxgi1_5.h>
struct doe_dx_native_surface {
    doe_dx_native_context *context;
    IDXGISwapChain3 *swapchain;
    HWND window;
    bool allow_tearing;
    doe_dx_surface_desc desc;
};
static DXGI_FORMAT surface_format(doe_dx_format format) {
    switch(format){
    case DOE_DX_RGBA8_UNORM:case DOE_DX_RGBA8_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DOE_DX_BGRA8_UNORM:case DOE_DX_BGRA8_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
static bool tearing(IDXGIFactory4 *factory) {
    IDXGIFactory5 *extended=NULL;BOOL supported=FALSE;
    HRESULT hr=IDXGIFactory4_QueryInterface(factory,&IID_IDXGIFactory5,(void **)&extended);
    if(SUCCEEDED(hr)){
        hr=IDXGIFactory5_CheckFeatureSupport(extended,DXGI_FEATURE_PRESENT_ALLOW_TEARING,&supported,sizeof(supported));
        IDXGIFactory5_Release(extended);
    }
    return SUCCEEDED(hr)&&supported;
}
doe_dx_status doe_dx_native_surface_capabilities(doe_dx_native_context *context,void *hwnd,
    doe_dx_surface_caps *out,doe_dx_error *error) {
    if(!IsWindow((HWND)hwnd))return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"surface.HWND",error);
    IDXGIFactory4 *factory=NULL;HRESULT hr=CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory);
    if(FAILED(hr))return doe_dx_hresult(hr,"surface.factory",error);
    doe_dx_surface_caps caps={.usages=DOE_DX_TEXTURE_RENDER_ATTACHMENT|DOE_DX_TEXTURE_COPY_SRC|
        DOE_DX_TEXTURE_COPY_DST|DOE_DX_TEXTURE_SAMPLED,.immediate=tearing(factory)};
    IDXGIFactory4_Release(factory);
    const doe_dx_format candidates[]={DOE_DX_BGRA8_UNORM,DOE_DX_BGRA8_SRGB,DOE_DX_RGBA8_UNORM,DOE_DX_RGBA8_SRGB};
    for(unsigned i=0;i<4;++i){
        D3D12_FEATURE_DATA_FORMAT_SUPPORT base={.Format=surface_format(candidates[i])};
        hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_SUPPORT,&base,sizeof(base));
        if(FAILED(hr))return doe_dx_hresult(hr,"surface.format_support",error);
        const UINT required=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_DISPLAY;
        if(((UINT)base.Support1&required)!=required)continue;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT view={.Format=doe_dx_texture_format(candidates[i])};
        hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_SUPPORT,&view,sizeof(view));
        if(FAILED(hr))return doe_dx_hresult(hr,"surface.view_support",error);
        const UINT view_required=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        if(((UINT)view.Support1&view_required)==view_required)caps.formats[caps.format_count++]=candidates[i];
    }
    if(!caps.format_count)return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"surface.formats",error);
    *out=caps;return doe_dx_fail(DOE_DX_OK,0,"surface.capabilities",error);
}
doe_dx_status doe_dx_native_surface_create(doe_dx_native_context *context,void *hwnd,
    const doe_dx_surface_desc *desc,doe_dx_native_surface **out,doe_dx_error *error) {
    doe_dx_native_surface *surface=calloc(1,sizeof(*surface));
    if(!surface)return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"surface.native_allocate",error);
    IDXGIFactory4 *factory=NULL;IDXGISwapChain1 *initial=NULL;
    HRESULT hr=CreateDXGIFactory1(&IID_IDXGIFactory4,(void **)&factory);
    const char *operation="surface.factory";
    if(FAILED(hr))goto done;
    DXGI_SWAP_CHAIN_DESC1 swap={0};swap.Width=desc->width;swap.Height=desc->height;
    swap.Format=surface_format(desc->format);swap.SampleDesc.Count=1;swap.BufferCount=2;
    swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    if(desc->usage&DOE_DX_TEXTURE_SAMPLED)swap.BufferUsage|=DXGI_USAGE_SHADER_INPUT;
    swap.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;swap.Scaling=DXGI_SCALING_STRETCH;
    swap.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    surface->allow_tearing=tearing(factory);
    if(surface->allow_tearing)swap.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    operation="CreateSwapChainForHwnd";
    hr=IDXGIFactory4_CreateSwapChainForHwnd(factory,(IUnknown *)context->queue,(HWND)hwnd,&swap,NULL,NULL,&initial);
    if(FAILED(hr))goto done;
    operation="surface.swapchain3";
    hr=IDXGISwapChain1_QueryInterface(initial,&IID_IDXGISwapChain3,(void **)&surface->swapchain);
    if(SUCCEEDED(hr)){surface->context=context;surface->window=(HWND)hwnd;surface->desc=*desc;*out=surface;}
done:
    if(initial)IDXGISwapChain1_Release(initial);
    if(factory)IDXGIFactory4_Release(factory);
    if(FAILED(hr))doe_dx_native_surface_destroy(surface);
    return doe_dx_hresult(hr,operation,error);
}
doe_dx_status doe_dx_native_surface_reconfigure(doe_dx_native_surface *surface,
    const doe_dx_surface_desc *desc,doe_dx_error *error) {
    if(!IsWindow(surface->window))return doe_dx_fail(DOE_DX_INVALID_STATE,0,"surface.HWND_lost",error);
    UINT flags=surface->allow_tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0;
    HRESULT hr=IDXGISwapChain3_ResizeBuffers(surface->swapchain,2,desc->width,desc->height,
        surface_format(desc->format),flags);
    if(SUCCEEDED(hr))surface->desc=*desc;
    return doe_dx_hresult(hr,"surface.ResizeBuffers",error);
}
void doe_dx_native_surface_destroy(doe_dx_native_surface *surface) {
    if(!surface)return;
    if(surface->swapchain)IDXGISwapChain3_Release(surface->swapchain);
    free(surface);
}
doe_dx_status doe_dx_native_surface_acquire(doe_dx_native_surface *surface,
    doe_dx_native_texture **out,uint32_t *frame_index,doe_dx_error *error) {
    if(!IsWindow(surface->window))return doe_dx_fail(DOE_DX_INVALID_STATE,0,"surface.HWND_lost",error);
    UINT index=IDXGISwapChain3_GetCurrentBackBufferIndex(surface->swapchain);
    if(index>=2)return doe_dx_fail(DOE_DX_NATIVE_FAILURE,0,"surface.backbuffer_index",error);
    doe_dx_native_texture *texture=calloc(1,sizeof(*texture));
    if(!texture)return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"surface.frame_allocate",error);
    HRESULT hr=IDXGISwapChain3_GetBuffer(surface->swapchain,index,&IID_ID3D12Resource,(void **)&texture->resource);
    if(FAILED(hr)){free(texture);return doe_dx_hresult(hr,"surface.GetBuffer",error);}
    texture->resource->lpVtbl->GetDesc(texture->resource,&texture->description);
    texture->format=surface->desc.format;
    const D3D12_RESOURCE_DESC *d=&texture->description;
    if(d->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d->Width!=surface->desc.width ||
        d->Height!=surface->desc.height || d->DepthOrArraySize!=1 || d->MipLevels!=1 ||
        d->SampleDesc.Count!=1 || d->Format!=surface_format(surface->desc.format)){
        doe_dx_native_texture_destroy(texture);return doe_dx_fail(DOE_DX_NATIVE_FAILURE,0,"surface.backbuffer_descriptor",error);
    }
    doe_dx_status status=doe_dx_texture_native_footprints(surface->context,texture,error);
    if(status!=DOE_DX_OK){doe_dx_native_texture_destroy(texture);return status;}
    *out=texture;*frame_index=index;return doe_dx_fail(DOE_DX_OK,0,"surface.GetBuffer",error);
}
doe_dx_status doe_dx_native_surface_transition(doe_dx_native_commands *commands,
    doe_dx_native_texture *texture,bool acquire,doe_dx_error *error) {
    doe_dx_transition(commands->list,texture->resource,
        acquire?D3D12_RESOURCE_STATE_PRESENT:D3D12_RESOURCE_STATE_COPY_DEST,
        acquire?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_PRESENT);
    return doe_dx_fail(DOE_DX_OK,0,"surface.ResourceBarrier",error);
}
doe_dx_status doe_dx_native_surface_present(doe_dx_native_surface *surface,uint32_t index,doe_dx_error *error) {
    if(IDXGISwapChain3_GetCurrentBackBufferIndex(surface->swapchain)!=index)
        return doe_dx_fail(DOE_DX_INVALID_STATE,0,"surface.present_identity",error);
    HRESULT hr=IDXGISwapChain3_Present(surface->swapchain,surface->desc.immediate?0u:1u,
        surface->desc.immediate?DXGI_PRESENT_ALLOW_TEARING:0u);
    return doe_dx_hresult(hr,"IDXGISwapChain3.Present",error);
}
#endif
