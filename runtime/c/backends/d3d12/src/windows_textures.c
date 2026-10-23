/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

DXGI_FORMAT doe_dx_texture_format(doe_dx_format format) {
    switch (format) {
        case DOE_DX_RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DOE_DX_RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
        case DOE_DX_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DOE_DX_R32_UINT: return DXGI_FORMAT_R32_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

void doe_dx_native_texture_destroy(doe_dx_native_texture *texture) {
    if (!texture) return;
    if (texture->resource) ID3D12Resource_Release(texture->resource);
    free(texture);
}

doe_dx_status doe_dx_native_texture_create(doe_dx_native_context *context,
    const doe_dx_texture_desc *desc, doe_dx_native_texture **out, doe_dx_error *error) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {0};
    support.Format = doe_dx_texture_format(desc->format);
    HRESULT result = ID3D12Device_CheckFeatureSupport(context->device,
        D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    if (FAILED(result)) return doe_dx_hresult(result, "CheckFeatureSupport.format", error);
    UINT required = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
    if (desc->usage & DOE_DX_TEXTURE_SAMPLED) required |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    if ((desc->usage & DOE_DX_TEXTURE_SAMPLED) &&
        (desc->format == DOE_DX_RGBA8_UNORM || desc->format == DOE_DX_R32_FLOAT))
        required |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    if (desc->usage & DOE_DX_TEXTURE_STORAGE)
        required |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    if (desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT) required |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
    if (((UINT)support.Support1 & required) != required)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.native_format", error);
    if (desc->usage & DOE_DX_TEXTURE_STORAGE) {
        UINT storage = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
        if (((UINT)support.Support2 & storage) != storage)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.native_storage", error);
    }
    doe_dx_native_texture *texture = calloc(1, sizeof(*texture));
    if (!texture) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.texture.allocate", error);
    D3D12_RESOURCE_DESC *description = &texture->description;
    description->Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description->Width = desc->width;
    description->Height = desc->height;
    description->DepthOrArraySize = 1;
    description->MipLevels = 1;
    description->Format = support.Format;
    description->SampleDesc.Count = 1;
    description->Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (desc->usage & DOE_DX_TEXTURE_STORAGE)
        description->Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT)
        description->Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap = {0};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    result = ID3D12Device_CreateCommittedResource(context->device, &heap, D3D12_HEAP_FLAG_NONE,
        description, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
        (void **)&texture->resource);
    if (FAILED(result)) {
        doe_dx_native_texture_destroy(texture);
        return doe_dx_hresult(result, "CreateCommittedResource.texture", error);
    }
    /* Check the admitted four-byte format layout against the SDK's real
     * footprint query; no backend-private approximation overrides native facts. */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    ID3D12Device_GetCopyableFootprints(context->device, description, 0, 1, 0,
        &footprint, &rows, &row_bytes, &total);
    doe_dx_texture_footprint expected;
    doe_dx_status normalized = doe_dx_texture_layout_2d(desc, (doe_dx_texture_layout){0}, &expected);
    if (normalized != DOE_DX_OK || rows != expected.rows || row_bytes != expected.row_bytes ||
        total != expected.span || footprint.Offset != 0 || footprint.Footprint.RowPitch != expected.row_pitch ||
        footprint.Footprint.Width != desc->width || footprint.Footprint.Height != desc->height ||
        footprint.Footprint.Depth != 1 || footprint.Footprint.Format != support.Format) {
        doe_dx_native_texture_destroy(texture);
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.native_footprint", error);
    }
    *out = texture;
    return doe_dx_fail(DOE_DX_OK, 0, "native.texture.create", error);
}

void doe_dx_native_texture_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, bool upload) {
    D3D12_TEXTURE_COPY_LOCATION linear = {0};
    linear.pResource = buffer->resource;
    linear.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    linear.PlacedFootprint.Offset = footprint->offset;
    linear.PlacedFootprint.Footprint.Format = texture->description.Format;
    linear.PlacedFootprint.Footprint.Width = (UINT)texture->description.Width;
    linear.PlacedFootprint.Footprint.Height = texture->description.Height;
    linear.PlacedFootprint.Footprint.Depth = 1;
    linear.PlacedFootprint.Footprint.RowPitch = footprint->row_pitch;
    D3D12_TEXTURE_COPY_LOCATION image = {0};
    image.pResource = texture->resource;
    image.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (upload) {
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list, &image, 0, 0, 0, &linear, NULL);
    } else {
        doe_dx_transition(commands->list, texture->resource,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list, &linear, 0, 0, 0, &image, NULL);
        doe_dx_transition(commands->list, texture->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    }
}

void doe_dx_native_texture_copy(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_native_texture *destination) {
    doe_dx_transition(commands->list, source->resource,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyResource(commands->list, destination->resource, source->resource);
    doe_dx_transition(commands->list, source->resource,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}
#endif
