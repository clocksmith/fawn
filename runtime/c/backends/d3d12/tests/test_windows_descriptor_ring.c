/* SPDX-License-Identifier: MIT
 * Official SDK CPU vtable control of the context descriptor ring.
 * Proves range/heap call composition only; retirement is the caller's proof. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

#define STRIDE 32u
typedef struct sdk_heap { ID3D12DescriptorHeap iface; UINT count; unsigned releases; } sdk_heap;
typedef struct sdk_resource {
    ID3D12Resource iface;
    unsigned char bytes[256u * 256u];
    unsigned maps, unmaps, releases;
} sdk_resource;
typedef struct sdk_device {
    ID3D12Device iface;
    sdk_heap heaps[4];
    sdk_resource arenas[2];
    unsigned creates, arena_creates;
    HRESULT result, arena_result;
} sdk_device;

static ULONG STDMETHODCALLTYPE release_heap(ID3D12DescriptorHeap *raw)
{
    sdk_heap *heap = (sdk_heap *)raw;
    assert(!heap->releases);
    heap->releases++;
    return 0;
}
static D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE cpu_start(ID3D12DescriptorHeap *raw,
    D3D12_CPU_DESCRIPTOR_HANDLE *out)
{
    out->ptr = (SIZE_T)0x100000u * (SIZE_T)(((sdk_heap *)raw)->count);
    return out;
}
static D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE gpu_start(ID3D12DescriptorHeap *raw,
    D3D12_GPU_DESCRIPTOR_HANDLE *out)
{
    out->ptr = UINT64_C(0x200000000) + ((sdk_heap *)raw)->count;
    return out;
}
static HRESULT STDMETHODCALLTYPE create_heap(ID3D12Device *raw, const D3D12_DESCRIPTOR_HEAP_DESC *desc,
    REFIID iid, void **out)
{
    sdk_device *device = (sdk_device *)raw;
    assert(desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    assert(desc->Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE && !desc->NodeMask);
    assert(!memcmp(iid,&IID_ID3D12DescriptorHeap,sizeof(IID_ID3D12DescriptorHeap)));
    if (FAILED(device->result)) return device->result;
    assert(device->creates < 4);
    sdk_heap *heap = &device->heaps[device->creates++];
    heap->count = desc->NumDescriptors;
    *out = &heap->iface;
    return S_OK;
}
static UINT STDMETHODCALLTYPE increment(ID3D12Device *raw, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    (void)raw;
    assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return STRIDE;
}

static HRESULT STDMETHODCALLTYPE map_arena(ID3D12Resource *raw, UINT subresource,
    const D3D12_RANGE *range, void **out)
{
    sdk_resource *arena = (sdk_resource *)raw;
    assert(!subresource && range && !range->Begin && !range->End && !arena->maps);
    arena->maps++;
    *out = arena->bytes;
    return S_OK;
}
static void STDMETHODCALLTYPE unmap_arena(ID3D12Resource *raw, UINT subresource, const D3D12_RANGE *range)
{
    sdk_resource *arena = (sdk_resource *)raw;
    assert(!subresource && !range && arena->maps == 1u && !arena->unmaps);
    arena->unmaps++;
}
static ULONG STDMETHODCALLTYPE release_arena(ID3D12Resource *raw)
{
    sdk_resource *arena = (sdk_resource *)raw;
    assert(!arena->releases);
    arena->releases++;
    return 0;
}
static D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE arena_address(ID3D12Resource *raw)
{
    (void)raw;
    return UINT64_C(0x700000000);
}
static HRESULT STDMETHODCALLTYPE create_arena(ID3D12Device *raw, const D3D12_HEAP_PROPERTIES *heap,
    D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE *clear, REFIID iid, void **out)
{
    sdk_device *device = (sdk_device *)raw;
    assert(heap->Type == D3D12_HEAP_TYPE_UPLOAD && flags == D3D12_HEAP_FLAG_NONE && !clear);
    assert(desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc->Width == 256u * 256u);
    assert(state == D3D12_RESOURCE_STATE_GENERIC_READ);
    assert(!memcmp(iid,&IID_ID3D12Resource,sizeof(IID_ID3D12Resource)));
    if (FAILED(device->arena_result)) return device->arena_result;
    assert(device->arena_creates < 2);
    *out = &device->arenas[device->arena_creates++].iface;
    return S_OK;
}

static ID3D12DeviceVtbl device_methods;
static ID3D12DescriptorHeapVtbl heap_methods;
static ID3D12ResourceVtbl resource_methods;
static void initialize(sdk_device *device, doe_dx_native_context *context)
{
    device_methods.CreateDescriptorHeap = create_heap;
    device_methods.GetDescriptorHandleIncrementSize = increment;
    device_methods.CreateCommittedResource = create_arena;
    resource_methods.Map = map_arena;
    resource_methods.Unmap = unmap_arena;
    resource_methods.Release = release_arena;
    resource_methods.GetGPUVirtualAddress = arena_address;
    heap_methods.Release = release_heap;
    heap_methods.GetCPUDescriptorHandleForHeapStart = cpu_start;
    heap_methods.GetGPUDescriptorHandleForHeapStart = gpu_start;
    memset(device,0,sizeof(*device));
    device->iface.lpVtbl = &device_methods;
    for (unsigned i = 0; i < 4; i++) device->heaps[i].iface.lpVtbl = &heap_methods;
    for (unsigned i = 0; i < 2; i++) device->arenas[i].iface.lpVtbl = &resource_methods;
    memset(context,0,sizeof(*context));
    context->device = &device->iface;
}

static doe_dx_descriptor_use *take(doe_dx_native_context *context, UINT count, SIZE_T *cpu)
{
    doe_dx_descriptor_use *use = calloc(1,sizeof(*use));
    D3D12_CPU_DESCRIPTOR_HANDLE handle = {0};
    assert(use && doe_dx_descriptor_ring_take(context,count,use,&handle,NULL) == DOE_DX_OK);
    *cpu = handle.ptr;
    return use;
}

static void ranges_are_disjoint_and_return_only_on_release(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    SIZE_T cpu = 0;
    doe_dx_descriptor_use *a = take(&context,3,&cpu);
    assert(device.creates == 1 && device.heaps[0].count == 4096 && cpu == (SIZE_T)0x100000u * 4096u);
    doe_dx_descriptor_use *b = take(&context,5,&cpu);
    assert(device.creates == 1 && b->ring == a->ring && b->ring_first == 3 && cpu == (SIZE_T)0x100000u * 4096u + 3u * STRIDE);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    doe_dx_descriptor_gpu_start(b,&gpu);
    assert(gpu.ptr == UINT64_C(0x200000000) + 4096u + 3u * STRIDE);
    /* Released ranges come back only through release; live ranges never overlap. */
    doe_dx_release_descriptors(a);
    doe_dx_descriptor_use *c = take(&context,4088,&cpu);
    assert(c->ring == b->ring && c->ring_first == 8 && device.creates == 1);
    doe_dx_descriptor_use *d = take(&context,3,&cpu); /* Wraps to the released prefix. */
    assert(d->ring == b->ring && d->ring_first == 0);
    /* Full ring: a second, larger-if-needed ring is created, then released once empty. */
    doe_dx_descriptor_use *e = take(&context,2,&cpu);
    assert(device.creates == 2 && e->ring != b->ring && !device.heaps[1].releases);
    doe_dx_release_descriptors(e);
    assert(device.heaps[1].releases == 1);
    doe_dx_release_descriptors(b);
    doe_dx_release_descriptors(c);
    doe_dx_release_descriptors(d);
    assert(!device.heaps[0].releases); /* The first ring is long-lived. */
    doe_dx_descriptor_use *f = take(&context,4096,&cpu);
    assert(device.creates == 2 && f->ring_first == 0);
    doe_dx_release_descriptors(f);
    doe_dx_descriptor_rings_close(&context);
    assert(device.heaps[0].releases == 1 && !context.descriptor_rings);
}

