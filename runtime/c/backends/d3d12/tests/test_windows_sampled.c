/* SPDX-License-Identifier: MIT
 * Official SDK CPU root/heap/descriptor controls only; no adapter/device/GPU. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct sdk_heap {
    ID3D12DescriptorHeap iface;
    ULONG references;
    UINT index;
} sdk_heap;
typedef struct sdk_root { ID3D12RootSignature iface; ULONG references; } sdk_root;
typedef struct sdk_state { ID3D12PipelineState iface; ULONG references; } sdk_state;
typedef struct sdk_device {
    ID3D12Device iface;
    sdk_heap heaps[2];
    sdk_root root;
    sdk_state state;
    UINT heap_calls, fail_heap, views, samplers, resources;
    bool no_typed_load;
    D3D12_SAMPLER_DESC sampler_values[2];
} sdk_device;
typedef struct sdk_list {
    ID3D12GraphicsCommandList iface;
    UINT heaps, tables, barriers;
    UINT table_indices[2];
    D3D12_GPU_DESCRIPTOR_HANDLE handles[2];
    D3D12_RESOURCE_BARRIER transitions[4];
} sdk_list;
static ULONG STDMETHODCALLTYPE release_heap(ID3D12DescriptorHeap *raw)
{
    sdk_heap *heap = (sdk_heap *)raw;
    assert(heap->references == 1);
    return --heap->references;
}
static D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE cpu_start(ID3D12DescriptorHeap *raw,D3D12_CPU_DESCRIPTOR_HANDLE *out)
{
    sdk_heap *heap = (sdk_heap *)raw;
    assert(heap->references);
    *out = (D3D12_CPU_DESCRIPTOR_HANDLE){100u + 1000u * heap->index};
    return out;
}
static D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE gpu_start(ID3D12DescriptorHeap *raw,D3D12_GPU_DESCRIPTOR_HANDLE *out)
{
    sdk_heap *heap = (sdk_heap *)raw;
    assert(heap->references);
    *out = (D3D12_GPU_DESCRIPTOR_HANDLE){10000u + 1000u * heap->index};
    return out;
}
static CONST_VTBL ID3D12DescriptorHeapVtbl heap_methods = {
    .Release=release_heap,.GetCPUDescriptorHandleForHeapStart=cpu_start,.GetGPUDescriptorHandleForHeapStart=gpu_start};
static ULONG STDMETHODCALLTYPE release_root(ID3D12RootSignature *raw)
{
    sdk_root *root = (sdk_root *)raw;
    assert(root->references == 1);
    return --root->references;
}
static ULONG STDMETHODCALLTYPE release_state(ID3D12PipelineState *raw)
{
    sdk_state *state = (sdk_state *)raw;
    assert(state->references == 1);
    return --state->references;
}
static CONST_VTBL ID3D12RootSignatureVtbl root_methods = {.Release=release_root};
static CONST_VTBL ID3D12PipelineStateVtbl state_methods = {.Release=release_state};
static HRESULT STDMETHODCALLTYPE create_root(ID3D12Device *raw,UINT node,const void *bytes,SIZE_T size,
    REFIID id,void **out)
{
    (void)id;
    sdk_device *device = (sdk_device *)raw;
    assert(node == 0);
    ID3D12RootSignatureDeserializer *decoded = NULL;
    assert(SUCCEEDED(D3D12CreateRootSignatureDeserializer(bytes,size,&IID_ID3D12RootSignatureDeserializer,(void **)&decoded)));
    const D3D12_ROOT_SIGNATURE_DESC *root = ID3D12RootSignatureDeserializer_GetRootSignatureDesc(decoded);
    assert(root->NumParameters == (device->resources ? 2u : 1u) && !root->NumStaticSamplers);
    UINT sampler_slot = 0;
    if (device->resources) {
        const D3D12_ROOT_DESCRIPTOR_TABLE *table = &root->pParameters[0].DescriptorTable;
        assert(table->NumDescriptorRanges == 3);
        assert(table->pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
            table->pDescriptorRanges[0].BaseShaderRegister == 2 && table->pDescriptorRanges[0].RegisterSpace == 0);
        assert(table->pDescriptorRanges[1].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
            table->pDescriptorRanges[1].BaseShaderRegister == 3 && table->pDescriptorRanges[1].RegisterSpace == 1);
        assert(table->pDescriptorRanges[2].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
        sampler_slot = 1;
    }
    const D3D12_ROOT_DESCRIPTOR_TABLE *samplers = &root->pParameters[sampler_slot].DescriptorTable;
    assert(samplers->NumDescriptorRanges == 2);
    for (UINT i = 0; i < 2; ++i) {
        assert(samplers->pDescriptorRanges[i].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER &&
            samplers->pDescriptorRanges[i].BaseShaderRegister == 5 + i &&
            samplers->pDescriptorRanges[i].RegisterSpace == 3 &&
            samplers->pDescriptorRanges[i].OffsetInDescriptorsFromTableStart == i);
    }
    ID3D12RootSignatureDeserializer_Release(decoded);
    device->root.iface.lpVtbl = &root_methods;
    device->root.references = 1;
    *out = &device->root.iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE create_state(ID3D12Device *raw,const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
    REFIID id,void **out)
{
    (void)id;
    sdk_device *device = (sdk_device *)raw;
    assert(desc->pRootSignature == &device->root.iface && desc->CS.BytecodeLength == 1);
    device->state.iface.lpVtbl = &state_methods;
    device->state.references = 1;
    *out = &device->state.iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE create_heap(ID3D12Device *raw,const D3D12_DESCRIPTOR_HEAP_DESC *desc,
    REFIID id,void **out)
{
    (void)id;
    sdk_device *device = (sdk_device *)raw;
    if (++device->heap_calls == device->fail_heap) return E_OUTOFMEMORY;
    assert(desc->Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    UINT index = desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 1u : 0u;
    assert(desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER || desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    assert(desc->NumDescriptors == (index ? 2u : 3u));
    sdk_heap *heap = &device->heaps[index];
    assert(!heap->references);
    heap->iface.lpVtbl = &heap_methods;
    heap->index = index;
    heap->references = 1;
    *out = &heap->iface;
    return S_OK;
}
static UINT STDMETHODCALLTYPE increment(ID3D12Device *raw,D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    (void)raw;
    return type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 32u : 64u;
}
static void STDMETHODCALLTYPE create_srv(ID3D12Device *raw,ID3D12Resource *resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    sdk_device *device = (sdk_device *)raw;
    assert(resource && desc->Format == DXGI_FORMAT_R32_UINT && desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D &&
        desc->Texture2D.MipLevels == 1 && desc->Shader4ComponentMapping == D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    assert(handle.ptr == 100u + 64u * device->views);
    device->views++;
}
static void STDMETHODCALLTYPE create_uav(ID3D12Device *raw,ID3D12Resource *resource,ID3D12Resource *counter,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    sdk_device *device = (sdk_device *)raw;
    assert(resource && !counter && desc->Format == DXGI_FORMAT_R32_UINT && desc->ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D);
    assert(handle.ptr == 228);
    device->views++;
}
static void STDMETHODCALLTYPE create_sampler(ID3D12Device *raw,const D3D12_SAMPLER_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    sdk_device *device = (sdk_device *)raw;
    assert(device->samplers < 2 && handle.ptr == 1100u + 32u * device->samplers);
    device->sampler_values[device->samplers++] = *desc;
}
static HRESULT STDMETHODCALLTYPE query_format(ID3D12Device *raw,D3D12_FEATURE feature,void *data,UINT size)
{
    sdk_device *device=(sdk_device *)raw;
    assert(feature==D3D12_FEATURE_FORMAT_SUPPORT && size==sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT));
    D3D12_FEATURE_DATA_FORMAT_SUPPORT *support=data;
    support->Support2=device->no_typed_load ? D3D12_FORMAT_SUPPORT2_NONE : D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    return S_OK;
}
static CONST_VTBL ID3D12DeviceVtbl device_methods = {.CheckFeatureSupport=query_format,.CreateRootSignature=create_root,
    .CreateComputePipelineState=create_state,.CreateDescriptorHeap=create_heap,.GetDescriptorHandleIncrementSize=increment,
    .CreateShaderResourceView=create_srv,.CreateUnorderedAccessView=create_uav,.CreateSampler=create_sampler};
static void STDMETHODCALLTYPE set_heaps(ID3D12GraphicsCommandList *raw,UINT count,ID3D12DescriptorHeap *const *heaps)
{
    sdk_list *list = (sdk_list *)raw;
    assert(count >= 1 && count <= 2 && heaps[count - 1]);
    list->heaps = count;
}
static void STDMETHODCALLTYPE set_table(ID3D12GraphicsCommandList *raw,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    sdk_list *list = (sdk_list *)raw;
    assert(list->tables < 2);
    list->table_indices[list->tables] = index;
    list->handles[list->tables++] = handle;
}
static void STDMETHODCALLTYPE barriers(ID3D12GraphicsCommandList *raw,UINT count,const D3D12_RESOURCE_BARRIER *values)
{
    sdk_list *list = (sdk_list *)raw;
    assert(count == 1 && list->barriers < 4);
    list->transitions[list->barriers++] = *values;
}
static CONST_VTBL ID3D12GraphicsCommandListVtbl list_methods = {
    .SetDescriptorHeaps=set_heaps,.SetComputeRootDescriptorTable=set_table,.ResourceBarrier=barriers};
static void component_mapping(void)
{
    doe_dx_native_texture texture={.format=DOE_DX_RGBA8_UNORM};
    doe_dx_texture_view_desc span={.format=DOE_DX_RGBA8_UNORM,.dimension=2,.mip_count=1,.layer_count=1,
        .swizzle={DOE_DX_SWIZZLE_B,DOE_DX_SWIZZLE_ZERO,DOE_DX_SWIZZLE_R,DOE_DX_SWIZZLE_ONE}};
    D3D12_SHADER_RESOURCE_VIEW_DESC view;
    doe_dx_texture_srv(&texture,&span,&view);
    assert(view.Shader4ComponentMapping==D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2,4,0,5));
    span.swizzle[0]=span.swizzle[1]=span.swizzle[2]=span.swizzle[3]=DOE_DX_SWIZZLE_IDENTITY;
    doe_dx_texture_srv(&texture,&span,&view);
    assert(view.Shader4ComponentMapping==D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
}
int main(void)
{
    component_mapping();
    sdk_device device = {.resources=3};
    device.iface.lpVtbl = &device_methods;
    doe_dx_native_context context = {.device=&device.iface};
    doe_dx_texture_binding_layout textures[3] = {{2,0,DOE_DX_RGBA8_UNORM,DOE_DX_READ_ONLY, false},
        {3,1,DOE_DX_R32_UINT,DOE_DX_READ_ONLY, false},{4,2,DOE_DX_R32_UINT,DOE_DX_READ_WRITE, false}};
    doe_dx_sampler_layout samplers[2] = {{5,3,false},{6,3,true}};
    unsigned char bytecode = 1;
    doe_dx_dynamic_pipeline_desc desc = {.resources={.buffers={&bytecode,1,NULL,0},.textures=textures,.texture_count=3},
        .samplers=samplers,.sampler_count=2};
    doe_dx_native_pipeline *pipeline = NULL;
    assert(doe_dx_native_pipeline_create_dynamic(&context,&desc,&pipeline,NULL) == DOE_DX_OK);
    assert(pipeline->resource_table == 0 && pipeline->sampler_table == 1);
    sdk_list list = {0};
    list.iface.lpVtbl = &list_methods;
    doe_dx_native_commands commands = {.context=&context,.list=&list.iface};
    ID3D12Resource source_resource = {0}, destination_resource = {0};
    doe_dx_native_texture source = {.resource=&source_resource,.format=DOE_DX_R32_UINT},
        writable = {.resource=&destination_resource,.format=DOE_DX_R32_UINT};
    source.description.Format = writable.description.Format = DXGI_FORMAT_R32_UINT;
    doe_dx_native_resources values = {.textures={&source,&source,&writable},.texture_count=3,.sampler_count=2};
    values.samplers[0] = (doe_dx_sampler_desc){.min_filter=DOE_DX_LINEAR,.mag_filter=DOE_DX_NEAREST,
        .mip_filter=DOE_DX_LINEAR,.address_u=DOE_DX_REPEAT,.address_v=DOE_DX_MIRROR,.address_w=DOE_DX_CLAMP,
        .min_lod=0.25f,.max_lod=4,.max_anisotropy=1};
    values.samplers[1] = (doe_dx_sampler_desc){.min_filter=DOE_DX_LINEAR,.mag_filter=DOE_DX_LINEAR,
        .mip_filter=DOE_DX_LINEAR,.address_u=DOE_DX_CLAMP,.address_v=DOE_DX_REPEAT,.address_w=DOE_DX_MIRROR,
        .min_lod=1,.max_lod=8,.max_anisotropy=16,.compare=DOE_DX_SAMPLER_LESS_EQUAL};
    doe_dx_descriptor_use *use = NULL;
    device.no_typed_load=true;
    assert(doe_dx_prepare_descriptors(&commands,pipeline,&values,&use,NULL)==DOE_DX_UNSUPPORTED && !use);
    assert(!device.heap_calls && !device.views && !device.samplers);
    device.no_typed_load=false;
    device.fail_heap = 2;
    doe_dx_error error = {0};
    assert(doe_dx_prepare_descriptors(&commands,pipeline,&values,&use,&error) == DOE_DX_OUT_OF_MEMORY && !use);
    assert(!device.heaps[0].references && !device.views && !device.samplers && !list.heaps && !list.tables);
    assert(error.native_code == (int32_t)E_OUTOFMEMORY);
    device.fail_heap = 0;
    assert(doe_dx_prepare_descriptors(&commands,pipeline,&values,&use,NULL) == DOE_DX_OK);
    assert(device.views == 3 && device.samplers == 2);
    assert(device.sampler_values[0].Filter == D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR &&
        device.sampler_values[0].AddressU == D3D12_TEXTURE_ADDRESS_MODE_WRAP &&
        device.sampler_values[0].AddressV == D3D12_TEXTURE_ADDRESS_MODE_MIRROR &&
        device.sampler_values[0].MinLOD == 0.25f && device.sampler_values[0].MaxLOD == 4);
    assert(device.sampler_values[1].Filter == D3D12_FILTER_COMPARISON_ANISOTROPIC &&
        device.sampler_values[1].MaxAnisotropy == 16 && device.sampler_values[1].ComparisonFunc == D3D12_COMPARISON_FUNC_LESS_EQUAL);
    doe_dx_bind_descriptors(&commands,pipeline,use,false);
    assert(list.heaps == 2 && list.tables == 2 && list.table_indices[0] == 0 && list.table_indices[1] == 1);
    assert(list.handles[0].ptr == 10000 && list.handles[1].ptr == 11000);
    doe_dx_transition_resources(&commands,pipeline,&values,false,false);
    doe_dx_transition_resources(&commands,pipeline,&values,false,true);
    assert(list.barriers == 4 && list.transitions[0].Transition.pResource == &source_resource &&
        list.transitions[1].Transition.pResource == &destination_resource);
    assert(list.transitions[0].Transition.StateAfter == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE &&
        list.transitions[1].Transition.StateAfter == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    doe_dx_release_descriptors(use);
    doe_dx_native_pipeline_destroy(pipeline);
    assert(!device.root.references && !device.state.references && !device.heaps[0].references && !device.heaps[1].references);
    device.resources = 0;
    desc.resources.texture_count = 0;
    assert(doe_dx_native_pipeline_create_dynamic(&context,&desc,&pipeline,NULL) == DOE_DX_OK);
    assert(pipeline->resource_table == UINT32_MAX && pipeline->sampler_table == 0);
    values.texture_count = 0;
    device.samplers = 0;
    memset(&list,0,sizeof(list)); list.iface.lpVtbl = &list_methods;
    assert(doe_dx_prepare_descriptors(&commands,pipeline,&values,&use,NULL) == DOE_DX_OK);
    doe_dx_bind_descriptors(&commands,pipeline,use,false);
    assert(list.heaps == 1 && list.tables == 1 && list.table_indices[0] == 0 && list.handles[0].ptr == 11000);
    doe_dx_release_descriptors(use);
    doe_dx_native_pipeline_destroy(pipeline);
    puts("PASS actual SDK CPU root serialization, sampled/storage SRVs, dynamic sampler heaps, exact fields/table handles, alias barriers, heap OOM rollback and release; no device/GPU");
}
