/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

/* Small buffers are placed in context-owned 4 MiB buffer-only heaps of their
 * memory class: 64 slots of 64 KiB. Placement never implies contents; every
 * caller still fully initializes. A slot range returns only after the resource
 * is released, which the common owner allows only after GPU use has retired. */
#define DOE_DX_BUFFER_HEAP_SLOTS 64u
#define DOE_DX_BUFFER_HEAP_SLOT (UINT64_C(64) * 1024u)
#define DOE_DX_BUFFER_PLACEMENT_MAX (UINT64_C(1) << 20)
struct doe_dx_buffer_heap {
    ID3D12Heap *heap;
    doe_dx_native_context *context; /* NULL once detached by context close. */
    doe_dx_memory memory;
    uint64_t used;
    /* Slots never placed since CreateHeap, which zeroes them (Doe never sets
     * D3D12_HEAP_FLAG_CREATE_NOT_ZEROED). A released slot is never fresh again. */
    uint64_t fresh;
    struct doe_dx_buffer_heap *next;
};

static void heap_unlink(struct doe_dx_buffer_heap *heap) {
    if (!heap->context) return;
    struct doe_dx_buffer_heap **cursor = &heap->context->buffer_heaps;
    while (*cursor != heap) cursor = &(*cursor)->next;
    *cursor = heap->next;
    heap->context = NULL;
}

static void heap_free(struct doe_dx_buffer_heap *heap) {
    heap_unlink(heap);
    ID3D12Heap_Release(heap->heap);
    free(heap);
}

void doe_dx_native_buffer_destroy(doe_dx_native_buffer *buffer) {
    if (!buffer) return;
    if (buffer->resource) ID3D12Resource_Release(buffer->resource);
    struct doe_dx_buffer_heap *heap = buffer->heap;
    if (heap) {
        heap->used &= ~buffer->heap_mask;
        bool spare = false;
        if (!heap->used && heap->context)
            for (struct doe_dx_buffer_heap *other = heap->context->buffer_heaps; other; other = other->next)
                if (other != heap && other->memory == heap->memory && !other->used) spare = true;
        /* Keep at most one empty heap per memory class; detached heaps go now. */
        if (!heap->used && (spare || !heap->context)) heap_free(heap);
    }
    free(buffer);
}

void doe_dx_buffer_heaps_close(doe_dx_native_context *context) {
    while (context->buffer_heaps) {
        struct doe_dx_buffer_heap *heap = context->buffer_heaps;
        if (heap->used) heap_unlink(heap);
        else heap_free(heap);
    }
}

static bool heap_take(struct doe_dx_buffer_heap *heap, uint64_t slots, uint64_t align, uint64_t *mask,
    uint64_t *offset) {
    const uint64_t run = slots == 64u ? UINT64_MAX : ((UINT64_C(1) << slots) - 1u);
    for (uint64_t first = 0; first + slots <= DOE_DX_BUFFER_HEAP_SLOTS; first += align) {
        const uint64_t candidate = run << first;
        if (heap->used & candidate) continue;
        heap->used |= candidate;
        *mask = candidate;
        *offset = first * DOE_DX_BUFFER_HEAP_SLOT;
        return true;
    }
    return false;
}

static HRESULT heap_create(doe_dx_native_context *context, const D3D12_HEAP_PROPERTIES *properties,
    doe_dx_memory memory, struct doe_dx_buffer_heap **out, const char **operation) {
    struct doe_dx_buffer_heap *heap = calloc(1, sizeof(*heap));
    *operation = "native.buffer_heap.allocate";
    if (!heap) return E_OUTOFMEMORY;
    D3D12_HEAP_DESC desc = {0};
    desc.SizeInBytes = DOE_DX_BUFFER_HEAP_SLOTS * DOE_DX_BUFFER_HEAP_SLOT;
    desc.Properties = *properties;
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    *operation = "ID3D12Device.CreateHeap.buffers";
    HRESULT result = ID3D12Device_CreateHeap(context->device, &desc, &IID_ID3D12Heap, (void **)&heap->heap);
    if (FAILED(result)) { free(heap); return result; }
    heap->context = context;
    heap->memory = memory;
    heap->fresh = UINT64_MAX;
    heap->next = context->buffer_heaps;
    context->buffer_heaps = heap;
    *out = heap;
    return result;
}

/* Device opening creates the first UPLOAD buffer heap, as it does the descriptor
 * ring, so the first queue write does not pay its creation. It is the class's one
 * retained empty heap; placement and release rules are unchanged. Idempotent. */
