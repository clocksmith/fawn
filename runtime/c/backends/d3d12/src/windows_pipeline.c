/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#include "windows_cache.h"
#ifdef _WIN32

bool doe_dx_descriptor_range_type(doe_dx_descriptor_kind kind,D3D12_DESCRIPTOR_RANGE_TYPE *out)
{
    switch (kind) {
    case DOE_DX_DESCRIPTOR_CBV: *out=D3D12_DESCRIPTOR_RANGE_TYPE_CBV; return true;
    case DOE_DX_DESCRIPTOR_SRV: *out=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; return true;
    case DOE_DX_DESCRIPTOR_UAV: *out=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; return true;
    case DOE_DX_DESCRIPTOR_INVALID: return false;
    }
    return false;
}

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
    free(pipeline->render);
    free(pipeline);
}

static doe_dx_status create_root(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc,const doe_dx_dynamic_pipeline_desc *dynamic, doe_dx_native_pipeline *pipeline,
    bool input_assembler, doe_dx_error *error) {
    D3D12_DESCRIPTOR_RANGE ranges[DOE_DX_MAX_BINDINGS] = {0};
    for (size_t index = 0; index < desc->buffers.binding_count; index++) {
        const doe_dx_binding_layout *binding = &desc->buffers.bindings[index];
        doe_dx_descriptor_kind kind = doe_dx_buffer_descriptor_kind(binding);
        if (!doe_dx_descriptor_range_type(kind,&ranges[index].RangeType))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "native.root.binding_kind", error);
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
    D3D12_STATIC_SAMPLER_DESC samplers[DOE_DX_MAX_STATIC_SAMPLERS] = {0};
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
    D3D12_ROOT_PARAMETER parameters[4] = {0};
    UINT table_count = 0;
    pipeline->resource_table = UINT32_MAX;
    pipeline->sampler_table = UINT32_MAX;
    if (binding_count) {
        pipeline->resource_table = table_count;
        D3D12_ROOT_PARAMETER *parameter = &parameters[table_count++];
        parameter->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter->DescriptorTable.NumDescriptorRanges = (UINT)binding_count;
        parameter->DescriptorTable.pDescriptorRanges = ranges;
        parameter->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_DESCRIPTOR_RANGE sampler_ranges[DOE_DX_MAX_DYNAMIC_SAMPLERS] = {0};
    size_t dynamic_count = dynamic ? dynamic->sampler_count : 0;
    if (dynamic_count) {
        pipeline->sampler_table = table_count;
        for (size_t index = 0; index < dynamic_count; ++index) {
            sampler_ranges[index] = (D3D12_DESCRIPTOR_RANGE){D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,1,
                dynamic->samplers[index].shader_register,dynamic->samplers[index].register_space,(UINT)index};
        }
        D3D12_ROOT_PARAMETER *parameter = &parameters[table_count++];
        parameter->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter->DescriptorTable.NumDescriptorRanges = (UINT)dynamic_count;
        parameter->DescriptorTable.pDescriptorRanges = sampler_ranges;
        parameter->ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    pipeline->immediate_root=UINT32_MAX;
    if (dynamic && dynamic->immediate_bytes) {
        pipeline->immediate_root=table_count;
        pipeline->immediate_words=dynamic->immediate_bytes/4;
        D3D12_ROOT_PARAMETER *parameter=&parameters[table_count++];
        parameter->ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter->ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        parameter->Constants=(D3D12_ROOT_CONSTANTS){1,7,pipeline->immediate_words};
    }
    pipeline->storage_sizes_root=UINT32_MAX;
    if (dynamic && dynamic->logical_size_count) {
        pipeline->storage_sizes_root=table_count;
        D3D12_ROOT_PARAMETER *parameter=&parameters[table_count++];
        parameter->ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameter->ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        parameter->Descriptor=(D3D12_ROOT_DESCRIPTOR){2,7};
    }
    D3D12_ROOT_SIGNATURE_DESC root = {0};
    if (input_assembler) root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    root.NumParameters = table_count;
    root.pParameters = table_count ? parameters : NULL;
    root.NumStaticSamplers = (UINT)desc->sampler_count;
    root.pStaticSamplers = desc->sampler_count ? samplers : NULL;
    ID3DBlob *blob = NULL;
    ID3DBlob *diagnostic = NULL;
    const char *operation = "D3D12SerializeRootSignature";
    HRESULT result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
        &blob, &diagnostic);
    if (diagnostic) ID3D10Blob_Release(diagnostic);
    if (FAILED(result)) goto fail;
    doe_dx_cache_root_key(ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),pipeline->root_digest);
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

doe_dx_status doe_dx_create_root(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc,doe_dx_native_pipeline *pipeline,
    bool input_assembler,doe_dx_error *error) {
    return create_root(context,desc,NULL,pipeline,input_assembler,error);
}

static doe_dx_status create_compute(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc,const doe_dx_dynamic_pipeline_desc *dynamic,
    doe_dx_native_pipeline **out,doe_dx_error *error) {
    doe_dx_native_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.pipeline.allocate", error);
    doe_dx_status status = create_root(context,desc,dynamic,pipeline,false,error);
    if (status == DOE_DX_OK) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC state = {0};
        state.pRootSignature = pipeline->root;
        state.CS.pShaderBytecode = desc->buffers.bytecode;
        state.CS.BytecodeLength = desc->buffers.bytecode_size;
        status=doe_dx_cached_compute(context,&state,pipeline->root_digest,&pipeline->state,error);
    }
    if (status != DOE_DX_OK) {
        doe_dx_native_pipeline_destroy(pipeline);
        return status;
    }
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "native.pipeline.create", error);
}
doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc,doe_dx_native_pipeline **out,doe_dx_error *error) {
    return create_compute(context,desc,NULL,out,error);
}
doe_dx_status doe_dx_native_pipeline_create_dynamic(doe_dx_native_context *context,
    const doe_dx_dynamic_pipeline_desc *desc,doe_dx_native_pipeline **out,doe_dx_error *error) {
    return create_compute(context,&desc->resources,desc,out,error);
}
#endif