static void device_open_prepares_once(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    assert(doe_dx_native_descriptors_prepare(&context,NULL) == DOE_DX_OK);
    assert(device.creates == 1 && device.heaps[0].count == 4096);
    assert(doe_dx_native_descriptors_prepare(&context,NULL) == DOE_DX_OK && device.creates == 1);
    SIZE_T cpu = 0;
    doe_dx_descriptor_use *use = take(&context,3,&cpu);
    assert(device.creates == 1 && use->heap == &device.heaps[0].iface && use->ring_first == 0);
    doe_dx_release_descriptors(use);
    doe_dx_descriptor_rings_close(&context);
    assert(device.heaps[0].releases == 1);
    initialize(&device,&context);
    device.result = E_OUTOFMEMORY;
    doe_dx_error error = {0};
    assert(doe_dx_native_descriptors_prepare(&context,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(!context.descriptor_rings && !strcmp(error.operation,"ID3D12Device.CreateDescriptorHeap.resources"));
}

static void failure_and_detached_close(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    device.result = E_OUTOFMEMORY;
    doe_dx_descriptor_use use = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0};
    doe_dx_error error = {0};
    assert(doe_dx_descriptor_ring_take(&context,1,&use,&cpu,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(!use.ring && !use.heap && !context.descriptor_rings);
    assert(!strcmp(error.operation,"ID3D12Device.CreateDescriptorHeap.resources"));
    device.result = S_OK;
    SIZE_T address = 0;
    doe_dx_descriptor_use *live = take(&context,5000,&address);
    assert(device.heaps[0].count == 5000);
    doe_dx_descriptor_rings_close(&context); /* Live range: detached, not released. */
    assert(!context.descriptor_rings && !device.heaps[0].releases);
    doe_dx_release_descriptors(live);
    assert(device.heaps[0].releases == 1);
}

static doe_dx_descriptor_use *take_slots(doe_dx_native_context *context, UINT count, UINT slots)
{
    doe_dx_descriptor_use *use = calloc(1,sizeof(*use));
    D3D12_CPU_DESCRIPTOR_HANDLE handle = {0};
    assert(use && doe_dx_descriptor_ring_take_slots(context,count,slots,use,&handle,NULL) == DOE_DX_OK);
    return use;
}

/* Storage-size slots: one mapped UPLOAD arena per ring, created on first need,
 * borrowed with the descriptor range and returned only with it. */
static void constant_slots_follow_descriptor_ranges(void)
{
    sdk_device device; doe_dx_native_context context;
    initialize(&device,&context);
    /* Device open prepares the first ring's arena with its heap, never a dispatch. */
    assert(doe_dx_native_descriptors_prepare(&context,NULL) == DOE_DX_OK);
    assert(device.arena_creates == 1 && device.arenas[0].maps == 1);
    doe_dx_descriptor_use *plain = take_slots(&context,2,0);
    assert(device.arena_creates == 1 && !plain->constant_count);
    doe_dx_descriptor_use *a = take_slots(&context,3,2);
    assert(device.arena_creates == 1 && device.arenas[0].maps == 1 && a->ring == plain->ring);
    assert(a->constant_first == 0 && a->constant_count == 2);
    assert(doe_dx_descriptor_constants(a,1) == device.arenas[0].bytes + 256);
    doe_dx_descriptor_use *b = take_slots(&context,1,254);
    assert(device.arena_creates == 1 && b->ring == a->ring && b->constant_first == 2);
    /* Constants exhausted while descriptors remain: a second ring and arena. */
    doe_dx_descriptor_use *c = take_slots(&context,1,1);
    assert(device.creates == 2 && device.arena_creates == 2 && c->ring != a->ring && !c->constant_first);
    doe_dx_release_descriptors(c);
    assert(device.heaps[1].releases == 1 && device.arenas[1].unmaps == 1 && device.arenas[1].releases == 1);
    doe_dx_release_descriptors(a); /* Slots return only with their range. */
    doe_dx_descriptor_use *d = take_slots(&context,1,2);
    assert(d->ring == b->ring && d->constant_first == 0 && device.arena_creates == 2);
    doe_dx_error error = {0};
    doe_dx_descriptor_use bad = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {0};
    assert(doe_dx_descriptor_ring_take_slots(&context,1,257,&bad,&cpu,&error) == DOE_DX_RANGE && !bad.ring);
    assert(doe_dx_descriptor_ring_take_slots(&context,0,1,&bad,&cpu,&error) == DOE_DX_RANGE && !bad.ring);
    doe_dx_release_descriptors(plain);
    doe_dx_release_descriptors(b);
    doe_dx_release_descriptors(d);
    assert(!device.arenas[0].unmaps); /* The long-lived ring keeps its arena. */
    doe_dx_descriptor_rings_close(&context);
    assert(device.heaps[0].releases == 1 && device.arenas[0].unmaps == 1 && device.arenas[0].releases == 1);
    /* Arena creation failure is exact and leaves no borrowed range or ring,
     * whether at device open or for a ring created on demand. */
    initialize(&device,&context);
    device.arena_result = E_OUTOFMEMORY;
    assert(doe_dx_native_descriptors_prepare(&context,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(!context.descriptor_rings && device.heaps[0].releases == 1);
    assert(!strcmp(error.operation,"ID3D12Device.CreateCommittedResource.storage_sizes"));
    initialize(&device,&context);
    device.arena_result = E_OUTOFMEMORY;
    assert(doe_dx_descriptor_ring_take_slots(&context,1,1,&bad,&cpu,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(!bad.ring && !context.descriptor_rings && device.heaps[0].releases == 1);
    assert(!strcmp(error.operation,"ID3D12Device.CreateCommittedResource.storage_sizes"));
}

int main(void)
{
    constant_slots_follow_descriptor_ranges();
    ranges_are_disjoint_and_return_only_on_release();
    failure_and_detached_close();
    device_open_prepares_once();
    puts("PASS official SDK CPU descriptor ring: one long-lived 4096-entry heap prepared once at device open, disjoint live ranges, offsets in CPU/GPU handles, release-only return, extra ring for overflow released when empty, exact creation failure, detached close; storage-size slots from one mapped UPLOAD arena per ring (the first at device open), returned only with their range; no GPU");
    return 0;
}
