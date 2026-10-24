/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

void doe_dx_native_texture_region_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, doe_dx_texture_region region, bool upload)
{
    D3D12_TEXTURE_COPY_LOCATION linear = {0}, image = {0};
    linear.pResource = buffer->resource;
    linear.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    linear.PlacedFootprint.Offset = footprint->offset;
    linear.PlacedFootprint.Footprint = (D3D12_SUBRESOURCE_FOOTPRINT){texture->description.Format,
        region.width,region.height,1,footprint->row_pitch};
    image.pResource = texture->resource;
    image.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (upload) {
        if (buffer->memory == DOE_DX_DEFAULT)
            doe_dx_transition(commands->list,buffer->resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&image,region.x,region.y,0,&linear,NULL);
        if (buffer->memory == DOE_DX_DEFAULT)
            doe_dx_transition(commands->list,buffer->resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    } else {
        D3D12_BOX box = {region.x,region.y,0,region.x + region.width,region.y + region.height,1};
        doe_dx_transition(commands->list,texture->resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&linear,0,0,0,&image,&box);
        doe_dx_transition(commands->list,texture->resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    }
}

void doe_dx_native_texture_blit(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_texture_region from, doe_dx_native_texture *destination,
    uint32_t destination_x, uint32_t destination_y)
{
    D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
    src.pResource = source->resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = destination->resource;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX box = {from.x,from.y,0,from.x + from.width,from.y + from.height,1};
    doe_dx_transition(commands->list,source->resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyTextureRegion(commands->list,&dst,destination_x,destination_y,0,&src,&box);
    doe_dx_transition(commands->list,source->resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
}
#endif
