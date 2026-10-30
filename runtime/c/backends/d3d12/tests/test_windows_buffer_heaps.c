/* SPDX-License-Identifier: MIT
 * Official SDK CPU vtable control of placed-buffer heap ownership.
 * Proves slot/heap call composition only; no adapter, GPU or residency claim. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

#define SLOT 65536u
typedef struct sdk_heap { ID3D12Heap iface; D3D12_HEAP_TYPE type; unsigned releases; } sdk_heap;
typedef struct sdk_resource {
    ID3D12Resource iface; sdk_heap *heap; UINT64 offset; unsigned releases;
} sdk_resource;
typedef struct sdk_device {
    ID3D12Device iface;
    sdk_heap heaps[8];
    sdk_resource resources[64];
    unsigned heap_count, resource_count, committed;
    HRESULT heap_result, place_result;
} sdk_device;

static ULONG STDMETHODCALLTYPE release_heap(ID3D12Heap *raw)
{
    sdk_heap *heap = (sdk_heap *)raw;
    assert(!heap->releases);
    heap->releases++;
    return 0;
}

static ULONG STDMETHODCALLTYPE release_resource(ID3D12Resource *raw)
{
    sdk_resource *resource = (sdk_resource *)raw;
    assert(!resource->releases && !resource->heap->releases); /* Heap outlives placement. */
    resource->releases++;
    return 0;
}

static HRESULT STDMETHODCALLTYPE map_resource(ID3D12Resource *raw, UINT subresource,
    const D3D12_RANGE *read, void **out)
{
    static unsigned char bytes[2u << 20]; /* Covers the 1 MiB UPLOAD zero fill. */
    (void)raw; (void)subresource; (void)read;
    *out = bytes;
    return S_OK;
}

