/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

doe_dx_status doe_dx_native_graphics_create(doe_dx_native_context *context,
    const doe_dx_graphics_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error) {
    doe_dx_native_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.graphics.allocate", error);
    doe_dx_status status = doe_dx_create_root(context, &desc->fragment, pipeline, error);
    if (status != DOE_DX_OK) goto fail;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state = {0};
    state.pRootSignature = pipeline->root;
    state.VS.pShaderBytecode = desc->vertex_bytecode;
    state.VS.BytecodeLength = desc->vertex_bytecode_size;
    state.PS.pShaderBytecode = desc->fragment.buffers.bytecode;
    state.PS.BytecodeLength = desc->fragment.buffers.bytecode_size;
    D3D12_RENDER_TARGET_BLEND_DESC *blend = &state.BlendState.RenderTarget[0];
    blend->SrcBlend = D3D12_BLEND_ONE;
    blend->DestBlend = D3D12_BLEND_ZERO;
    blend->BlendOp = D3D12_BLEND_OP_ADD;
    blend->SrcBlendAlpha = D3D12_BLEND_ONE;
    blend->DestBlendAlpha = D3D12_BLEND_ZERO;
    blend->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend->LogicOp = D3D12_LOGIC_OP_NOOP;
    blend->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    state.SampleMask = UINT32_MAX;
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    state.RasterizerState.DepthClipEnable = TRUE;
    state.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    state.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    state.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    state.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    state.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    state.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    state.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    state.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    state.DepthStencilState.BackFace = state.DepthStencilState.FrontFace;
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets = 1;
    state.RTVFormats[0] = doe_dx_texture_format(desc->color_format);
    state.SampleDesc.Count = 1;
    HRESULT result = ID3D12Device_CreateGraphicsPipelineState(context->device, &state,
        &IID_ID3D12PipelineState, (void **)&pipeline->state);
    status = doe_dx_hresult(result, "ID3D12Device.CreateGraphicsPipelineState", error);
    if (status != DOE_DX_OK) goto fail;
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "native.graphics.create", error);
fail:
    doe_dx_native_pipeline_destroy(pipeline);
    return status;
}

static doe_dx_status prepare_target(doe_dx_native_commands *commands, doe_dx_native_texture *color,
    doe_dx_descriptor_use **out, D3D12_CPU_DESCRIPTOR_HANDLE *handle, doe_dx_error *error) {
    doe_dx_descriptor_use *use = calloc(1, sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "rtv.allocate", error);
    D3D12_DESCRIPTOR_HEAP_DESC description = {0};
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    description.NumDescriptors = 1;
    ID3D12Device *device = commands->context->device;
    HRESULT result = ID3D12Device_CreateDescriptorHeap(device, &description,
        &IID_ID3D12DescriptorHeap, (void **)&use->heap);
    if (FAILED(result)) {
        free(use);
        return doe_dx_hresult(result, "CreateDescriptorHeap.rtv", error);
    }
    use->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->heap, handle);
    D3D12_RENDER_TARGET_VIEW_DESC view = {0};
    view.Format = color->description.Format;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateRenderTargetView(device, color->resource, &view, *handle);
    *out = use;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_draw(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_texture *color, const doe_dx_draw_desc *draw, doe_dx_error *error) {
    doe_dx_descriptor_use *rtv = NULL, *bindings = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE target;
    doe_dx_status status = prepare_target(commands, color, &rtv, &target, error);
    if (status != DOE_DX_OK) return status;
    if (resources->buffer_count + resources->texture_count) {
        status = doe_dx_prepare_descriptors(commands, pipeline, resources, &bindings, error);
        if (status != DOE_DX_OK) {
            ID3D12DescriptorHeap_Release(rtv->heap);
            free(rtv);
            return status;
        }
        bindings->next = commands->descriptors;
        commands->descriptors = bindings;
    }
    rtv->next = commands->descriptors;
    commands->descriptors = rtv;
    /* All fallible preparation is complete; no rejected call partially records. */
    ID3D12GraphicsCommandList *list = commands->list;
    doe_dx_transition(list, color->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    doe_dx_transition_resources(commands, pipeline, resources, true, false);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, pipeline->root);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline->state);
    if (bindings) {
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &bindings->heap);
        D3D12_GPU_DESCRIPTOR_HANDLE handle;
        bindings->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(bindings->heap, &handle);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, handle);
    }
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &target, FALSE, NULL);
    if (draw->clear) ID3D12GraphicsCommandList_ClearRenderTargetView(list, target, draw->clear_color, 0, NULL);
    D3D12_VIEWPORT viewport = {0};
    viewport.Width = (FLOAT)color->description.Width;
    viewport.Height = (FLOAT)color->description.Height;
    viewport.MaxDepth = 1;
    D3D12_RECT scissor = {0, 0, (LONG)color->description.Width, (LONG)color->description.Height};
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(list, draw->vertex_count, draw->instance_count,
        draw->first_vertex, draw->first_instance);
    doe_dx_transition_resources(commands, pipeline, resources, true, true);
    doe_dx_transition(list, color->resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    return doe_dx_fail(DOE_DX_OK, 0, "native.draw", error);
}
#endif
