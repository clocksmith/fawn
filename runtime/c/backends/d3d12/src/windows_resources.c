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
    if (size > context->limits.max_buffer_size)
        return doe_dx_fail(DOE_DX_RANGE, 0, "native.buffer.size", error);
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
    D3D12_RESOURCE_ALLOCATION_INFO allocation = {0};
    context->device->lpVtbl->GetResourceAllocationInfo(context->device, &allocation, 0, 1, &description);
    if (allocation.SizeInBytes == UINT64_MAX || allocation.SizeInBytes < size ||
        allocation.SizeInBytes > context->limits.max_buffer_allocation_bytes ||
        !allocation.Alignment || (allocation.Alignment & (allocation.Alignment - 1u)) ||
        allocation.SizeInBytes % allocation.Alignment) {
        doe_dx_native_buffer_destroy(buffer);
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "ID3D12Device.GetResourceAllocationInfo.buffer", error);
    }
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

doe_dx_status doe_dx_native_zero_readback(doe_dx_native_buffer *buffer, doe_dx_error *error) {
    if (!buffer || buffer->memory != DOE_DX_READBACK)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"native.readback_zero.memory",error);
    void *mapped = NULL;
    const D3D12_RANGE no_read = {0,0};
    HRESULT result = ID3D12Resource_Map(buffer->resource,0,&no_read,&mapped);
    if (FAILED(result)) return doe_dx_hresult(result,"ID3D12Resource.Map.readback_zero",error);
    if (!mapped) {
        const D3D12_RANGE no_write = {0,0};
        ID3D12Resource_Unmap(buffer->resource,0,&no_write);
        return doe_dx_fail(DOE_DX_NATIVE_FAILURE,(int32_t)E_FAIL,"native.readback_zero.pointer",error);
    }
    /* Width is the accessible resource extent, not committed-heap alignment
     * slack from GetResourceAllocationInfo. No command has used this resource. */
    memset(mapped,0,(size_t)buffer->size);
    const D3D12_RANGE written = {0,(SIZE_T)buffer->size};
    ID3D12Resource_Unmap(buffer->resource,0,&written);
    return doe_dx_fail(DOE_DX_OK,0,"native.readback_zero",error);
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
