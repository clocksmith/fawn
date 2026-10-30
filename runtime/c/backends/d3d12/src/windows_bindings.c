/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static void write_set(ID3D12Device *device,const doe_dx_resource_set *set,
    D3D12_CPU_DESCRIPTOR_HANDLE handle,D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle) {
    const doe_dx_native_resources *resources=set->resources;
    UINT increment = ID3D12Device_GetDescriptorHandleIncrementSize(device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (size_t index = 0; index < resources->buffer_count; index++) {
        const doe_dx_binding_layout *layout = &set->buffers[index];
        const doe_dx_native_binding *binding = &resources->buffers[index];
        doe_dx_descriptor_kind kind = doe_dx_buffer_descriptor_kind(layout);
        if (kind == DOE_DX_DESCRIPTOR_CBV) {
            D3D12_CONSTANT_BUFFER_VIEW_DESC view = {0};
            view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(binding->buffer->resource) + binding->offset;
            view.SizeInBytes = (UINT)binding->size;
            ID3D12Device_CreateConstantBufferView(device, &view, handle);
        } else if (kind == DOE_DX_DESCRIPTOR_SRV) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view = {0};
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.FirstElement = binding->offset / (layout->stride ? layout->stride : 4u);
            view.Buffer.NumElements = (UINT)(layout->stride ? binding->size / layout->stride : (binding->size+3)/4);
            view.Buffer.StructureByteStride = layout->stride;
            if (layout->view==DOE_DX_RAW) { view.Format=DXGI_FORMAT_R32_TYPELESS; view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW; }
            ID3D12Device_CreateShaderResourceView(device, binding->buffer->resource, &view, handle);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {0};
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.FirstElement = binding->offset / (layout->stride ? layout->stride : 4u);
            view.Buffer.NumElements = (UINT)(layout->stride ? binding->size / layout->stride : (binding->size+3)/4);
            view.Buffer.StructureByteStride = layout->stride;
            if (layout->view==DOE_DX_RAW) { view.Format=DXGI_FORMAT_R32_TYPELESS; view.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW; }
            ID3D12Device_CreateUnorderedAccessView(device, binding->buffer->resource,
                NULL, &view, handle);
        }
        handle.ptr += increment;
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        const doe_dx_texture_binding_layout *layout = &set->textures[index];
        doe_dx_native_texture *texture = resources->textures[index];
        if (layout->access == DOE_DX_READ_ONLY) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view;
            doe_dx_texture_srv(texture,&resources->texture_views[index],&view);
            ID3D12Device_CreateShaderResourceView(device, texture->resource, &view, handle);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view;
            doe_dx_texture_uav(texture,&resources->texture_views[index],&view);
            ID3D12Device_CreateUnorderedAccessView(device, texture->resource, NULL, &view, handle);
        }
        handle.ptr += increment;
    }
    if (resources->sampler_count) {
        handle=sampler_handle;
        increment = ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        for (size_t index = 0; index < resources->sampler_count; ++index) {
            D3D12_SAMPLER_DESC sampler;
            doe_dx_sampler_native(&resources->samplers[index],&sampler);
            ID3D12Device_CreateSampler(device,&sampler,handle);
            handle.ptr += increment;
        }
    }
}

/* Long-lived shader-visible CBV/SRV/UAV heaps owned by the context. A use
 * borrows one contiguous range; it returns only through
 * doe_dx_descriptor_ring_free when its command can no longer reach the GPU.
 * The first ring stays for the context lifetime; extra rings release when empty.
 * A ring also owns one persistently mapped UPLOAD arena of 256-byte root-CBV
 * slots (the first ring's with the device, overflow rings' on first need); a
 * use borrows its storage-size table slots from the same ring and returns them
 * with its descriptor range. */
#define DOE_DX_RING_DESCRIPTORS 4096u
#define DOE_DX_RING_CONSTANT_SLOTS 256u /* 64 KiB: one maximal 16384-row table. */
typedef struct doe_dx_ring_bits {
    uint64_t *used; /* One bit per descriptor or constant slot. */
    UINT capacity, live, cursor;
} doe_dx_ring_bits;
struct doe_dx_descriptor_ring {
    ID3D12DescriptorHeap *heap;
    doe_dx_native_context *context; /* NULL once detached by context close. */
    doe_dx_ring_bits descriptors, constants;
    UINT stride;
    ID3D12Resource *constant_buffer; /* NULL until a use needs constants. */
    uint8_t *constant_bytes;
    D3D12_GPU_VIRTUAL_ADDRESS constant_address;
    struct doe_dx_descriptor_ring *next;
};

