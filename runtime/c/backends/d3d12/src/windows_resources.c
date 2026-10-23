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

#endif
