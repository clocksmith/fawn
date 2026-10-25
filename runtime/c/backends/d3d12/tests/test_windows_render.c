/* SPDX-License-Identifier: MIT
 * Official SDK CPU root/packed-heap/pass ledger, never a device or GPU. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct cpu_heap { ID3D12DescriptorHeap iface; ULONG refs; UINT kind; } cpu_heap;
typedef struct cpu_root { ID3D12RootSignature iface; ULONG refs; } cpu_root;
typedef struct cpu_state { ID3D12PipelineState iface; ULONG refs; } cpu_state;
typedef struct cpu_device {
    ID3D12Device iface;
    cpu_heap heaps[3];
    cpu_root root;
    cpu_state state;
    UINT heap_calls,fail_heap,srvs,samplers,rtvs,format_queries;
    DXGI_FORMAT expected_vertex;
    bool deny_vertex, expected_blend;
    D3D12_CULL_MODE expected_cull;
    UINT expected_sample_mask;
    UINT8 expected_write_mask;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE expected_topology;
} cpu_device;
typedef struct cpu_list {
    ID3D12GraphicsCommandList iface;
    UINT mutations,heap_count,tables,barriers,clears,draws,releases,vertex_sets;
    ID3D12Resource *shared;
    UINT table_indices[4];
    UINT64 table_handles[4];
} cpu_list;
static ULONG STDMETHODCALLTYPE heap_release(ID3D12DescriptorHeap *raw)
{
    cpu_heap *heap=(cpu_heap *)raw;
    assert(heap->refs==1);
    return --heap->refs;
}
static D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE cpu_handle(ID3D12DescriptorHeap *raw,D3D12_CPU_DESCRIPTOR_HANDLE *out)
{
    cpu_heap *heap=(cpu_heap *)raw;
    assert(heap->refs);
    *out=(D3D12_CPU_DESCRIPTOR_HANDLE){100+1000*heap->kind};
    return out;
}
static D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE gpu_handle(ID3D12DescriptorHeap *raw,D3D12_GPU_DESCRIPTOR_HANDLE *out)
{
    cpu_heap *heap=(cpu_heap *)raw;
    assert(heap->refs);
    *out=(D3D12_GPU_DESCRIPTOR_HANDLE){10000+10000*heap->kind};
    return out;
}
static CONST_VTBL ID3D12DescriptorHeapVtbl heap_methods={.Release=heap_release,
    .GetCPUDescriptorHandleForHeapStart=cpu_handle,.GetGPUDescriptorHandleForHeapStart=gpu_handle};
static ULONG STDMETHODCALLTYPE root_release(ID3D12RootSignature *raw)
{
    cpu_root *root=(cpu_root *)raw;
    assert(root->refs==1);
    return --root->refs;
}
static ULONG STDMETHODCALLTYPE state_release(ID3D12PipelineState *raw)
{
    cpu_state *state=(cpu_state *)raw;
    assert(state->refs==1);
    return --state->refs;
}
static CONST_VTBL ID3D12RootSignatureVtbl root_methods={.Release=root_release};
static CONST_VTBL ID3D12PipelineStateVtbl state_methods={.Release=state_release};
static HRESULT STDMETHODCALLTYPE create_root(ID3D12Device *raw,UINT node,const void *bytes,SIZE_T size,REFIID id,void **out)
{
    (void)id;
    assert(!node);
    cpu_device *device=(cpu_device *)raw;
    ID3D12RootSignatureDeserializer *decoded=NULL;
    assert(SUCCEEDED(D3D12CreateRootSignatureDeserializer(bytes,size,&IID_ID3D12RootSignatureDeserializer,(void **)&decoded)));
    const D3D12_ROOT_SIGNATURE_DESC *root=ID3D12RootSignatureDeserializer_GetRootSignatureDesc(decoded);
    assert(root->NumParameters==4 && !root->NumStaticSamplers);
    assert(root->Flags==(device->expected_vertex ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT :
        D3D12_ROOT_SIGNATURE_FLAG_NONE));
    for (UINT table=0;table<4;++table) {
        const D3D12_ROOT_PARAMETER *parameter=&root->pParameters[table];
        assert(parameter->ShaderVisibility==(table<2 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL));
        assert(parameter->ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
        bool sampler=table%2!=0;
        const D3D12_ROOT_DESCRIPTOR_TABLE *ranges=&parameter->DescriptorTable;
        assert(ranges->NumDescriptorRanges==(sampler ? 16u : 64u));
        for (UINT i=0;i<ranges->NumDescriptorRanges;++i) {
            const D3D12_DESCRIPTOR_RANGE *range=&ranges->pDescriptorRanges[i];
            assert(range->RangeType==(sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER : D3D12_DESCRIPTOR_RANGE_TYPE_SRV));
            assert(range->BaseShaderRegister==i && !range->RegisterSpace && range->NumDescriptors==1 &&
                range->OffsetInDescriptorsFromTableStart==i);
        }
    }
    ID3D12RootSignatureDeserializer_Release(decoded);
    device->root.iface.lpVtbl=&root_methods;
    device->root.refs=1;
    *out=&device->root.iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE create_state(ID3D12Device *raw,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
    REFIID id,void **out)
{
    (void)id;
    cpu_device *device=(cpu_device *)raw;
    assert(desc->pRootSignature==&device->root.iface && desc->VS.BytecodeLength==1 && desc->PS.BytecodeLength==1);
    assert(desc->RasterizerState.FrontCounterClockwise && desc->RasterizerState.CullMode==device->expected_cull);
    assert(desc->BlendState.RenderTarget[0].BlendEnable==device->expected_blend);
    if (device->expected_blend) {
        const D3D12_RENDER_TARGET_BLEND_DESC *blend=&desc->BlendState.RenderTarget[0];
        assert(blend->SrcBlend==D3D12_BLEND_SRC_ALPHA && blend->DestBlend==D3D12_BLEND_INV_SRC_ALPHA &&
            blend->BlendOp==D3D12_BLEND_OP_ADD && blend->SrcBlendAlpha==D3D12_BLEND_ONE &&
            blend->DestBlendAlpha==D3D12_BLEND_INV_SRC_ALPHA && blend->BlendOpAlpha==D3D12_BLEND_OP_ADD);
    }
    assert(desc->SampleMask==device->expected_sample_mask &&
        desc->BlendState.RenderTarget[0].RenderTargetWriteMask==device->expected_write_mask &&
        desc->PrimitiveTopologyType==device->expected_topology);
    assert(!desc->DepthStencilState.DepthEnable && desc->SampleDesc.Count==1 &&
        desc->NumRenderTargets==1 && desc->RTVFormats[0]==DXGI_FORMAT_R8G8B8A8_UNORM);
    if (device->expected_vertex) {
        assert(desc->InputLayout.NumElements==1);
        const D3D12_INPUT_ELEMENT_DESC *attribute=desc->InputLayout.pInputElementDescs;
        assert(!strcmp(attribute->SemanticName,"TEXCOORD") && attribute->SemanticIndex==3 &&
            attribute->Format==device->expected_vertex && attribute->InputSlot==1 && attribute->AlignedByteOffset==8 &&
            attribute->InputSlotClass==D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA && attribute->InstanceDataStepRate==1);
    } else assert(!desc->InputLayout.NumElements);
    device->state.iface.lpVtbl=&state_methods;
    device->state.refs=1;
    *out=&device->state.iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE create_heap(ID3D12Device *raw,const D3D12_DESCRIPTOR_HEAP_DESC *desc,REFIID id,void **out)
{
    (void)id;
    cpu_device *device=(cpu_device *)raw;
    if (++device->heap_calls==device->fail_heap) return E_OUTOFMEMORY;
    UINT kind=desc->Type==D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? 0u :
        desc->Type==D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 1u : 2u;
    assert(desc->Type==D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || desc->Type==D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ||
        desc->Type==D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    assert(desc->NumDescriptors==(kind==0 ? 128u : kind==1 ? 32u : 1u));
    assert(desc->Flags==(kind==2 ? D3D12_DESCRIPTOR_HEAP_FLAG_NONE : D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));
    cpu_heap *heap=&device->heaps[kind];
    assert(!heap->refs);
    heap->iface.lpVtbl=&heap_methods; heap->refs=1; heap->kind=kind;
    *out=&heap->iface;
    return S_OK;
}
static UINT STDMETHODCALLTYPE increment(ID3D12Device *raw,D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    (void)raw;
    return type==D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ? 32u : 64u;
}
static void STDMETHODCALLTYPE create_srv(ID3D12Device *raw,ID3D12Resource *resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    cpu_device *device=(cpu_device *)raw;
    assert(resource && desc->ViewDimension==D3D12_SRV_DIMENSION_BUFFER &&
        desc->Buffer.NumElements==64 && desc->Buffer.StructureByteStride==4);
    assert(handle.ptr==100u+64u*device->srvs);
    ++device->srvs;
}
static void STDMETHODCALLTYPE create_sampler(ID3D12Device *raw,const D3D12_SAMPLER_DESC *desc,
    D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    cpu_device *device=(cpu_device *)raw;
    assert(handle.ptr==1100u+32u*device->samplers && desc->MaxAnisotropy==1 &&
        desc->MinLOD==(device->samplers<16 ? 0.0f : 1.0f));
    ++device->samplers;
}
static void STDMETHODCALLTYPE create_rtv(ID3D12Device *raw,ID3D12Resource *resource,
    const D3D12_RENDER_TARGET_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    cpu_device *device=(cpu_device *)raw;
    assert(resource && desc->ViewDimension==D3D12_RTV_DIMENSION_TEXTURE2D && handle.ptr==2100);
    ++device->rtvs;
}
static HRESULT STDMETHODCALLTYPE feature(ID3D12Device *raw,D3D12_FEATURE kind,void *out,UINT size)
{
    cpu_device *device=(cpu_device *)raw;
    assert(kind==D3D12_FEATURE_FORMAT_SUPPORT && size==sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT));
    D3D12_FEATURE_DATA_FORMAT_SUPPORT *support=out;
    assert(support->Format==device->expected_vertex);
    ++device->format_queries;
    support->Support1=device->deny_vertex ? D3D12_FORMAT_SUPPORT1_NONE : D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
    return S_OK;
}
static CONST_VTBL ID3D12DeviceVtbl device_methods={.CreateRootSignature=create_root,.CreateGraphicsPipelineState=create_state,
    .CreateDescriptorHeap=create_heap,.GetDescriptorHandleIncrementSize=increment,.CreateShaderResourceView=create_srv,
    .CreateSampler=create_sampler,.CreateRenderTargetView=create_rtv,.CheckFeatureSupport=feature};
static ULONG STDMETHODCALLTYPE list_release(ID3D12GraphicsCommandList *raw)
{
    ++((cpu_list *)raw)->releases;
    return 0;
}
static void STDMETHODCALLTYPE set_heaps(ID3D12GraphicsCommandList *raw,UINT count,ID3D12DescriptorHeap *const *heaps)
{
    cpu_list *list=(cpu_list *)raw;
    assert(count==2 && heaps[0] && heaps[1]);
    list->heap_count=count; ++list->mutations;
}
static void STDMETHODCALLTYPE set_table(ID3D12GraphicsCommandList *raw,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    cpu_list *list=(cpu_list *)raw;
    assert(list->tables<4);
    list->table_indices[list->tables]=index;
    list->table_handles[list->tables++]=handle.ptr;
    ++list->mutations;
}
static void STDMETHODCALLTYPE barrier(ID3D12GraphicsCommandList *raw,UINT count,const D3D12_RESOURCE_BARRIER *values)
{
    cpu_list *list=(cpu_list *)raw;
    assert(count==1 && values->Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
    if (values->Transition.pResource==list->shared) {
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        assert((values->Transition.StateBefore==D3D12_RESOURCE_STATE_COPY_DEST && values->Transition.StateAfter==state) ||
            (values->Transition.StateBefore==state && values->Transition.StateAfter==D3D12_RESOURCE_STATE_COPY_DEST));
    }
    ++list->barriers; ++list->mutations;
}
static void STDMETHODCALLTYPE root_signature(ID3D12GraphicsCommandList *raw,ID3D12RootSignature *root)
{
    assert(root); ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE pipeline_state(ID3D12GraphicsCommandList *raw,ID3D12PipelineState *state)
{
    assert(state); ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE targets(ID3D12GraphicsCommandList *raw,UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE *target,
    BOOL contiguous,const D3D12_CPU_DESCRIPTOR_HANDLE *depth)
{
    assert(count==1 && target->ptr==2100 && !contiguous && !depth);
    ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE clear(ID3D12GraphicsCommandList *raw,D3D12_CPU_DESCRIPTOR_HANDLE target,
    const FLOAT value[4],UINT count,const D3D12_RECT *rects)
{
    cpu_list *list=(cpu_list *)raw;
    assert(target.ptr==2100 && !count && !rects);
    if (list->clears) for (size_t i=0;i<4;++i) assert(value[i]==0);
    else assert(value[0]==0.25f && value[1]==0.5f && value[2]==0.75f && value[3]==1);
    ++list->clears; ++list->mutations;
}
static void STDMETHODCALLTYPE viewport(ID3D12GraphicsCommandList *raw,UINT count,const D3D12_VIEWPORT *value)
{
    assert(count==1 && value->Width==4 && value->Height==4 && value->MaxDepth==1);
    ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE scissor(ID3D12GraphicsCommandList *raw,UINT count,const D3D12_RECT *value)
{
    assert(count==1 && value->right==4 && value->bottom==4); ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE topology(ID3D12GraphicsCommandList *raw,D3D_PRIMITIVE_TOPOLOGY value)
{
    assert(value==D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE draw(ID3D12GraphicsCommandList *raw,UINT vertices,UINT instances,UINT first,UINT base)
{
    assert(vertices==3 && instances==1 && !first && !base);
    ++((cpu_list *)raw)->draws; ++((cpu_list *)raw)->mutations;
}
static void STDMETHODCALLTYPE vertex_views(ID3D12GraphicsCommandList *raw,UINT first,UINT count,
    const D3D12_VERTEX_BUFFER_VIEW *views)
{
    cpu_list *list=(cpu_list *)raw;
    assert(!first && count==3 && !views[0].BufferLocation && !views[0].SizeInBytes && !views[0].StrideInBytes);
    assert(views[1].BufferLocation==0x100010 && views[1].SizeInBytes==128 && !views[1].StrideInBytes);
    assert(views[2].BufferLocation==0x100100 && views[2].SizeInBytes==64 && views[2].StrideInBytes==24);
    ++list->vertex_sets; ++list->mutations;
}
static D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE gpu_address(ID3D12Resource *resource)
{
    assert(resource);
    return 0x100000;
}
static CONST_VTBL ID3D12ResourceVtbl resource_methods={.GetGPUVirtualAddress=gpu_address};
static void STDMETHODCALLTYPE blend_constant(ID3D12GraphicsCommandList *raw,const FLOAT value[4])
{
    cpu_list *list=(cpu_list *)raw;
    assert(value[0]==0.25f && value[1]==0.5f && value[2]==0.75f && value[3]==1.0f);
    ++list->mutations;
}
static CONST_VTBL ID3D12GraphicsCommandListVtbl list_methods={.Release=list_release,.SetDescriptorHeaps=set_heaps,
    .SetGraphicsRootDescriptorTable=set_table,.ResourceBarrier=barrier,.SetGraphicsRootSignature=root_signature,
    .SetPipelineState=pipeline_state,.OMSetRenderTargets=targets,.ClearRenderTargetView=clear,
    .OMSetBlendFactor=blend_constant,.RSSetViewports=viewport,.RSSetScissorRects=scissor,.IASetPrimitiveTopology=topology,.IASetVertexBuffers=vertex_views,.DrawInstanced=draw};
static void vertex_formats(cpu_device *device,doe_dx_native_context *context,doe_dx_render_pipeline_desc *desc)
{
    static const struct { doe_dx_vertex_format typed; DXGI_FORMAT sdk; } cases[]={
        {DOE_DX_FLOAT32,DXGI_FORMAT_R32_FLOAT},
        {DOE_DX_FLOAT32X2,DXGI_FORMAT_R32G32_FLOAT},
        {DOE_DX_FLOAT32X3,DXGI_FORMAT_R32G32B32_FLOAT},
        {DOE_DX_FLOAT32X4,DXGI_FORMAT_R32G32B32A32_FLOAT},
        {DOE_DX_UINT32,DXGI_FORMAT_R32_UINT},
        {DOE_DX_UINT32X2,DXGI_FORMAT_R32G32_UINT},
        {DOE_DX_UINT32X3,DXGI_FORMAT_R32G32B32_UINT},
        {DOE_DX_UINT32X4,DXGI_FORMAT_R32G32B32A32_UINT},
        {DOE_DX_SINT32,DXGI_FORMAT_R32_SINT},
        {DOE_DX_SINT32X2,DXGI_FORMAT_R32G32_SINT},
        {DOE_DX_SINT32X3,DXGI_FORMAT_R32G32B32_SINT},
        {DOE_DX_SINT32X4,DXGI_FORMAT_R32G32B32A32_SINT},
        {DOE_DX_UINT8,DXGI_FORMAT_R8_UINT},
        {DOE_DX_SINT8,DXGI_FORMAT_R8_SINT},
        {DOE_DX_UNORM8,DXGI_FORMAT_R8_UNORM},
        {DOE_DX_SNORM8,DXGI_FORMAT_R8_SNORM},
        {DOE_DX_UINT8X2,DXGI_FORMAT_R8G8_UINT},
        {DOE_DX_SINT8X2,DXGI_FORMAT_R8G8_SINT},
        {DOE_DX_UNORM8X2,DXGI_FORMAT_R8G8_UNORM},
        {DOE_DX_SNORM8X2,DXGI_FORMAT_R8G8_SNORM},
        {DOE_DX_UINT8X4,DXGI_FORMAT_R8G8B8A8_UINT},
        {DOE_DX_SINT8X4,DXGI_FORMAT_R8G8B8A8_SINT},
        {DOE_DX_UNORM8X4,DXGI_FORMAT_R8G8B8A8_UNORM},
        {DOE_DX_SNORM8X4,DXGI_FORMAT_R8G8B8A8_SNORM},
        {DOE_DX_UINT16,DXGI_FORMAT_R16_UINT},
        {DOE_DX_SINT16,DXGI_FORMAT_R16_SINT},
        {DOE_DX_UNORM16,DXGI_FORMAT_R16_UNORM},
        {DOE_DX_SNORM16,DXGI_FORMAT_R16_SNORM},
        {DOE_DX_FLOAT16,DXGI_FORMAT_R16_FLOAT},
        {DOE_DX_UINT16X2,DXGI_FORMAT_R16G16_UINT},
        {DOE_DX_SINT16X2,DXGI_FORMAT_R16G16_SINT},
        {DOE_DX_UNORM16X2,DXGI_FORMAT_R16G16_UNORM},
        {DOE_DX_SNORM16X2,DXGI_FORMAT_R16G16_SNORM},
        {DOE_DX_FLOAT16X2,DXGI_FORMAT_R16G16_FLOAT},
        {DOE_DX_UINT16X4,DXGI_FORMAT_R16G16B16A16_UINT},
        {DOE_DX_SINT16X4,DXGI_FORMAT_R16G16B16A16_SINT},
        {DOE_DX_UNORM16X4,DXGI_FORMAT_R16G16B16A16_UNORM},
        {DOE_DX_SNORM16X4,DXGI_FORMAT_R16G16B16A16_SNORM},
        {DOE_DX_FLOAT16X4,DXGI_FORMAT_R16G16B16A16_FLOAT},
        {DOE_DX_UNORM10_10_10_2,DXGI_FORMAT_R10G10B10A2_UNORM},
        {DOE_DX_UNORM8X4_BGRA,DXGI_FORMAT_B8G8R8A8_UNORM},
    };
    UINT before=device->format_queries;
    const doe_dx_vertex_layout layouts[2]={{0,DOE_DX_PER_VERTEX},{0,DOE_DX_PER_INSTANCE}};
    doe_dx_vertex_attribute attribute={3,1,8,DOE_DX_FLOAT32};
    desc->vertex_buffers=layouts; desc->vertex_buffer_count=2; desc->attributes=&attribute; desc->attribute_count=1;
    for (size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        device->expected_vertex=cases[i].sdk;
        attribute.format=cases[i].typed;
        doe_dx_native_pipeline *pipeline=NULL;
        assert(doe_dx_native_render_create(context,desc,&pipeline,NULL)==DOE_DX_OK);
        assert(pipeline->vertex_buffers[1].stride==0 && pipeline->vertex_buffers[1].step==DOE_DX_PER_INSTANCE);
        doe_dx_native_pipeline_destroy(pipeline);
    }
    assert(device->format_queries==before+41);
    device->deny_vertex=true;
    doe_dx_native_pipeline *rejected=NULL;
    assert(doe_dx_native_render_create(context,desc,&rejected,NULL)==DOE_DX_UNSUPPORTED && !rejected);
    assert(!device->root.refs && !device->state.refs && device->format_queries==before+42);
}
int main(void)
{
    cpu_device device={.expected_cull=D3D12_CULL_MODE_NONE,.expected_sample_mask=UINT32_MAX,
        .expected_write_mask=15,.expected_topology=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE};
    device.iface.lpVtbl=&device_methods;
    doe_dx_native_context context={.device=&device.iface};
    doe_dx_binding_layout rows[64]; doe_dx_sampler_layout samplers[16];
    for (UINT i=0;i<64;++i) rows[i]=(doe_dx_binding_layout){i,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
    for (UINT i=0;i<16;++i) samplers[i]=(doe_dx_sampler_layout){i,0,false};
    unsigned char code=1;
    doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM,.front_counter_clockwise=true};
    for (size_t i=0;i<2;++i) desc.stages[i]=(doe_dx_dynamic_pipeline_desc){
        .resources={.buffers={&code,1,rows,64}},.samplers=samplers,.sampler_count=16};
    doe_dx_native_pipeline *pipeline=NULL;
    assert(doe_dx_native_render_create(&context,&desc,&pipeline,NULL)==DOE_DX_OK);
    assert(pipeline->render->resource_offset[1]==64 && pipeline->render->sampler_offset[1]==16);
    cpu_list list={0}; list.iface.lpVtbl=&list_methods;
    doe_dx_native_commands *commands=calloc(1,sizeof(*commands)); assert(commands);
    commands->context=&context; commands->list=&list.iface;
    ID3D12Resource color_resource={0},buffer_resource={.lpVtbl=&resource_methods};
    list.shared=&buffer_resource;
    doe_dx_native_texture color={.resource=&color_resource};
    color.description.Width=color.description.Height=4; color.description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    doe_dx_native_buffer buffer={.resource=&buffer_resource};
    doe_dx_native_render_draw packet={.pipeline=pipeline,.vertex_count=3,.instance_count=1,
        .viewport={0,0,4,4,0,1},.scissor={0,0,4,4},.blend_constant={0.25f,0.5f,0.75f,1}};
    for (size_t stage=0;stage<2;++stage) {
        packet.stages[stage].buffer_count=64; packet.stages[stage].sampler_count=16;
        for (size_t i=0;i<64;++i) packet.stages[stage].buffers[i]=(doe_dx_native_binding){&buffer,0,256};
        for (size_t i=0;i<16;++i) packet.stages[stage].samplers[i]=(doe_dx_sampler_desc){
            .min_filter=DOE_DX_NEAREST,.mag_filter=DOE_DX_NEAREST,.mip_filter=DOE_DX_NEAREST,
            .address_u=DOE_DX_CLAMP,.address_v=DOE_DX_CLAMP,.address_w=DOE_DX_CLAMP,
            .min_lod=(float)stage,.max_lod=8,.max_anisotropy=1};
    }
    packet.vertex_buffer_count=3;
    packet.vertices[1]=(doe_dx_native_binding){&buffer,16,128};
    packet.vertices[2]=(doe_dx_native_binding){&buffer,256,64};
    pipeline->vertex_buffers[1]=(doe_dx_vertex_layout){0,DOE_DX_PER_INSTANCE};
    pipeline->vertex_buffers[2]=(doe_dx_vertex_layout){24,DOE_DX_PER_VERTEX};
    const float clear_color[4]={0.25f,0.5f,0.75f,1};
    device.fail_heap=3;
    assert(doe_dx_native_render(commands,&color,true,clear_color,true,NULL,&packet,1,NULL)==DOE_DX_OUT_OF_MEMORY);
    assert(!list.mutations && !device.srvs && !device.samplers && !commands->descriptors);
    for (size_t i=0;i<3;++i) assert(!device.heaps[i].refs);
    device.fail_heap=0;
    assert(doe_dx_native_render(commands,&color,true,clear_color,true,NULL,&packet,1,NULL)==DOE_DX_OK);
    assert(device.srvs==128 && device.samplers==32 && list.heap_count==2 && list.tables==4 &&
        list.barriers==4 && list.clears==2 && list.draws==1 && list.vertex_sets==1);
    const UINT64 expected[4]={10000,20000,10000+64*64,20000+16*32};
    for (UINT i=0;i<4;++i) assert(list.table_indices[i]==i && list.table_handles[i]==expected[i]);
    doe_dx_native_commands_destroy(commands);
    assert(list.releases==1);
    for (size_t i=0;i<3;++i) assert(!device.heaps[i].refs);
    doe_dx_native_pipeline_destroy(pipeline);
    assert(!device.root.refs && !device.state.refs);
    vertex_formats(&device,&context,&desc);
    device.expected_vertex=DXGI_FORMAT_UNKNOWN;
    desc.vertex_buffers=NULL; desc.vertex_buffer_count=0; desc.attributes=NULL; desc.attribute_count=0;
    desc.masks_explicit=true; desc.sample_mask=0; desc.color_write_mask=0;
    device.expected_sample_mask=0; device.expected_write_mask=0;
    desc.blend_enabled=true; device.expected_blend=true;
    desc.color_blend=(doe_dx_blend_component){6,7,0};
    desc.alpha_blend=(doe_dx_blend_component){1,7,0};
    desc.cull=DOE_DX_CULL_BACK; device.expected_cull=D3D12_CULL_MODE_BACK;
    desc.topology=DOE_DX_LINE_STRIP; device.expected_topology=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    assert(doe_dx_native_render_create(&context,&desc,&pipeline,NULL)==DOE_DX_OK);
    assert(pipeline->render->topology==D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    doe_dx_native_pipeline_destroy(pipeline);
    assert(!device.root.refs && !device.state.refs);
    puts("PASS SDK CPU render: 41 queried vertex formats and negative IA support,  disjoint stage register visibility,128/32 packed descriptors,4tables/2heaps, OOM before recording, clear/discard/native draws and cleanup; no device/GPU");
    return 0;
}
