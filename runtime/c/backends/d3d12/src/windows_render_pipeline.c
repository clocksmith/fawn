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

static D3D12_STENCIL_OP stencil_operation(uint32_t value)
{
    static const D3D12_STENCIL_OP operations[]={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_ZERO,
        D3D12_STENCIL_OP_REPLACE,D3D12_STENCIL_OP_INCR_SAT,D3D12_STENCIL_OP_DECR_SAT,
        D3D12_STENCIL_OP_INVERT,D3D12_STENCIL_OP_INCR,D3D12_STENCIL_OP_DECR};
    return operations[value];
}
static D3D12_DEPTH_STENCILOP_DESC stencil_face(doe_dx_stencil_face face)
{
    return (D3D12_DEPTH_STENCILOP_DESC){stencil_operation(face.fail),stencil_operation(face.depth_fail),
        stencil_operation(face.pass),doe_dx_depth_compare(face.compare)};
}
static D3D12_BLEND blend_factor(uint32_t factor,bool alpha)
{
    switch (factor) {
    case 0: return D3D12_BLEND_ZERO;
    case 1: return D3D12_BLEND_ONE;
    case 2: return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
    case 3: return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    case 4: return alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
    case 5: return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
    case 6: return D3D12_BLEND_SRC_ALPHA;
    case 7: return D3D12_BLEND_INV_SRC_ALPHA;
    case 8: return D3D12_BLEND_DEST_ALPHA;
    case 9: return D3D12_BLEND_INV_DEST_ALPHA;
    case 10: return D3D12_BLEND_BLEND_FACTOR;
    case 11: return D3D12_BLEND_INV_BLEND_FACTOR;
    case 14: return alpha ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA_SAT;
    }
    return D3D12_BLEND_ZERO; /* Private owner rejected every other code. */
}
static D3D12_BLEND_OP blend_operation(uint32_t operation)
{
    switch (operation) {
    case 0: return D3D12_BLEND_OP_ADD;
    case 1: return D3D12_BLEND_OP_SUBTRACT;
    case 2: return D3D12_BLEND_OP_REV_SUBTRACT;
    case 3: return D3D12_BLEND_OP_MIN;
    case 4: return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD; /* Private owner rejected every other code. */
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
    const D3D12_ROOT_SIGNATURE_DESC root={table_count,table_count ? parameters : NULL,0,NULL,
        desc->attribute_count ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT : D3D12_ROOT_SIGNATURE_FLAG_NONE};
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
    pipeline->render->stencil_test=desc->stencil_test;
    D3D12_INPUT_ELEMENT_DESC attributes[DOE_DX_MAX_VERTEX_ATTRIBUTES]={0};
    doe_dx_status status=doe_dx_input_layout_rows(context,desc->vertex_buffers,desc->vertex_buffer_count,
        desc->attributes,desc->attribute_count,pipeline,attributes,error);
    if (status==DOE_DX_OK) status=render_root(context,desc,pipeline,error);
    if (status==DOE_DX_OK) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state={0};
        state.pRootSignature=pipeline->root;
        state.VS=(D3D12_SHADER_BYTECODE){desc->stages[0].resources.buffers.bytecode,desc->stages[0].resources.buffers.bytecode_size};
        state.PS=(D3D12_SHADER_BYTECODE){desc->stages[1].resources.buffers.bytecode,desc->stages[1].resources.buffers.bytecode_size};
        doe_dx_graphics_defaults(&state,desc->color_format);
        state.SampleDesc.Count=desc->samples ? desc->samples : 1;
        state.RasterizerState.MultisampleEnable=state.SampleDesc.Count>1;
        state.BlendState.AlphaToCoverageEnable=desc->alpha_to_coverage;
        state.RTVFormats[0]=DXGI_FORMAT_UNKNOWN;
        state.NumRenderTargets=doe_dx_render_target_count(desc);
        state.BlendState.IndependentBlendEnable=TRUE;
        for (uint32_t i=0;i<state.NumRenderTargets;++i) {
            doe_dx_render_target target=doe_dx_render_target_at(desc,i);
            state.RTVFormats[i]=target.absent ? DXGI_FORMAT_UNKNOWN : doe_dx_texture_format(target.format);
            D3D12_RENDER_TARGET_BLEND_DESC *blend=&state.BlendState.RenderTarget[i];
            *blend=state.BlendState.RenderTarget[0];
            blend->RenderTargetWriteMask=target.absent ? 0 : (UINT8)target.write_mask;
            blend->BlendEnable=target.blend_enabled;
            if (target.blend_enabled) {
                blend->SrcBlend=blend_factor(target.color_blend.source,false);
                blend->DestBlend=blend_factor(target.color_blend.destination,false);
                blend->BlendOp=blend_operation(target.color_blend.operation);
                blend->SrcBlendAlpha=blend_factor(target.alpha_blend.source,true);
                blend->DestBlendAlpha=blend_factor(target.alpha_blend.destination,true);
                blend->BlendOpAlpha=blend_operation(target.alpha_blend.operation);
            }
        }
        switch (desc->topology) {
        case DOE_DX_TRIANGLES:
            pipeline->render->topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case DOE_DX_TRIANGLE_STRIP:
            pipeline->render->topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        case DOE_DX_LINES:
            state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            pipeline->render->topology=D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case DOE_DX_LINE_STRIP:
            state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            pipeline->render->topology=D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case DOE_DX_POINTS:
            state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            pipeline->render->topology=D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        }
        if (desc->strip_indexed) state.IBStripCutValue=desc->strip_index_format==DOE_DX_INDEX_UINT16 ?
            D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
        if (desc->depth_enabled) {
            state.DepthStencilState.DepthEnable=desc->depth_state_explicit ? desc->depth_test : TRUE;
            state.DepthStencilState.DepthWriteMask=desc->depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            state.DepthStencilState.DepthFunc=doe_dx_depth_compare(desc->depth_compare);
            state.DSVFormat=doe_dx_texture_format(desc->depth_state_explicit ? desc->depth_format : DOE_DX_D32_FLOAT);
            if (desc->stencil_test) {
                state.DepthStencilState.StencilEnable=TRUE;
                state.DepthStencilState.StencilReadMask=(UINT8)desc->stencil_read_mask;
                state.DepthStencilState.StencilWriteMask=(UINT8)desc->stencil_write_mask;
                state.DepthStencilState.FrontFace=stencil_face(desc->stencil_front);
                state.DepthStencilState.BackFace=stencil_face(desc->stencil_back);
            }
            state.RasterizerState.DepthBias=desc->depth_bias;
            state.RasterizerState.SlopeScaledDepthBias=desc->depth_bias_slope;
            state.RasterizerState.DepthBiasClamp=desc->depth_bias_clamp;
        }
        state.RasterizerState.DepthClipEnable=!desc->unclipped_depth;
        state.RasterizerState.FrontCounterClockwise=desc->front_counter_clockwise;
        switch (desc->cull) {
        case DOE_DX_CULL_NONE: state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; break;
        case DOE_DX_CULL_FRONT: state.RasterizerState.CullMode=D3D12_CULL_MODE_FRONT; break;
        case DOE_DX_CULL_BACK: state.RasterizerState.CullMode=D3D12_CULL_MODE_BACK; break;
        }
        if (desc->masks_explicit) state.SampleMask=desc->sample_mask;
        state.InputLayout=(D3D12_INPUT_LAYOUT_DESC){desc->attribute_count ? attributes : NULL,(UINT)desc->attribute_count};
        HRESULT result=ID3D12Device_CreateGraphicsPipelineState(context->device,&state,
            &IID_ID3D12PipelineState,(void **)&pipeline->state);
        status=doe_dx_hresult(result,"ID3D12Device.CreateGraphicsPipelineState.render",error);
    }
    if (status!=DOE_DX_OK) { doe_dx_native_pipeline_destroy(pipeline); return status; }
    *out=pipeline;
    return DOE_DX_OK;
}
#endif
