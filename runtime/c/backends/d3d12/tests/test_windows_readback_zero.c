/* SPDX-License-Identifier: MIT
 * Official SDK CPU vtable control of production Map/zero/Unmap composition.
 * No adapter, GPU execution, or GPU copy-visibility claim. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct sdk_resource {
    ID3D12Resource iface;
    unsigned char *bytes;
    size_t width, capacity;
    HRESULT map_result;
    bool null_mapping;
    unsigned maps, unmaps, releases;
    D3D12_RANGE written;
} sdk_resource;

typedef struct sdk_device {
    ID3D12Device iface;
    sdk_resource *resource;
    unsigned allocations, creates;
} sdk_device;

static HRESULT STDMETHODCALLTYPE map_resource(ID3D12Resource *raw, UINT subresource,
    const D3D12_RANGE *read, void **out)
{
    sdk_resource *resource = (sdk_resource *)raw;
    assert(subresource == 0 && read && read->Begin == 0 && read->End == 0 && out);
    resource->maps++;
    if (FAILED(resource->map_result)) return resource->map_result;
    *out = resource->null_mapping ? NULL : resource->bytes + 16;
    return S_OK;
}

static void STDMETHODCALLTYPE unmap_resource(ID3D12Resource *raw, UINT subresource,
    const D3D12_RANGE *written)
{
    sdk_resource *resource = (sdk_resource *)raw;
    assert(subresource == 0 && written);
    resource->unmaps++;
    resource->written = *written;
}

static ULONG STDMETHODCALLTYPE release_resource(ID3D12Resource *raw)
{
    sdk_resource *resource = (sdk_resource *)raw;
    resource->releases++;
    return 0;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE allocation_info(ID3D12Device *raw,
    D3D12_RESOURCE_ALLOCATION_INFO *out, UINT visible, UINT count,
    const D3D12_RESOURCE_DESC *description)
{
    sdk_device *device = (sdk_device *)raw;
    assert(visible == 0 && count == 1 && description);
    assert(description->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(description->Width == device->resource->width);
    device->allocations++;
    *out = (D3D12_RESOURCE_ALLOCATION_INFO){device->resource->capacity,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT};
    return out;
}

static HRESULT STDMETHODCALLTYPE create_resource(ID3D12Device *raw,
    const D3D12_HEAP_PROPERTIES *heap, D3D12_HEAP_FLAGS flags,
    const D3D12_RESOURCE_DESC *description, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE *clear, REFIID iid, void **out)
{
    sdk_device *device = (sdk_device *)raw;
    assert(heap && heap->Type == D3D12_HEAP_TYPE_READBACK);
    assert(heap->CreationNodeMask == 1 && heap->VisibleNodeMask == 1);
    assert(flags == D3D12_HEAP_FLAG_NONE && description);
    assert(description->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(description->Width == device->resource->width && description->Height == 1);
    assert(description->DepthOrArraySize == 1 && description->MipLevels == 1);
    assert(description->SampleDesc.Count == 1 && description->SampleDesc.Quality == 0);
    assert(description->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR);
    assert(description->Flags == D3D12_RESOURCE_FLAG_NONE);
    assert(state == D3D12_RESOURCE_STATE_COPY_DEST && !clear && out);
    assert(!memcmp(iid,&IID_ID3D12Resource,sizeof(IID_ID3D12Resource)));
    device->creates++;
    *out = &device->resource->iface;
    return S_OK;
}

static void assert_bytes(const sdk_resource *resource, bool zeroed)
{
    for (size_t index = 0; index < resource->capacity + 32; index++) {
        bool in_width = index >= 16 && index - 16 < resource->width;
        assert(resource->bytes[index] == (zeroed && in_width ? 0 : 0xa5));
    }
}

static void resource_width_and_admission(size_t width)
{
    ID3D12ResourceVtbl resource_methods = {0};
    resource_methods.Map = map_resource;
    resource_methods.Unmap = unmap_resource;
    resource_methods.Release = release_resource;
    const size_t capacity = 2u * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    sdk_resource resource = {.iface = {&resource_methods}, .width = width,
        .capacity = capacity, .map_result = S_OK};
    resource.bytes = malloc(capacity + 32);
    assert(resource.bytes && width < capacity);
    memset(resource.bytes,0xa5,capacity + 32);
    ID3D12DeviceVtbl device_methods = {0};
    device_methods.GetResourceAllocationInfo = allocation_info;
    device_methods.CreateCommittedResource = create_resource;
    sdk_device device = {.iface = {&device_methods}, .resource = &resource};
    doe_dx_native_context context = {.device = &device.iface};
    context.limits.max_buffer_size = capacity;
    context.limits.max_buffer_allocation_bytes = capacity;
    doe_dx_native_buffer *buffer = NULL;
    assert(doe_dx_native_buffer_create(&context,width,DOE_DX_READBACK,&buffer,NULL) == DOE_DX_OK);
    assert(buffer && buffer->memory == DOE_DX_READBACK && buffer->size == width);
    assert(device.allocations == 1 && device.creates == 1 && !resource.maps && !resource.unmaps);
    assert_bytes(&resource,false); /* General READBACK construction remains uninitialized. */
    assert(doe_dx_native_zero_readback(buffer,NULL) == DOE_DX_OK);
    assert(resource.maps == 1 && resource.unmaps == 1);
    assert(resource.written.Begin == 0 && resource.written.End == width);
    assert_bytes(&resource,true); /* Includes all resource-width padding, excludes heap slack. */
    buffer->memory = DOE_DX_DEFAULT;
    assert(doe_dx_native_zero_readback(buffer,NULL) == DOE_DX_INVALID_ARGUMENT);
    buffer->memory = DOE_DX_UPLOAD;
    assert(doe_dx_native_zero_readback(buffer,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_native_zero_readback(NULL,NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(resource.maps == 1 && resource.unmaps == 1);
    buffer->memory = DOE_DX_READBACK;
    doe_dx_native_buffer_destroy(buffer);
    assert(resource.releases == 1);
    free(resource.bytes);
}

static void failed_map(HRESULT result, doe_dx_status expected, bool null_mapping)
{
    ID3D12ResourceVtbl methods = {0};
    methods.Map = map_resource;
    methods.Unmap = unmap_resource;
    unsigned char bytes[96];
    memset(bytes,0xa5,sizeof(bytes));
    sdk_resource resource = {.iface = {&methods}, .bytes = bytes,
        .width = 17, .capacity = sizeof(bytes)-32, .map_result = result,
        .null_mapping = null_mapping};
    doe_dx_native_buffer buffer = {.resource = &resource.iface, .size = 17,
        .memory = DOE_DX_READBACK};
    doe_dx_error error = {0};
    assert(doe_dx_native_zero_readback(&buffer,&error) == expected);
    assert(error.status == expected && resource.maps == 1);
    if (null_mapping) {
        assert(error.native_code == (int32_t)E_FAIL && resource.unmaps == 1);
        assert(!strcmp(error.operation,"native.readback_zero.pointer"));
        assert(resource.written.Begin == 0 && resource.written.End == 0);
    } else {
        assert(error.native_code == (int32_t)result && !resource.unmaps);
        assert(!strcmp(error.operation,"ID3D12Resource.Map.readback_zero"));
    }
    assert_bytes(&resource,false);
}

int main(void)
{
    resource_width_and_admission(1);
    resource_width_and_admission(257);
    resource_width_and_admission(65537);
    failed_map(E_FAIL,DOE_DX_NATIVE_FAILURE,false);
    failed_map(E_OUTOFMEMORY,DOE_DX_OUT_OF_MEMORY,false);
    failed_map(DXGI_ERROR_DEVICE_REMOVED,DOE_DX_DEVICE_LOST,false);
    failed_map(DXGI_ERROR_DEVICE_RESET,DOE_DX_DEVICE_LOST,false);
    failed_map(S_OK,DOE_DX_NATIVE_FAILURE,true);
    puts("PASS official SDK CPU READBACK zero composition: READBACK/COPY_DEST, complete width, guard/heap slack, Map failures and null-map cleanup; no GPU");
    return 0;
}
