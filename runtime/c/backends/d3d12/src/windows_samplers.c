/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static D3D12_FILTER_TYPE filter(doe_dx_filter value)
{
    return value == DOE_DX_LINEAR ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
}
static D3D12_TEXTURE_ADDRESS_MODE address(doe_dx_address value)
{
    switch (value) {
    case DOE_DX_CLAMP: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case DOE_DX_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case DOE_DX_MIRROR: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    }
    abort();
}
static D3D12_COMPARISON_FUNC compare(doe_dx_sampler_compare value)
{
    switch (value) {
    case DOE_DX_SAMPLER_NO_COMPARE: return D3D12_COMPARISON_FUNC_ALWAYS;
    case DOE_DX_SAMPLER_NEVER: return D3D12_COMPARISON_FUNC_NEVER;
    case DOE_DX_SAMPLER_LESS: return D3D12_COMPARISON_FUNC_LESS;
    case DOE_DX_SAMPLER_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
    case DOE_DX_SAMPLER_LESS_EQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case DOE_DX_SAMPLER_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
    case DOE_DX_SAMPLER_NOT_EQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case DOE_DX_SAMPLER_GREATER_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case DOE_DX_SAMPLER_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    abort();
}
void doe_dx_sampler_native(const doe_dx_sampler_desc *sampler,D3D12_SAMPLER_DESC *out)
{
    D3D12_FILTER_REDUCTION_TYPE reduction = sampler->compare == DOE_DX_SAMPLER_NO_COMPARE ?
        D3D12_FILTER_REDUCTION_TYPE_STANDARD : D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
    *out = (D3D12_SAMPLER_DESC){0};
    out->Filter = sampler->max_anisotropy > 1 ? D3D12_ENCODE_ANISOTROPIC_FILTER(reduction) :
        D3D12_ENCODE_BASIC_FILTER(filter(sampler->min_filter),filter(sampler->mag_filter),filter(sampler->mip_filter),reduction);
    out->AddressU = address(sampler->address_u);
    out->AddressV = address(sampler->address_v);
    out->AddressW = address(sampler->address_w);
    out->MinLOD = sampler->min_lod;
    out->MaxLOD = sampler->max_lod;
    out->MaxAnisotropy = sampler->max_anisotropy;
    out->ComparisonFunc = compare(sampler->compare);
}

void doe_dx_release_descriptors(doe_dx_descriptor_use *use)
{
    if (!use) return;
    for (size_t i=0;i<DOE_DX_RENDER_STAGES;++i) if (use->storage_sizes[i]) doe_dx_native_buffer_destroy(use->storage_sizes[i]);
    if (use->heap) ID3D12DescriptorHeap_Release(use->heap);
    if (use->sampler_heap) ID3D12DescriptorHeap_Release(use->sampler_heap);
    free(use);
}

void doe_dx_bind_descriptors(doe_dx_native_commands *commands,doe_dx_native_pipeline *pipeline,
    doe_dx_descriptor_use *use,bool graphics)
{
    ID3D12DescriptorHeap *heaps[2];
    UINT count = 0;
    if (use->heap) heaps[count++] = use->heap;
    if (use->sampler_heap) heaps[count++] = use->sampler_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(commands->list,count,heaps);
    if (use->heap) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle;
        use->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(use->heap,&handle);
        if (graphics) ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commands->list,pipeline->resource_table,handle);
        else ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(commands->list,pipeline->resource_table,handle);
    }
    if (use->sampler_heap) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle;
        use->sampler_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(use->sampler_heap,&handle);
        if (graphics) ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commands->list,pipeline->sampler_table,handle);
        else ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(commands->list,pipeline->sampler_table,handle);
    }
}
#endif
