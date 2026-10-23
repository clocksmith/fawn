/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

void doe_dx_native_buffer_destroy(doe_dx_native_buffer *buffer) {
    if (!buffer) return;
    if (buffer->resource) ID3D12Resource_Release(buffer->resource);
    free(buffer);
}

doe_dx_status doe_dx_native_buffer_create(doe_dx_native_context *context,
    uint64_t size, doe_dx_memory memory, doe_dx_native_buffer **out, doe_dx_error *error) {
    doe_dx_native_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.buffer.allocate", error);
    D3D12_HEAP_PROPERTIES heap = {0};
    heap.Type = memory == DOE_DX_DEFAULT ? D3D12_HEAP_TYPE_DEFAULT :
        (memory == DOE_DX_UPLOAD ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK);
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description = {0};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (memory == DOE_DX_DEFAULT) description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES state = memory == DOE_DX_UPLOAD ?
        D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
    HRESULT result = ID3D12Device_CreateCommittedResource(context->device, &heap,
        D3D12_HEAP_FLAG_NONE, &description, state, NULL, &IID_ID3D12Resource,
        (void **)&buffer->resource);
    if (FAILED(result)) {
        doe_dx_native_buffer_destroy(buffer);
        return doe_dx_hresult(result, "ID3D12Device.CreateCommittedResource", error);
    }
    buffer->size = size;
    buffer->memory = memory;
    if (memory == DOE_DX_UPLOAD) {
        void *mapped = NULL;
        D3D12_RANGE read = {0, 0};
        result = ID3D12Resource_Map(buffer->resource, 0, &read, &mapped);
        if (FAILED(result)) {
            doe_dx_native_buffer_destroy(buffer);
            return doe_dx_hresult(result, "ID3D12Resource.Map.initialize", error);
        }
        memset(mapped, 0, (size_t)size);
        D3D12_RANGE written = {0, (SIZE_T)size};
        ID3D12Resource_Unmap(buffer->resource, 0, &written);
    }
    *out = buffer;
    return doe_dx_fail(DOE_DX_OK, 0, "native.buffer.create", error);
}

doe_dx_status doe_dx_native_write(doe_dx_native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error) {
    void *mapped = NULL;
    D3D12_RANGE read = {0, 0};
    HRESULT result = ID3D12Resource_Map(buffer->resource, 0, &read, &mapped);
    if (FAILED(result)) return doe_dx_hresult(result, "ID3D12Resource.Map.write", error);
    memcpy((unsigned char *)mapped + (size_t)offset, bytes, size);
    D3D12_RANGE written = {(SIZE_T)offset, (SIZE_T)offset + size};
    ID3D12Resource_Unmap(buffer->resource, 0, &written);
    return doe_dx_fail(DOE_DX_OK, 0, "native.buffer.write", error);
}

doe_dx_status doe_dx_native_read(doe_dx_native_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error) {
    void *mapped = NULL;
    D3D12_RANGE read = {(SIZE_T)offset, (SIZE_T)offset + size};
    HRESULT result = ID3D12Resource_Map(buffer->resource, 0, &read, &mapped);
    if (FAILED(result)) return doe_dx_hresult(result, "ID3D12Resource.Map.read", error);
    memcpy(bytes, (unsigned char *)mapped + (size_t)offset, size);
    D3D12_RANGE written = {0, 0};
    ID3D12Resource_Unmap(buffer->resource, 0, &written);
    return doe_dx_fail(DOE_DX_OK, 0, "native.buffer.read", error);
}

void doe_dx_native_pipeline_destroy(doe_dx_native_pipeline *pipeline) {
    if (!pipeline) return;
    if (pipeline->state) ID3D12PipelineState_Release(pipeline->state);
    if (pipeline->root) ID3D12RootSignature_Release(pipeline->root);
    free(pipeline);
}

doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error) {
    doe_dx_native_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.pipeline.allocate", error);
    D3D12_DESCRIPTOR_RANGE ranges[DOE_DX_MAX_BINDINGS] = {0};
    for (size_t index = 0; index < desc->binding_count; index++) {
        const doe_dx_binding_layout *binding = &desc->bindings[index];
        ranges[index].RangeType = binding->access == DOE_DX_READ_ONLY ?
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[index].NumDescriptors = 1;
        ranges[index].BaseShaderRegister = binding->shader_register;
        ranges[index].RegisterSpace = binding->register_space;
        ranges[index].OffsetInDescriptorsFromTableStart = (UINT)index;
        pipeline->bindings[index] = *binding;
    }
    D3D12_ROOT_PARAMETER parameter = {0};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = (UINT)desc->binding_count;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root = {0};
    root.NumParameters = desc->binding_count ? 1 : 0;
    root.pParameters = desc->binding_count ? &parameter : NULL;
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
    D3D12_COMPUTE_PIPELINE_STATE_DESC state = {0};
    state.pRootSignature = pipeline->root;
    state.CS.pShaderBytecode = desc->bytecode;
    state.CS.BytecodeLength = desc->bytecode_size;
    operation = "ID3D12Device.CreateComputePipelineState";
    result = ID3D12Device_CreateComputePipelineState(context->device, &state,
        &IID_ID3D12PipelineState, (void **)&pipeline->state);
    if (FAILED(result)) goto fail;
    ID3D10Blob_Release(blob);
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "native.pipeline.create", error);
fail:
    if (blob) ID3D10Blob_Release(blob);
    doe_dx_native_pipeline_destroy(pipeline);
    return doe_dx_hresult(result, operation, error);
}
#endif
