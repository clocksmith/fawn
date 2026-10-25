/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

doe_dx_status doe_dx_texture_native_footprints(doe_dx_native_context *context,doe_dx_native_texture *texture,doe_dx_error *error)
{
    const D3D12_RESOURCE_DESC *desc=&texture->description;
    if (desc->SampleDesc.Count>1) return DOE_DX_OK;
    uint32_t layers=desc->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc->DepthOrArraySize;
    D3D12_FEATURE_DATA_FORMAT_INFO format={0}; format.Format=desc->Format;
    HRESULT result=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_INFO,&format,sizeof(format));
    if (FAILED(result)) return doe_dx_hresult(result,"CheckFeatureSupport.format_planes",error);
    uint32_t expected=doe_dx_format_stencil(texture->format) ? 2u : 1u;
    if (format.PlaneCount!=expected) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.format_planes",error);
    uint32_t count=layers*desc->MipLevels*expected;
    if (!count) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"texture.subresources",error);
    texture->footprints=calloc(count,sizeof(*texture->footprints));
    if (!texture->footprints) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"texture.footprints",error);
    UINT64 total=0;
    ID3D12Device_GetCopyableFootprints(context->device,desc,0,count,0,texture->footprints,NULL,NULL,&total);
    if (!total || total==UINT64_MAX || total>SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE,0,"texture.native_footprints",error);
    texture->footprint_count=count;
    if (!(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) texture->upload_bytes=total;
    return DOE_DX_OK;
}
uint64_t doe_dx_native_texture_upload_bytes(const doe_dx_native_texture *texture)
{
    return texture->upload_bytes;
}

static doe_dx_status initialize_attachments(doe_dx_native_commands *commands,doe_dx_native_texture *texture,doe_dx_error *error)
{
    const D3D12_RESOURCE_DESC *desc=&texture->description;
    bool depth=(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0;
    if (desc->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.initialize_attachment_dimension",error);
    uint32_t count=(uint32_t)desc->DepthOrArraySize*desc->MipLevels;
    doe_dx_descriptor_use *use=calloc(1,sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"texture.initialize_descriptors",error);
    D3D12_DESCRIPTOR_HEAP_DESC heap={0};
    heap.Type=depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors=count;
    ID3D12Device *device=commands->context->device;
    HRESULT result=ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void **)&use->heap);
    if (FAILED(result)) { free(use); return doe_dx_hresult(result,"CreateDescriptorHeap.initialize",error); }
    D3D12_CPU_DESCRIPTOR_HANDLE start={0};
    use->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->heap,&start);
    UINT stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,heap.Type);
    for (uint32_t layer=0;layer<desc->DepthOrArraySize;++layer) for (uint32_t mip=0;mip<desc->MipLevels;++mip) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle={start.ptr+(SIZE_T)(layer*desc->MipLevels+mip)*stride};
        if (depth) {
            D3D12_DEPTH_STENCIL_VIEW_DESC view={0}; view.Format=doe_dx_texture_format(texture->format);
            if (desc->SampleDesc.Count>1) view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DMS;
            else if (desc->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
                view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE1D; view.Texture1D.MipSlice=mip;
            } else {
                view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                view.Texture2DArray.MipSlice=mip; view.Texture2DArray.FirstArraySlice=layer; view.Texture2DArray.ArraySize=1;
            }
            ID3D12Device_CreateDepthStencilView(device,texture->resource,&view,handle);
        } else {
            D3D12_RENDER_TARGET_VIEW_DESC view={0}; view.Format=doe_dx_texture_format(texture->format);
            view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DMS;
            ID3D12Device_CreateRenderTargetView(device,texture->resource,&view,handle);
        }
    }
    /* All fallible descriptor preparation is complete before recording/publishing. */
    use->next=commands->descriptors; commands->descriptors=use;
    D3D12_RESOURCE_STATES state=depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    doe_dx_transition(commands->list,texture->resource,D3D12_RESOURCE_STATE_COPY_DEST,state);
    for (uint32_t i=0;i<count;++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle={start.ptr+(SIZE_T)i*stride};
        if (depth) {
            D3D12_CLEAR_FLAGS flags=D3D12_CLEAR_FLAG_DEPTH;
            if (doe_dx_format_stencil(texture->format)) flags=(D3D12_CLEAR_FLAGS)(flags|D3D12_CLEAR_FLAG_STENCIL);
            ID3D12GraphicsCommandList_ClearDepthStencilView(commands->list,handle,flags,0,0,0,NULL);
        } else {
            const float zero[4]={0};
            ID3D12GraphicsCommandList_ClearRenderTargetView(commands->list,handle,zero,0,NULL);
        }
    }
    doe_dx_transition(commands->list,texture->resource,state,D3D12_RESOURCE_STATE_COPY_DEST);
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_texture_initialize(doe_dx_native_commands *commands,doe_dx_native_texture *texture,
    doe_dx_native_buffer *zero,doe_dx_error *error)
{
    if (!texture->upload_bytes) return initialize_attachments(commands,texture,error);
    if (!zero || zero->memory!=DOE_DX_UPLOAD || zero->size<texture->upload_bytes)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"texture.initialize_upload",error);
    for (uint32_t i=0;i<texture->footprint_count;++i) {
        D3D12_TEXTURE_COPY_LOCATION source={0},target={0};
        source.pResource=zero->resource; source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint=texture->footprints[i];
        target.pResource=texture->resource; target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; target.SubresourceIndex=i;
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&target,0,0,0,&source,NULL);
    }
    return DOE_DX_OK;
}
#endif