doe_dx_status doe_dx_native_upload_heap_prepare(doe_dx_native_context *context, doe_dx_error *error) {
    for (struct doe_dx_buffer_heap *heap = context->buffer_heaps; heap; heap = heap->next)
        if (heap->memory == DOE_DX_UPLOAD) return DOE_DX_OK;
    const D3D12_HEAP_PROPERTIES properties = {.Type = D3D12_HEAP_TYPE_UPLOAD,
        .CreationNodeMask = 1, .VisibleNodeMask = 1};
    struct doe_dx_buffer_heap *heap = NULL;
    const char *operation = NULL;
    HRESULT result = heap_create(context, &properties, DOE_DX_UPLOAD, &heap, &operation);
    return FAILED(result) ? doe_dx_hresult(result, operation, error) : DOE_DX_OK;
}

static HRESULT place_buffer(doe_dx_native_context *context, const D3D12_HEAP_PROPERTIES *properties,
    const D3D12_RESOURCE_DESC *description, const D3D12_RESOURCE_ALLOCATION_INFO *allocation,
    D3D12_RESOURCE_STATES state, doe_dx_memory memory, doe_dx_native_buffer *buffer, const char **operation) {
    const uint64_t slots = allocation->SizeInBytes / DOE_DX_BUFFER_HEAP_SLOT;
    const uint64_t align = allocation->Alignment / DOE_DX_BUFFER_HEAP_SLOT;
    uint64_t mask = 0, offset = 0;
    struct doe_dx_buffer_heap *heap = context->buffer_heaps;
    while (heap && (heap->memory != memory || !heap_take(heap, slots, align, &mask, &offset))) heap = heap->next;
    if (!heap) {
        HRESULT result = heap_create(context, properties, memory, &heap, operation);
        if (FAILED(result)) return result;
        if (!heap_take(heap, slots, align, &mask, &offset)) abort(); /* Admitted size fits an empty heap. */
    }
    buffer->zeroed = (heap->fresh & mask) == mask;
    heap->fresh &= ~mask;
    *operation = "ID3D12Device.CreatePlacedResource";
    HRESULT result = ID3D12Device_CreatePlacedResource(context->device, heap->heap, offset, description,
        state, NULL, &IID_ID3D12Resource, (void **)&buffer->resource);
    buffer->heap = heap;
    buffer->heap_mask = mask;
    return result; /* Failure destroy returns the slots and any now-spare heap. */
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
    const char *operation = "ID3D12Device.CreateCommittedResource";
    HRESULT result;
    if (allocation.SizeInBytes <= DOE_DX_BUFFER_PLACEMENT_MAX &&
        allocation.Alignment <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT &&
        allocation.Alignment % DOE_DX_BUFFER_HEAP_SLOT == 0 &&
        allocation.SizeInBytes % DOE_DX_BUFFER_HEAP_SLOT == 0)
        result = place_buffer(context, &heap, &description, &allocation, state, memory, buffer, &operation);
    else result = ID3D12Device_CreateCommittedResource(context->device, &heap,
        D3D12_HEAP_FLAG_NONE, &description, state, NULL, &IID_ID3D12Resource,
        (void **)&buffer->resource);
    if (FAILED(result)) {
        doe_dx_native_buffer_destroy(buffer);
        return doe_dx_hresult(result, operation, error);
    }
    buffer->size = size;
    buffer->memory = memory;
    /* A new committed resource's implicit heap is zeroed (HEAP_FLAG_NONE). */
    if (!buffer->heap) buffer->zeroed = true;
    if (memory == DOE_DX_UPLOAD && !buffer->zeroed) {
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

bool doe_dx_native_buffer_zeroed(const doe_dx_native_buffer *buffer) {
    return buffer && buffer->zeroed;
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

/* Map stays open until unmap_read: the read range tells the runtime which bytes
 * the CPU reads; Unmap writes nothing. Completion was established by the caller. */
doe_dx_status doe_dx_native_map_read(doe_dx_native_buffer *buffer, uint64_t offset, size_t size,
    const void **out, doe_dx_error *error) {
    void *mapped = NULL;
    D3D12_RANGE read = {(SIZE_T)offset, (SIZE_T)offset + size};
    HRESULT result = ID3D12Resource_Map(buffer->resource, 0, &read, &mapped);
    if (FAILED(result)) return doe_dx_hresult(result, "ID3D12Resource.Map.lend", error);
    if (!mapped) {
        const D3D12_RANGE none = {0, 0};
        ID3D12Resource_Unmap(buffer->resource, 0, &none);
        return doe_dx_fail(DOE_DX_NATIVE_FAILURE, (int32_t)E_FAIL, "native.buffer.lend.pointer", error);
    }
    *out = (const unsigned char *)mapped + (size_t)offset;
    return doe_dx_fail(DOE_DX_OK, 0, "native.buffer.lend", error);
}

void doe_dx_native_unmap_read(doe_dx_native_buffer *buffer) {
    const D3D12_RANGE written = {0, 0};
    ID3D12Resource_Unmap(buffer->resource, 0, &written);
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