static void bits_mark(doe_dx_ring_bits *bits, UINT first, UINT count, bool value) {
    for (UINT i = first; i < first + count; ++i) {
        const uint64_t bit = UINT64_C(1) << (i % 64u);
        if (value) bits->used[i / 64u] |= bit;
        else bits->used[i / 64u] &= ~bit;
    }
    if (value) bits->live += count;
    else bits->live -= count;
}
/* First fit from the cursor, then from zero: ranges recycle in submission order. */
static bool bits_take(doe_dx_ring_bits *bits, UINT count, UINT *first) {
    if (count > bits->capacity - bits->live) return false;
    for (int pass = 0; pass < 2; ++pass) {
        UINT start = pass ? 0 : bits->cursor, run = 0;
        for (UINT i = start; i < bits->capacity; ++i) {
            run = (bits->used[i / 64u] >> (i % 64u)) & 1u ? 0 : run + 1u;
            if (run == count) {
                *first = i + 1u - count;
                bits_mark(bits, *first, count, true);
                bits->cursor = i + 1u == bits->capacity ? 0 : i + 1u;
                return true;
            }
        }
    }
    return false;
}
static void ring_destroy(struct doe_dx_descriptor_ring *ring) {
    if (ring->context) {
        struct doe_dx_descriptor_ring **cursor = &ring->context->descriptor_rings;
        while (*cursor != ring) cursor = &(*cursor)->next;
        *cursor = ring->next;
    }
    if (ring->constant_buffer) {
        ID3D12Resource_Unmap(ring->constant_buffer, 0, NULL);
        ID3D12Resource_Release(ring->constant_buffer);
    }
    ID3D12DescriptorHeap_Release(ring->heap);
    free(ring->descriptors.used);
    free(ring->constants.used);
    free(ring);
}
void doe_dx_descriptor_ring_free(doe_dx_descriptor_use *use) {
    struct doe_dx_descriptor_ring *ring = use->ring;
    bits_mark(&ring->descriptors, use->ring_first, use->ring_count, false);
    if (use->constant_count) bits_mark(&ring->constants, use->constant_first, use->constant_count, false);
    use->ring = NULL;
    use->heap = NULL;
    use->constant_count = 0;
    /* Keep only the oldest ring; a detached or extra empty ring releases. */
    if (!ring->descriptors.live && !ring->constants.live &&
        (!ring->context || ring != ring->context->descriptor_rings)) ring_destroy(ring);
}
void doe_dx_descriptor_rings_close(doe_dx_native_context *context) {
    while (context->descriptor_rings) {
        struct doe_dx_descriptor_ring *ring = context->descriptor_rings;
        context->descriptor_rings = ring->next;
        ring->context = NULL;
        ring->next = NULL;
        if (!ring->descriptors.live && !ring->constants.live) ring_destroy(ring);
    }
}
void doe_dx_descriptor_gpu_start(const doe_dx_descriptor_use *use, D3D12_GPU_DESCRIPTOR_HANDLE *out) {
    use->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(use->heap, out);
    if (use->ring) out->ptr += (UINT64)use->ring_first * use->ring->stride;
}
static doe_dx_status ring_create(doe_dx_native_context *context, UINT capacity,
    struct doe_dx_descriptor_ring **out, doe_dx_error *error) {
    struct doe_dx_descriptor_ring *ring = calloc(1, sizeof(*ring));
    if (ring) {
        ring->descriptors.used = calloc((capacity + 63u) / 64u, sizeof(uint64_t));
        ring->constants.used = calloc(DOE_DX_RING_CONSTANT_SLOTS / 64u, sizeof(uint64_t));
    }
    if (!ring || !ring->descriptors.used || !ring->constants.used) {
        if (ring) { free(ring->descriptors.used); free(ring->constants.used); }
        free(ring);
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "descriptors.ring.allocate", error);
    }
    D3D12_DESCRIPTOR_HEAP_DESC description = {0};
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    description.NumDescriptors = capacity;
    HRESULT result = ID3D12Device_CreateDescriptorHeap(context->device, &description,
        &IID_ID3D12DescriptorHeap, (void **)&ring->heap);
    if (FAILED(result)) {
        free(ring->descriptors.used);
        free(ring->constants.used);
        free(ring);
        return doe_dx_hresult(result, "ID3D12Device.CreateDescriptorHeap.resources", error);
    }
    ring->descriptors.capacity = capacity;
    ring->constants.capacity = DOE_DX_RING_CONSTANT_SLOTS;
    ring->stride = ID3D12Device_GetDescriptorHandleIncrementSize(context->device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ring->context = context;
    /* Append: the first ring stays the long-lived one. */
    struct doe_dx_descriptor_ring **tail = &context->descriptor_rings;
    while (*tail) tail = &(*tail)->next;
    *tail = ring;
    *out = ring;
    return DOE_DX_OK;
}
/* One committed UPLOAD buffer per ring, mapped for its lifetime. CPU writes
 * touch only slots no live use holds, so no write overlaps GPU reads. */