static void STDMETHODCALLTYPE unmap_resource(ID3D12Resource *raw, UINT subresource, const D3D12_RANGE *written)
{
    (void)raw; (void)subresource; (void)written;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE allocation_info(ID3D12Device *raw,
    D3D12_RESOURCE_ALLOCATION_INFO *out, UINT visible, UINT count, const D3D12_RESOURCE_DESC *description)
{
    (void)raw; (void)visible; (void)count;
    UINT64 size = (description->Width + SLOT - 1u) / SLOT * SLOT;
    *out = (D3D12_RESOURCE_ALLOCATION_INFO){size, SLOT};
    return out;
}

static HRESULT STDMETHODCALLTYPE create_heap(ID3D12Device *raw, const D3D12_HEAP_DESC *desc,
    REFIID iid, void **out)
{
    sdk_device *device = (sdk_device *)raw;
    assert(desc->SizeInBytes == 64u * SLOT && desc->Flags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
    assert(!memcmp(iid,&IID_ID3D12Heap,sizeof(IID_ID3D12Heap)));
    if (FAILED(device->heap_result)) return device->heap_result;
    assert(device->heap_count < 8);
    sdk_heap *heap = &device->heaps[device->heap_count++];
    heap->type = desc->Properties.Type;
    *out = &heap->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE place_resource(ID3D12Device *raw, ID3D12Heap *raw_heap, UINT64 offset,
    const D3D12_RESOURCE_DESC *description, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE *clear, REFIID iid, void **out)
{
    sdk_device *device = (sdk_device *)raw;
    sdk_heap *heap = (sdk_heap *)raw_heap;
    assert(!heap->releases && offset % SLOT == 0 && offset + description->Width <= 64u * SLOT);
    assert(!clear && !memcmp(iid,&IID_ID3D12Resource,sizeof(IID_ID3D12Resource)));
    assert(state == (heap->type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ :
        D3D12_RESOURCE_STATE_COPY_DEST));
    if (FAILED(device->place_result)) return device->place_result;
    sdk_resource *resource = &device->resources[device->resource_count++];
    resource->heap = heap;
    resource->offset = offset;
    *out = &resource->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE create_committed(ID3D12Device *raw, const D3D12_HEAP_PROPERTIES *heap,
    D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC *description, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE *clear, REFIID iid, void **out)
{
    sdk_device *device = (sdk_device *)raw;
    (void)heap; (void)flags; (void)state; (void)clear; (void)iid;
    assert(description->Width > (1u << 20));
    device->committed++;
    static sdk_heap none;
    sdk_resource *resource = &device->resources[device->resource_count++];
    resource->heap = &none;
    *out = &resource->iface;
    return S_OK;
}

static ID3D12DeviceVtbl device_methods;
static ID3D12HeapVtbl heap_methods;
static ID3D12ResourceVtbl resource_methods;

static void initialize(sdk_device *device, doe_dx_native_context *context)
{
    device_methods.GetResourceAllocationInfo = allocation_info;
    device_methods.CreateHeap = create_heap;
    device_methods.CreatePlacedResource = place_resource;
    device_methods.CreateCommittedResource = create_committed;
    heap_methods.Release = release_heap;
    resource_methods.Release = release_resource;
    resource_methods.Map = map_resource;
    resource_methods.Unmap = unmap_resource;
    memset(device,0,sizeof(*device));
    device->iface.lpVtbl = &device_methods;
    for (unsigned i = 0; i < 8; i++) device->heaps[i].iface.lpVtbl = &heap_methods;
    for (unsigned i = 0; i < 64; i++) device->resources[i].iface.lpVtbl = &resource_methods;
    memset(context,0,sizeof(*context));
    context->device = &device->iface;
    context->limits.max_buffer_size = UINT64_C(1) << 28;
    context->limits.max_buffer_allocation_bytes = UINT64_C(1) << 28;
}

static doe_dx_native_buffer *create(doe_dx_native_context *context, uint64_t size, doe_dx_memory memory)
{
    doe_dx_native_buffer *buffer = NULL;
    assert(doe_dx_native_buffer_create(context,size,memory,&buffer,NULL) == DOE_DX_OK && buffer);
    return buffer;
}

static UINT64 offset_of(const doe_dx_native_buffer *buffer)
{
    return ((const sdk_resource *)buffer->resource)->offset;
}

static void slots_classes_reuse_and_spare_bound(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    doe_dx_native_buffer *a = create(&context,1u << 20,DOE_DX_DEFAULT);
    doe_dx_native_buffer *b = create(&context,16,DOE_DX_DEFAULT);
    doe_dx_native_buffer *c = create(&context,(1u << 20) + 4u,DOE_DX_DEFAULT); /* 17 slots: committed. */
    doe_dx_native_buffer *d = create(&context,1u << 20,DOE_DX_UPLOAD);
    doe_dx_native_buffer *e = create(&context,256,DOE_DX_READBACK);
    assert(device.heap_count == 3 && device.committed == 1 && !c->heap);
    assert(offset_of(a) == 0 && offset_of(b) == 16u * SLOT && a->heap == b->heap);
    assert(d->heap != a->heap && e->heap != a->heap && e->heap != d->heap);
    assert(device.heaps[0].type == D3D12_HEAP_TYPE_DEFAULT && device.heaps[1].type == D3D12_HEAP_TYPE_UPLOAD &&
        device.heaps[2].type == D3D12_HEAP_TYPE_READBACK);
    /* Freed slots are reused only after the resource itself is released. */
    doe_dx_native_buffer_destroy(a);
    assert(device.resources[0].releases == 1 && !device.heaps[0].releases);
    doe_dx_native_buffer *f = create(&context,SLOT,DOE_DX_DEFAULT);
    assert(f->heap == b->heap && offset_of(f) == 0);
    /* Slots 17..48 take two 1 MiB placements; fifteen-slot gaps cannot hold a third. */
    doe_dx_native_buffer *g[3];
    for (unsigned i = 0; i < 3; i++) g[i] = create(&context,1u << 20,DOE_DX_DEFAULT);
    assert(g[0]->heap == b->heap && offset_of(g[0]) == 17u * SLOT);
    assert(g[1]->heap == b->heap && offset_of(g[1]) == 33u * SLOT);
    assert(g[2]->heap != b->heap && device.heap_count == 4 && offset_of(g[2]) == 0);
    /* Empty heaps: the first empty one per class stays, a second releases. */
    doe_dx_native_buffer_destroy(g[2]);
    assert(!device.heaps[3].releases);
    doe_dx_native_buffer_destroy(g[0]);
    doe_dx_native_buffer_destroy(g[1]);
    doe_dx_native_buffer_destroy(b);
    doe_dx_native_buffer_destroy(f);
    assert(device.heaps[0].releases == 1 && !device.heaps[3].releases);
    doe_dx_native_buffer_destroy(c);
    doe_dx_native_buffer_destroy(d);
    doe_dx_native_buffer_destroy(e);
    assert(!device.heaps[1].releases && !device.heaps[2].releases);
    doe_dx_buffer_heaps_close(&context);
    assert(!context.buffer_heaps && device.heaps[1].releases == 1 && device.heaps[2].releases == 1 &&
        device.heaps[3].releases == 1);
    for (unsigned i = 0; i < device.resource_count; i++) assert(device.resources[i].releases == 1);
}

static void failures_keep_exact_status_without_committed_retry(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    device.heap_result = E_OUTOFMEMORY;
    doe_dx_native_buffer *buffer = NULL;
    doe_dx_error error = {0};
    assert(doe_dx_native_buffer_create(&context,SLOT,DOE_DX_DEFAULT,&buffer,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(!buffer && !strcmp(error.operation,"ID3D12Device.CreateHeap.buffers") && !device.committed);
    assert(!context.buffer_heaps);
    device.heap_result = S_OK;
    device.place_result = DXGI_ERROR_DEVICE_REMOVED;
    assert(doe_dx_native_buffer_create(&context,SLOT,DOE_DX_DEFAULT,&buffer,&error) == DOE_DX_DEVICE_LOST);
    assert(!buffer && !strcmp(error.operation,"ID3D12Device.CreatePlacedResource") && !device.committed);
    assert(context.buffer_heaps && !device.heaps[0].releases);
    device.place_result = S_OK;
    doe_dx_native_buffer *kept = create(&context,SLOT,DOE_DX_DEFAULT);
    assert(offset_of(kept) == 0 && kept->heap == context.buffer_heaps); /* Failed slots returned. */
    /* Close detaches a heap with a live placement; its last buffer releases it. */
    doe_dx_buffer_heaps_close(&context);
    assert(!context.buffer_heaps && !device.heaps[0].releases);
    doe_dx_native_buffer_destroy(kept);
    assert(device.heaps[0].releases == 1 && device.resources[0].releases == 1);
}

static void upload_heap_prepared_at_open(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    doe_dx_error error = {0};
    device.heap_result = E_OUTOFMEMORY;
    assert(doe_dx_native_upload_heap_prepare(&context,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(!strcmp(error.operation,"ID3D12Device.CreateHeap.buffers") && !context.buffer_heaps);
    device.heap_result = S_OK;
    assert(doe_dx_native_upload_heap_prepare(&context,NULL) == DOE_DX_OK && device.heap_count == 1);
    assert(device.heaps[0].type == D3D12_HEAP_TYPE_UPLOAD);
    assert(doe_dx_native_upload_heap_prepare(&context,NULL) == DOE_DX_OK && device.heap_count == 1);
    /* The first UPLOAD placement uses the prepared heap; other classes still create theirs. */
    doe_dx_native_buffer *upload = create(&context,1u << 20,DOE_DX_UPLOAD);
    assert(device.heap_count == 1 && upload->heap == context.buffer_heaps && offset_of(upload) == 0);
    doe_dx_native_buffer *other = create(&context,SLOT,DOE_DX_DEFAULT);
    assert(device.heap_count == 2 && other->heap != upload->heap);
    /* Released, it stays the class's one retained empty heap until close. */
    doe_dx_native_buffer_destroy(upload);
    doe_dx_native_buffer_destroy(other);
    assert(!device.heaps[0].releases && !device.heaps[1].releases);
    doe_dx_buffer_heaps_close(&context);
    assert(device.heaps[0].releases == 1 && device.heaps[1].releases == 1);
}

int main(void)
{
    slots_classes_reuse_and_spare_bound();
    failures_keep_exact_status_without_committed_retry();
    upload_heap_prepared_at_open();
    puts("PASS official SDK CPU placed-buffer heaps: per-class 64KiB slots, 1MiB placement bound, committed above it, slot reuse after release, one spare empty heap, exact heap/placement failures without committed retry, detached close, UPLOAD heap prepared at open; no GPU");
    return 0;
}
