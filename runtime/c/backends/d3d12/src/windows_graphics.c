/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

doe_dx_status doe_dx_native_graphics_create(doe_dx_native_context *context,
    const doe_dx_mesh_pipeline_desc *mesh, doe_dx_native_pipeline **out, doe_dx_error *error) {
    doe_dx_native_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.graphics.allocate", error);
    const doe_dx_graphics_pipeline_desc *desc = &mesh->graphics;
    D3D12_INPUT_ELEMENT_DESC attributes[DOE_DX_MAX_VERTEX_ATTRIBUTES] = {0};
    doe_dx_status status = doe_dx_input_layout(context, mesh, pipeline, attributes, error);
    if (status != DOE_DX_OK) goto fail;
    status = doe_dx_create_root(context, &desc->fragment, pipeline, mesh->attribute_count != 0, error);
    if (status != DOE_DX_OK) goto fail;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state = {0};
    state.pRootSignature = pipeline->root;
    state.VS.pShaderBytecode = desc->vertex_bytecode;
    state.VS.BytecodeLength = desc->vertex_bytecode_size;
    state.PS.pShaderBytecode = desc->fragment.buffers.bytecode;
    state.PS.BytecodeLength = desc->fragment.buffers.bytecode_size;
    doe_dx_graphics_defaults(&state,desc->color_format);
    state.InputLayout.NumElements = (UINT)mesh->attribute_count;
    state.InputLayout.pInputElementDescs = mesh->attribute_count ? attributes : NULL;
    state.DepthStencilState.DepthEnable = mesh->depth_enabled ? TRUE : FALSE;
    state.DepthStencilState.DepthWriteMask = mesh->depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    state.DepthStencilState.DepthFunc = mesh->depth_enabled ? doe_dx_depth_compare(mesh->depth_compare) : D3D12_COMPARISON_FUNC_ALWAYS;
    state.DSVFormat = mesh->depth_enabled ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
    state.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    state.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    state.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    state.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    state.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    state.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    state.DepthStencilState.BackFace = state.DepthStencilState.FrontFace;
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

doe_dx_status doe_dx_prepare_color_target(doe_dx_native_commands *commands, doe_dx_native_texture *color,
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

doe_dx_status doe_dx_prepare_depth_target(doe_dx_native_commands *commands, doe_dx_native_texture *depth,
    doe_dx_descriptor_use **out, D3D12_CPU_DESCRIPTOR_HANDLE *handle, doe_dx_error *error) {
    doe_dx_descriptor_use *use = calloc(1, sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "dsv.allocate", error);
    D3D12_DESCRIPTOR_HEAP_DESC desc = {0};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 1;
    HRESULT result = ID3D12Device_CreateDescriptorHeap(commands->context->device, &desc,
        &IID_ID3D12DescriptorHeap, (void **)&use->heap);
    if (FAILED(result)) {
        free(use);
        return doe_dx_hresult(result, "CreateDescriptorHeap.dsv", error);
    }
    use->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->heap, handle);
    D3D12_DEPTH_STENCIL_VIEW_DESC view = {0};
    view.Format = DXGI_FORMAT_D32_FLOAT;
    view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateDepthStencilView(commands->context->device, depth->resource, &view, *handle);
    *out = use;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_draw(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_texture *color, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *mesh, doe_dx_error *error) {
    const doe_dx_draw_desc *draw = &mesh->draw;
    doe_dx_descriptor_use *rtv = NULL, *dsv = NULL, *bindings = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE target = {0}, depth = {0};
    doe_dx_status status = doe_dx_prepare_color_target(commands, color, &rtv, &target, error);
    if (status != DOE_DX_OK) return status;
    if (geometry->depth) {
        status = doe_dx_prepare_depth_target(commands, geometry->depth, &dsv, &depth, error);
        if (status != DOE_DX_OK) goto fail;
    }
    if (resources->buffer_count + resources->texture_count + resources->sampler_count) {
        status = doe_dx_prepare_descriptors(commands, pipeline, resources, &bindings, error);
        if (status != DOE_DX_OK) goto fail;
        bindings->next = commands->descriptors;
        commands->descriptors = bindings;
    }
    if (dsv) {
        dsv->next = commands->descriptors;
        commands->descriptors = dsv;
    }
    rtv->next = commands->descriptors;
    commands->descriptors = rtv;
    /* All fallible preparation is complete; no rejected call partially records. */
    ID3D12GraphicsCommandList *list = commands->list;
    doe_dx_transition(list, color->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    doe_dx_geometry_transition(commands, geometry, mesh, false);
    doe_dx_transition_resources(commands, pipeline, resources, true, false);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, pipeline->root);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline->state);
    if (bindings) doe_dx_bind_descriptors(commands,pipeline,bindings,true);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &target, FALSE, dsv ? &depth : NULL);
    if (mesh->clear_depth) ID3D12GraphicsCommandList_ClearDepthStencilView(list, depth, D3D12_CLEAR_FLAG_DEPTH, mesh->depth_clear, 0, 0, NULL);
    if (draw->clear) ID3D12GraphicsCommandList_ClearRenderTargetView(list, target, draw->clear_color, 0, NULL);
    D3D12_VIEWPORT viewport = {0};
    viewport.Width = (FLOAT)color->description.Width;
    viewport.Height = (FLOAT)color->description.Height;
    viewport.MaxDepth = 1;
    D3D12_RECT scissor = {0, 0, (LONG)color->description.Width, (LONG)color->description.Height};
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    doe_dx_bind_geometry(commands, pipeline, geometry, mesh);
    if (mesh->indices) ID3D12GraphicsCommandList_DrawIndexedInstanced(list, mesh->indices->count,
        draw->instance_count, mesh->indices->first, 0, draw->first_instance);
    else ID3D12GraphicsCommandList_DrawInstanced(list, draw->vertex_count, draw->instance_count,
        draw->first_vertex, draw->first_instance);
    doe_dx_transition_resources(commands, pipeline, resources, true, true);
    doe_dx_geometry_transition(commands, geometry, mesh, true);
    doe_dx_transition(list, color->resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    return doe_dx_fail(DOE_DX_OK, 0, "native.draw", error);
fail:
    if (dsv) {
        doe_dx_release_descriptors(dsv);
    }
    doe_dx_release_descriptors(rtv);
    return status;
}
#endif