static doe_dx_status ring_constants(struct doe_dx_descriptor_ring *ring, doe_dx_error *error) {
    if (ring->constant_buffer) return DOE_DX_OK;
    D3D12_HEAP_PROPERTIES heap = {0};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description = {0};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = (UINT64)DOE_DX_RING_CONSTANT_SLOTS * DOE_DX_DESCRIPTOR_CONSTANT_SLOT;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *buffer = NULL;
    HRESULT result = ID3D12Device_CreateCommittedResource(ring->context->device, &heap, D3D12_HEAP_FLAG_NONE,
        &description, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&buffer);
    if (FAILED(result)) return doe_dx_hresult(result, "ID3D12Device.CreateCommittedResource.storage_sizes", error);
    const D3D12_RANGE none = {0, 0};
    void *bytes = NULL;
    result = ID3D12Resource_Map(buffer, 0, &none, &bytes);
    if (FAILED(result) || !bytes) {
        ID3D12Resource_Release(buffer);
        return doe_dx_hresult(FAILED(result) ? result : E_POINTER, "ID3D12Resource.Map.storage_sizes", error);
    }
    ring->constant_buffer = buffer;
    ring->constant_bytes = bytes;
    ring->constant_address = ID3D12Resource_GetGPUVirtualAddress(buffer);
    return DOE_DX_OK;
}
uint8_t *doe_dx_descriptor_constants(const doe_dx_descriptor_use *use, UINT slot) {
    return use->ring->constant_bytes + (size_t)slot * DOE_DX_DESCRIPTOR_CONSTANT_SLOT;
}
/* Takes descriptors and, when slots is nonzero, constant slots from one ring. */
static bool ring_take(struct doe_dx_descriptor_ring *ring, UINT count, UINT slots,
    doe_dx_descriptor_use *use) {
    UINT first = 0, constant = 0;
    if (slots && ring->constants.capacity - ring->constants.live < slots) return false;
    if (!bits_take(&ring->descriptors, count, &first)) return false;
    if (slots && !bits_take(&ring->constants, slots, &constant)) {
        bits_mark(&ring->descriptors, first, count, false);
        return false;
    }
    use->ring = ring;
    use->ring_first = first;
    use->ring_count = count;
    use->constant_first = constant;
    use->constant_count = slots;
    use->heap = ring->heap;
    return true;
}

/* Device opening creates the long-lived ring and its constant arena up front,
 * as part of device creation; later preparations only borrow ranges. Idempotent. */
doe_dx_status doe_dx_native_descriptors_prepare(doe_dx_native_context *context, doe_dx_error *error) {
    if (context->descriptor_rings) return DOE_DX_OK;
    struct doe_dx_descriptor_ring *ring = NULL;
    doe_dx_status status = ring_create(context, DOE_DX_RING_DESCRIPTORS, &ring, error);
    if (status == DOE_DX_OK) status = ring_constants(ring, error);
    if (status != DOE_DX_OK && ring) ring_destroy(ring);
    return status;
}

