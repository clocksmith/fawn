/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32
_Static_assert(D3D12_VS_OUTPUT_REGISTER_COUNT * D3D12_VS_OUTPUT_REGISTER_COMPONENTS == 128 &&
    D3D12_PS_INPUT_REGISTER_COUNT == D3D12_VS_OUTPUT_REGISTER_COUNT,"public stage component admission matches SDK guarantees");

void doe_dx_graphics_defaults(D3D12_GRAPHICS_PIPELINE_STATE_DESC *state,doe_dx_format format)
{
    D3D12_RENDER_TARGET_BLEND_DESC *blend=&state->BlendState.RenderTarget[0];
    blend->SrcBlend=D3D12_BLEND_ONE;
    blend->DestBlend=D3D12_BLEND_ZERO;
    blend->BlendOp=D3D12_BLEND_OP_ADD;
    blend->SrcBlendAlpha=D3D12_BLEND_ONE;
    blend->DestBlendAlpha=D3D12_BLEND_ZERO;
    blend->BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend->LogicOp=D3D12_LOGIC_OP_NOOP;
    blend->RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    state->SampleMask=UINT32_MAX;
    state->RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    state->RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    state->RasterizerState.DepthClipEnable=TRUE;
    state->DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    state->DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    state->DepthStencilState.StencilReadMask=D3D12_DEFAULT_STENCIL_READ_MASK;
    state->DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_WRITE_MASK;
    state->DepthStencilState.FrontFace=(D3D12_DEPTH_STENCILOP_DESC){D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    state->DepthStencilState.BackFace=state->DepthStencilState.FrontFace;
    state->PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state->NumRenderTargets=1;
    state->RTVFormats[0]=doe_dx_texture_format(format);
    state->SampleDesc.Count=1;
}

static doe_dx_status render_root(doe_dx_native_context *context,
    const doe_dx_render_pipeline_desc *desc,doe_dx_native_pipeline *pipeline,doe_dx_error *error)
{
    D3D12_DESCRIPTOR_RANGE resources[DOE_DX_RENDER_DESCRIPTORS],samplers[DOE_DX_RENDER_SAMPLERS];
    D3D12_ROOT_PARAMETER parameters[4]={0};
    size_t resource_count=0,sampler_count=0;
    UINT table_count=0;
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        doe_dx_binding_plan *plan=&pipeline->render->stages[stage];
        doe_dx_binding_plan_copy(plan,&desc->stages[stage]);
        size_t first=resource_count;
        pipeline->render->resource_offset[stage]=(uint32_t)first;
        pipeline->render->sampler_offset[stage]=(uint32_t)sampler_count;
        pipeline->render->resource_table[stage]=pipeline->render->sampler_table[stage]=UINT32_MAX;
        for (size_t i=0;i<plan->binding_count+plan->texture_count;++i) {
            bool buffer=i<plan->binding_count;
            const doe_dx_binding_layout *binding=buffer ? &plan->bindings[i] : NULL;
            const doe_dx_texture_binding_layout *texture=buffer ? NULL : &plan->textures[i-plan->binding_count];
            doe_dx_descriptor_kind kind=buffer ? doe_dx_buffer_descriptor_kind(binding) :
                texture->access==DOE_DX_READ_ONLY ? DOE_DX_DESCRIPTOR_SRV : DOE_DX_DESCRIPTOR_UAV;
            D3D12_DESCRIPTOR_RANGE range={.NumDescriptors=1,
                .BaseShaderRegister=buffer ? binding->shader_register : texture->shader_register,
                .RegisterSpace=buffer ? binding->register_space : texture->register_space,
                .OffsetInDescriptorsFromTableStart=(UINT)i};
            if (!doe_dx_descriptor_range_type(kind,&range.RangeType))
                return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.root.kind",error);
            resources[resource_count++]=range;
        }
        D3D12_SHADER_VISIBILITY visibility=stage==0 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
        if (resource_count!=first) {
            pipeline->render->resource_table[stage]=table_count;
            D3D12_ROOT_PARAMETER *parameter=&parameters[table_count++];
            parameter->ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter->ShaderVisibility=visibility;
            parameter->DescriptorTable=(D3D12_ROOT_DESCRIPTOR_TABLE){(UINT)(resource_count-first),&resources[first]};
        }
        first=sampler_count;
        for (size_t i=0;i<plan->sampler_count;++i)
            samplers[sampler_count++]=(D3D12_DESCRIPTOR_RANGE){D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,
                plan->samplers[i].shader_register,plan->samplers[i].register_space,(UINT)i};
        if (sampler_count!=first) {
            pipeline->render->sampler_table[stage]=table_count;
            D3D12_ROOT_PARAMETER *parameter=&parameters[table_count++];
            parameter->ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter->ShaderVisibility=visibility;
            parameter->DescriptorTable=(D3D12_ROOT_DESCRIPTOR_TABLE){(UINT)(sampler_count-first),&samplers[first]};
        }
    }
    const D3D12_ROOT_SIGNATURE_DESC root={table_count,table_count ? parameters : NULL,0,NULL,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob=NULL,*diagnostic=NULL;
    HRESULT result=D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&diagnostic);
    if (diagnostic) ID3D10Blob_Release(diagnostic);
    if (FAILED(result)) return doe_dx_hresult(result,"D3D12SerializeRootSignature.render",error);
    result=ID3D12Device_CreateRootSignature(context->device,0,ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob),&IID_ID3D12RootSignature,(void **)&pipeline->root);
    ID3D10Blob_Release(blob);
    return doe_dx_hresult(result,"ID3D12Device.CreateRootSignature.render",error);
}

doe_dx_status doe_dx_native_render_create(doe_dx_native_context *context,
    const doe_dx_render_pipeline_desc *desc,doe_dx_native_pipeline **out,doe_dx_error *error)
{
    doe_dx_native_pipeline *pipeline=calloc(1,sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.native.allocate",error);
    pipeline->render=calloc(1,sizeof(*pipeline->render));
    if (!pipeline->render) {
        free(pipeline);
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.native.layout",error);
    }
    doe_dx_status status=render_root(context,desc,pipeline,error);
    if (status==DOE_DX_OK) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state={0};
        state.pRootSignature=pipeline->root;
        state.VS=(D3D12_SHADER_BYTECODE){desc->stages[0].resources.buffers.bytecode,desc->stages[0].resources.buffers.bytecode_size};
        state.PS=(D3D12_SHADER_BYTECODE){desc->stages[1].resources.buffers.bytecode,desc->stages[1].resources.buffers.bytecode_size};
        doe_dx_graphics_defaults(&state,desc->color_format);
        state.RasterizerState.FrontCounterClockwise=desc->front_counter_clockwise;
        HRESULT result=ID3D12Device_CreateGraphicsPipelineState(context->device,&state,
            &IID_ID3D12PipelineState,(void **)&pipeline->state);
        status=doe_dx_hresult(result,"ID3D12Device.CreateGraphicsPipelineState.render",error);
    }
    if (status!=DOE_DX_OK) { doe_dx_native_pipeline_destroy(pipeline); return status; }
    *out=pipeline;
    return DOE_DX_OK;
}
#endif
