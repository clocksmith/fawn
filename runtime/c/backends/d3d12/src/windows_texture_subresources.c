/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32
static uint32_t subresource(const doe_dx_native_texture *texture,doe_dx_texture_region_ex region)
{
    uint32_t layers=texture->description.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : texture->description.DepthOrArraySize;
    return region.mip+region.layer*texture->description.MipLevels+
        (region.aspect==2 ? layers*texture->description.MipLevels : 0u);
}
static void transition_slice(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,uint32_t index,
    D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER barrier={0}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=resource; barrier.Transition.Subresource=index;
    barrier.Transition.StateBefore=from; barrier.Transition.StateAfter=to;
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
}
void doe_dx_native_texture_region_transfer_ex(doe_dx_native_commands *commands,doe_dx_native_buffer *buffer,
    doe_dx_native_texture *texture,const doe_dx_texture_footprint *footprint,doe_dx_texture_region_ex region,bool upload)
{
    uint32_t index=subresource(texture,region);
    D3D12_TEXTURE_COPY_LOCATION linear={0},image={0};
    linear.pResource=buffer->resource; linear.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    linear.PlacedFootprint.Offset=footprint->offset;
    linear.PlacedFootprint.Footprint=texture->footprints[index].Footprint;
    linear.PlacedFootprint.Footprint.Width=region.rect.width;
    linear.PlacedFootprint.Footprint.Height=region.rect.height;
    linear.PlacedFootprint.Footprint.Depth=1;
    linear.PlacedFootprint.Footprint.RowPitch=footprint->row_pitch;
    image.pResource=texture->resource; image.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; image.SubresourceIndex=index;
    if (upload) {
        if (buffer->memory==DOE_DX_DEFAULT) doe_dx_transition(commands->list,buffer->resource,
            D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&image,region.rect.x,region.rect.y,region.z,&linear,NULL);
        if (buffer->memory==DOE_DX_DEFAULT) doe_dx_transition(commands->list,buffer->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    } else {
        D3D12_BOX box={region.rect.x,region.rect.y,region.z,region.rect.x+region.rect.width,
            region.rect.y+region.rect.height,region.z+1};
        transition_slice(commands->list,texture->resource,index,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&linear,0,0,0,&image,
            doe_dx_format_depth(texture->format) ? NULL : &box);
        transition_slice(commands->list,texture->resource,index,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    }
}
void doe_dx_native_texture_blit_ex(doe_dx_native_commands *commands,doe_dx_native_texture *source,
    doe_dx_texture_region_ex from,doe_dx_native_texture *destination,doe_dx_texture_region_ex to)
{
    uint32_t planes=!from.aspect && doe_dx_format_stencil(source->format) ? 2u : 1u;
    for (uint32_t plane=0;plane<planes;++plane) {
        doe_dx_texture_region_ex source_region=from,target_region=to;
        if (plane) { source_region.aspect=2; target_region.aspect=2; }
        uint32_t index=subresource(source,source_region);
        D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};
        src.pResource=source->resource; src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex=index;
        dst.pResource=destination->resource; dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex=subresource(destination,target_region);
        D3D12_BOX box={from.rect.x,from.rect.y,from.z,from.rect.x+from.rect.width,from.rect.y+from.rect.height,from.z+1};
        transition_slice(commands->list,source->resource,index,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&dst,to.rect.x,to.rect.y,to.z,&src,
            source->description.SampleDesc.Count>1 || doe_dx_format_depth(source->format) ? NULL : &box);
        transition_slice(commands->list,source->resource,index,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    }
}
#endif