doe_dx_status doe_dx_descriptor_ring_take_slots(doe_dx_native_context *context, UINT count, UINT slots,
    doe_dx_descriptor_use *use, D3D12_CPU_DESCRIPTOR_HANDLE *cpu, doe_dx_error *error) {
    if (slots > DOE_DX_RING_CONSTANT_SLOTS || (slots && !count))
        return doe_dx_fail(DOE_DX_RANGE, 0, "descriptors.storage_sizes", error);
    struct doe_dx_descriptor_ring *ring = context->descriptor_rings;
    for (; ring; ring = ring->next) {
        if (count > ring->descriptors.capacity - ring->descriptors.live) continue;
        if (slots) {
            doe_dx_status status = ring_constants(ring, error);
            if (status != DOE_DX_OK) return status;
        }
        if (ring_take(ring, count, slots, use)) break;
    }
    if (!ring) {
        doe_dx_status status = ring_create(context, count > DOE_DX_RING_DESCRIPTORS ? count :
            DOE_DX_RING_DESCRIPTORS, &ring, error);
        if (status == DOE_DX_OK && slots) status = ring_constants(ring, error);
        if (status != DOE_DX_OK) {
            if (ring) ring_destroy(ring);
            return status;
        }
        if (!ring_take(ring, count, slots, use)) abort(); /* An empty ring of this capacity fits. */
    }
    ring->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(ring->heap, cpu);
    cpu->ptr += (SIZE_T)use->ring_first * ring->stride;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_descriptor_ring_take(doe_dx_native_context *context, UINT count,
    doe_dx_descriptor_use *use, D3D12_CPU_DESCRIPTOR_HANDLE *cpu, doe_dx_error *error) {
    return doe_dx_descriptor_ring_take_slots(context, count, 0, use, cpu, error);
}

doe_dx_status doe_dx_prepare_sets(doe_dx_native_commands *commands,
    const doe_dx_resource_set *sets,size_t count,doe_dx_descriptor_use **out,doe_dx_error *error) {
    if (!sets || !count || count>DOE_DX_RENDER_STAGES)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"descriptors.sets",error);
    size_t resource_count=0,sampler_count=0;
    UINT slots=0,set_slots[DOE_DX_RENDER_STAGES]={0};
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_resources *values=sets[i].resources;
        if (!values || values->texture_count>DOE_DX_MAX_BINDINGS ||
            values->buffer_count>DOE_DX_MAX_BINDINGS-values->texture_count ||
            values->sampler_count>DOE_DX_MAX_DYNAMIC_SAMPLERS)
            return doe_dx_fail(DOE_DX_RANGE,0,"descriptors.set_range",error);
        for (size_t j=0;j<values->texture_count;++j) {
            const doe_dx_texture_binding_layout *layout=&sets[i].textures[j];
            if (layout->access!=DOE_DX_READ_WRITE || layout->write_only) continue;
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support={0};
            support.Format=doe_dx_texture_format(values->texture_views[j].dimension ?
                values->texture_views[j].format : values->textures[j]->format);
            HRESULT hr=ID3D12Device_CheckFeatureSupport(commands->context->device,
                D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
            if (FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.storage_read_write",error);
            if (!((UINT)support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
                return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"descriptors.storage_read_write",error);
        }
        resource_count+=sets[i].resources->buffer_count+sets[i].resources->texture_count;
        sampler_count+=sets[i].resources->sampler_count;
        set_slots[i]=doe_dx_storage_size_slots(values);
        slots+=set_slots[i];
    }
    doe_dx_descriptor_use *use = calloc(1, sizeof(*use));
    if (!use) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "descriptors.allocate", error);
    ID3D12Device *device = commands->context->device;
    D3D12_DESCRIPTOR_HEAP_DESC description = {0};
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    description.NumDescriptors = (UINT)resource_count;
    D3D12_CPU_DESCRIPTOR_HANDLE handle={0},samplers={0};
    if (description.NumDescriptors) {
        doe_dx_status status = doe_dx_descriptor_ring_take_slots(commands->context,description.NumDescriptors,slots,use,&handle,error);
        if (status != DOE_DX_OK) {
            doe_dx_release_descriptors(use);
            return status;
        }
    }
    else if (slots) {
        doe_dx_release_descriptors(use);
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"descriptors.storage_sizes",error);
    }
    if (sampler_count) {
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        description.NumDescriptors = (UINT)sampler_count;
        HRESULT result = ID3D12Device_CreateDescriptorHeap(device,&description,
            &IID_ID3D12DescriptorHeap,(void **)&use->sampler_heap);
        if (FAILED(result)) {
            doe_dx_release_descriptors(use);
            return doe_dx_hresult(result,"ID3D12Device.CreateDescriptorHeap.samplers",error);
        }
    }
    if (use->sampler_heap) use->sampler_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(use->sampler_heap,&samplers);
    UINT resource_stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT sampler_stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    UINT slot=use->constant_first;
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_resources *values=sets[i].resources;
        if (set_slots[i]) {
            /* The slots are this use's alone: no live command can read them. */
            uint8_t *table=doe_dx_descriptor_constants(use,slot);
            doe_dx_status status=doe_dx_write_storage_sizes(values,table,
                (size_t)set_slots[i]*DOE_DX_DESCRIPTOR_CONSTANT_SLOT,error);
            if (status!=DOE_DX_OK) { doe_dx_release_descriptors(use); return status; }
            use->storage_sizes[i]=use->ring->constant_address+(UINT64)slot*DOE_DX_DESCRIPTOR_CONSTANT_SLOT;
            slot+=set_slots[i];
        }
        write_set(device,&sets[i],handle,samplers);
        handle.ptr+=(values->buffer_count+values->texture_count)*resource_stride;
        samplers.ptr+=values->sampler_count*sampler_stride;
    }
    *out=use;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_prepare_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline,const doe_dx_native_resources *resources,
    doe_dx_descriptor_use **out,doe_dx_error *error) {
    const doe_dx_resource_set set={pipeline->bindings,pipeline->textures,resources};
    return doe_dx_prepare_sets(commands,&set,1,out,error);
}
#endif
