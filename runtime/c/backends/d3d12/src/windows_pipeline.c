/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static D3D12_TEXTURE_ADDRESS_MODE address_mode(doe_dx_address address) {
    switch (address) {
        case DOE_DX_CLAMP: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case DOE_DX_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case DOE_DX_MIRROR: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP; /* Already validated by the owner. */
}

void doe_dx_native_pipeline_destroy(doe_dx_native_pipeline *pipeline) {
    if (!pipeline) return;
    if (pipeline->state) ID3D12PipelineState_Release(pipeline->state);
    if (pipeline->root) ID3D12RootSignature_Release(pipeline->root);
    free(pipeline);
}

doe_dx_status doe_dx_create_root(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline *pipeline, doe_dx_error *error) {
    D3D12_DESCRIPTOR_RANGE ranges[DOE_DX_MAX_BINDINGS] = {0};
    for (size_t index = 0; index < desc->buffers.binding_count; index++) {
        const doe_dx_binding_layout *binding = &desc->buffers.bindings[index];
        ranges[index].RangeType = binding->access == DOE_DX_READ_ONLY ?
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[index].NumDescriptors = 1;
        ranges[index].BaseShaderRegister = binding->shader_register;
        ranges[index].RegisterSpace = binding->register_space;
        ranges[index].OffsetInDescriptorsFromTableStart = (UINT)index;
        pipeline->bindings[index] = *binding;
    }
    for (size_t index = 0; index < desc->texture_count; index++) {
        const doe_dx_texture_binding_layout *binding = &desc->textures[index];
        size_t slot = desc->buffers.binding_count + index;
        ranges[slot].RangeType = binding->access == DOE_DX_READ_ONLY ?
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[slot].NumDescriptors = 1;
        ranges[slot].BaseShaderRegister = binding->shader_register;
        ranges[slot].RegisterSpace = binding->register_space;
        ranges[slot].OffsetInDescriptorsFromTableStart = (UINT)slot;
        pipeline->textures[index] = *binding;
    }
    D3D12_STATIC_SAMPLER_DESC samplers[DOE_DX_MAX_BINDINGS] = {0};
    for (size_t index = 0; index < desc->sampler_count; index++) {
        const doe_dx_static_sampler *sampler = &desc->samplers[index];
        samplers[index].ShaderRegister = sampler->shader_register;
        samplers[index].RegisterSpace = sampler->register_space;
        samplers[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        samplers[index].Filter = sampler->filter == DOE_DX_LINEAR ?
            D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[index].AddressU = address_mode(sampler->address_u);
        samplers[index].AddressV = address_mode(sampler->address_v);
        samplers[index].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[index].MaxAnisotropy = 1;
        samplers[index].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[index].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    }
    size_t binding_count = desc->buffers.binding_count + desc->texture_count;
    D3D12_ROOT_PARAMETER parameter = {0};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = (UINT)binding_count;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root = {0};
    root.NumParameters = binding_count ? 1 : 0;
    root.pParameters = binding_count ? &parameter : NULL;
    root.NumStaticSamplers = (UINT)desc->sampler_count;
    root.pStaticSamplers = desc->sampler_count ? samplers : NULL;
    ID3DBlob *blob = NULL;
    ID3DBlob *diagnostic = NULL;
    const char *operation = "D3D12SerializeRootSignature";
    HRESULT result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
        &blob, &diagnostic);
    if (diagnostic) ID3D10Blob_Release(diagnostic);
    if (FAILED(result)) goto fail;
    operation = "ID3D12Device.CreateRootSignature";
    result = ID3D12Device_CreateRootSignature(context->device, 0,
        ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
        &IID_ID3D12RootSignature, (void **)&pipeline->root);
    if (FAILED(result)) goto fail;
    ID3D10Blob_Release(blob);
    return doe_dx_fail(DOE_DX_OK, 0, "native.root.create", error);
fail:
    if (blob) ID3D10Blob_Release(blob);
    return doe_dx_hresult(result, operation, error);
}

doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error) {
    doe_dx_native_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.pipeline.allocate", error);
    doe_dx_status status = doe_dx_create_root(context, desc, pipeline, error);
    if (status == DOE_DX_OK) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC state = {0};
        state.pRootSignature = pipeline->root;
        state.CS.pShaderBytecode = desc->buffers.bytecode;
        state.CS.BytecodeLength = desc->buffers.bytecode_size;
        HRESULT result = ID3D12Device_CreateComputePipelineState(context->device, &state,
            &IID_ID3D12PipelineState, (void **)&pipeline->state);
        status = doe_dx_hresult(result, "ID3D12Device.CreateComputePipelineState", error);
    }
    if (status != DOE_DX_OK) {
        doe_dx_native_pipeline_destroy(pipeline);
        return status;
    }
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "native.pipeline.create", error);
}
#endif
