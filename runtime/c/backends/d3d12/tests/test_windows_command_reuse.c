/* SPDX-License-Identifier: MIT
 * Official SDK CPU call composition for completed allocator/list reuse.
 * The caller supplies completion proof; this fixture never executes a GPU. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct sdk_fixture sdk_fixture;
typedef struct sdk_allocator {
    ID3D12CommandAllocator iface;
    sdk_fixture *fixture;
    unsigned index, resets, releases;
    HRESULT reset_result;
} sdk_allocator;
typedef struct sdk_list {
    ID3D12GraphicsCommandList iface;
    sdk_fixture *fixture;
    unsigned index, resets, closes, releases;
    bool created, open;
    HRESULT reset_result;
} sdk_list;
typedef struct sdk_heap {
    ID3D12DescriptorHeap iface;
    unsigned releases;
} sdk_heap;
struct sdk_fixture {
    ID3D12Device device;
    ID3D12DeviceVtbl device_methods;
    ID3D12CommandAllocatorVtbl allocator_methods;
    ID3D12GraphicsCommandListVtbl list_methods;
    ID3D12DescriptorHeapVtbl heap_methods;
    doe_dx_native_context context;
    sdk_allocator allocators[8];
    sdk_list lists[8];
    sdk_heap heaps[4];
    unsigned allocator_creates, list_creates;
    HRESULT allocator_result, list_result;
};

static ID3D12CommandAllocator *idle_allocator(const doe_dx_native_context *context)
{
    return context->idle_pairs ? context->idle_allocators[0] : NULL; /* Next reused: the oldest. */
}
static ID3D12GraphicsCommandList *idle_list(const doe_dx_native_context *context)
{
    return context->idle_pairs ? context->idle_lists[0] : NULL;
}
static bool pooled(const doe_dx_native_context *context, const void *object)
{
    for (uint32_t i = 0; i < context->idle_pairs; ++i)
        if ((const void *)context->idle_allocators[i] == object || (const void *)context->idle_lists[i] == object) return true;
    return false;
}

static HRESULT STDMETHODCALLTYPE reset_allocator(ID3D12CommandAllocator *raw)
{
    sdk_allocator *allocator = (sdk_allocator *)raw;
    sdk_fixture *fixture = allocator->fixture;
    assert(!allocator->releases && !fixture->lists[allocator->index].open);
    assert(!pooled(&fixture->context,raw)); /* Taken from the pool before its reset. */
    allocator->resets++;
    return allocator->reset_result;
}

static ULONG STDMETHODCALLTYPE release_allocator(ID3D12CommandAllocator *raw)
{
    sdk_allocator *allocator = (sdk_allocator *)raw;
    sdk_list *list = &allocator->fixture->lists[allocator->index];
    assert(!allocator->releases && (!list->created || list->releases == 1));
    allocator->releases++;
    return 0;
}

static HRESULT STDMETHODCALLTYPE reset_list(ID3D12GraphicsCommandList *raw,
    ID3D12CommandAllocator *raw_allocator, ID3D12PipelineState *pipeline)
{
    sdk_list *list = (sdk_list *)raw;
    sdk_fixture *fixture = list->fixture;
    sdk_allocator *allocator = &fixture->allocators[list->index];
    assert(raw_allocator == &allocator->iface && !pipeline);
    assert(!list->open && !list->releases && !allocator->releases);
    assert(allocator->resets == list->resets + 1);
    assert(!pooled(&fixture->context,raw) && !pooled(&fixture->context,raw_allocator));
    list->resets++;
    if (SUCCEEDED(list->reset_result)) list->open = true;
    return list->reset_result;
}

static HRESULT STDMETHODCALLTYPE close_list(ID3D12GraphicsCommandList *raw)
{
    sdk_list *list = (sdk_list *)raw;
    assert(list->open && !list->releases);
    list->open = false;
    list->closes++;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE release_list(ID3D12GraphicsCommandList *raw)
{
    sdk_list *list = (sdk_list *)raw;
    assert(!list->releases && !list->fixture->allocators[list->index].releases);
    list->releases++;
    return 0;
}

static ULONG STDMETHODCALLTYPE release_heap(ID3D12DescriptorHeap *raw)
{
    sdk_heap *heap = (sdk_heap *)raw;
    assert(!heap->releases);
    heap->releases++;
    return 0;
}

static HRESULT STDMETHODCALLTYPE create_allocator(ID3D12Device *raw,
    D3D12_COMMAND_LIST_TYPE type, REFIID iid, void **out)
{
    sdk_fixture *fixture = (sdk_fixture *)raw;
    assert(type == D3D12_COMMAND_LIST_TYPE_DIRECT && out);
    assert(!memcmp(iid,&IID_ID3D12CommandAllocator,sizeof(IID_ID3D12CommandAllocator)));
    unsigned index = fixture->allocator_creates++;
    assert(index < 8);
    if (FAILED(fixture->allocator_result)) return fixture->allocator_result;
    *out = &fixture->allocators[index].iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE create_list(ID3D12Device *raw, UINT node,
    D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *raw_allocator,
    ID3D12PipelineState *pipeline, REFIID iid, void **out)
{
    sdk_fixture *fixture = (sdk_fixture *)raw;
    sdk_allocator *allocator = (sdk_allocator *)raw_allocator;
    assert(!node && type == D3D12_COMMAND_LIST_TYPE_DIRECT && !pipeline && out);
    assert(!memcmp(iid,&IID_ID3D12GraphicsCommandList,sizeof(IID_ID3D12GraphicsCommandList)));
    assert(allocator->fixture == fixture && !allocator->resets && !allocator->releases);
    fixture->list_creates++;
    if (FAILED(fixture->list_result)) return fixture->list_result;
    sdk_list *list = &fixture->lists[allocator->index];
    assert(!list->created);
    list->created = true;
    list->open = true;
    *out = &list->iface;
    return S_OK;
}

static void initialize(sdk_fixture *fixture)
{
    memset(fixture,0,sizeof(*fixture));
    fixture->device_methods.CreateCommandAllocator = create_allocator;
    fixture->device_methods.CreateCommandList = create_list;
    fixture->allocator_methods.Reset = reset_allocator;
    fixture->allocator_methods.Release = release_allocator;
    fixture->list_methods.Reset = reset_list;
    fixture->list_methods.Close = close_list;
    fixture->list_methods.Release = release_list;
    fixture->heap_methods.Release = release_heap;
    fixture->device.lpVtbl = &fixture->device_methods;
    fixture->context.device = &fixture->device;
    for (unsigned index = 0; index < 8; index++) {
        fixture->allocators[index].iface.lpVtbl = &fixture->allocator_methods;
        fixture->allocators[index].fixture = fixture;
        fixture->allocators[index].index = index;
        fixture->lists[index].iface.lpVtbl = &fixture->list_methods;
        fixture->lists[index].fixture = fixture;
        fixture->lists[index].index = index;
        if (index < 4) fixture->heaps[index].iface.lpVtbl = &fixture->heap_methods;
    }
}

static doe_dx_native_commands *fresh(sdk_fixture *fixture, unsigned index)
{
    doe_dx_native_commands *commands = NULL;
    assert(doe_dx_native_commands_create(&fixture->context,&commands,NULL) == DOE_DX_OK);
    assert(commands && commands->context == &fixture->context && !commands->descriptors);
    assert(commands->allocator == &fixture->allocators[index].iface);
    assert(commands->list == &fixture->lists[index].iface);
    return commands;
}

static void attach_descriptors(sdk_fixture *fixture, doe_dx_native_commands *commands)
{
    doe_dx_descriptor_use *first = calloc(1,sizeof(*first));
    doe_dx_descriptor_use *second = calloc(1,sizeof(*second));
    assert(first && second);
    first->heap = &fixture->heaps[0].iface;
    first->sampler_heap = &fixture->heaps[1].iface;
    first->next = second;
    second->heap = &fixture->heaps[2].iface;
    second->sampler_heap = &fixture->heaps[3].iface;
    commands->descriptors = first;
}

static void completed_pair_reuses_and_releases_descriptors(void)
{
    sdk_fixture fixture;
    initialize(&fixture);
    doe_dx_native_commands *commands = fresh(&fixture,0);
    assert(fixture.allocator_creates == 1 && fixture.list_creates == 1);
    assert(!fixture.allocators[0].resets && !fixture.lists[0].resets);
    attach_descriptors(&fixture,commands);
    assert(doe_dx_native_finish(commands,NULL) == DOE_DX_OK);
    /* Successful terminal completion is the external precondition of recycle. */
    doe_dx_native_commands_recycle(commands);
    assert(idle_allocator(&fixture.context) == &fixture.allocators[0].iface);
    assert(idle_list(&fixture.context) == &fixture.lists[0].iface);
    assert(!fixture.allocators[0].releases && !fixture.lists[0].releases);
    for (unsigned index = 0; index < 4; index++) assert(fixture.heaps[index].releases == 1);
    commands = fresh(&fixture,0);
    assert(fixture.allocator_creates == 1 && fixture.list_creates == 1);
    assert(fixture.allocators[0].resets == 1 && fixture.lists[0].resets == 1);
    assert(!idle_allocator(&fixture.context) && !idle_list(&fixture.context));
    assert(doe_dx_native_finish(commands,NULL) == DOE_DX_OK);
    doe_dx_native_commands_recycle(commands);
    doe_dx_native_commands_cache_close(&fixture.context);
    assert(!idle_allocator(&fixture.context) && !idle_list(&fixture.context));
    assert(fixture.allocators[0].releases == 1 && fixture.lists[0].releases == 1);
    doe_dx_native_commands_cache_close(&fixture.context); /* Idempotent teardown. */
}

static void two_live_pairs_bound_cache_and_discard(void)
{
    sdk_fixture fixture;
    initialize(&fixture);
    doe_dx_native_commands *first = fresh(&fixture,0);
    doe_dx_native_commands *second = fresh(&fixture,1);
    assert(first->allocator != second->allocator && first->list != second->list);
    attach_descriptors(&fixture,second);
    assert(doe_dx_native_finish(first,NULL) == DOE_DX_OK);
    assert(doe_dx_native_finish(second,NULL) == DOE_DX_OK);
    doe_dx_native_commands_recycle(first);
    doe_dx_native_commands_recycle(second);
    /* Both completed pairs are kept; the oldest is reused first. */
    assert(fixture.context.idle_pairs == 2u);
    assert(idle_allocator(&fixture.context) == &fixture.allocators[0].iface);
    assert(idle_list(&fixture.context) == &fixture.lists[0].iface);
    for (unsigned index = 0; index < 2; index++)
        assert(!fixture.allocators[index].releases && !fixture.lists[index].releases);
    for (unsigned index = 0; index < 4; index++) assert(fixture.heaps[index].releases == 1);
    first = fresh(&fixture,0);
    second = fresh(&fixture,1);
    assert(fixture.allocator_creates == 2 && fixture.list_creates == 2 && !fixture.context.idle_pairs);
    /* Destroy is unconditional discard, even for a successfully closed list. */
    assert(doe_dx_native_finish(first,NULL) == DOE_DX_OK);
    doe_dx_native_commands_destroy(first);
    assert(fixture.allocators[0].releases == 1 && fixture.lists[0].releases == 1);
    doe_dx_native_commands_destroy(second); /* Unsubmitted/open discard never caches. */
    assert(fixture.allocators[1].releases == 1 && fixture.lists[1].releases == 1);
    assert(!fixture.context.idle_pairs);
    /* Bound: five completed pairs keep four; the fifth releases at once. */
    doe_dx_native_commands *live[5];
    for (unsigned index = 0; index < 5; index++) live[index] = fresh(&fixture,2u + index);
    for (unsigned index = 0; index < 5; index++) {
        assert(doe_dx_native_finish(live[index],NULL) == DOE_DX_OK);
        doe_dx_native_commands_recycle(live[index]);
    }
    assert(fixture.context.idle_pairs == DOE_DX_IDLE_COMMAND_PAIRS);
    for (unsigned index = 2; index < 6; index++)
        assert(!fixture.allocators[index].releases && !fixture.lists[index].releases);
    assert(fixture.allocators[6].releases == 1 && fixture.lists[6].releases == 1);
    doe_dx_native_commands_cache_close(&fixture.context);
    for (unsigned index = 2; index < 7; index++)
        assert(fixture.allocators[index].releases == 1 && fixture.lists[index].releases == 1);
    assert(!fixture.context.idle_pairs);
    doe_dx_native_commands_cache_close(&fixture.context);
}

static void reset_failure(bool allocator_failure, HRESULT result, doe_dx_status expected)
{
    sdk_fixture fixture;
    initialize(&fixture);
    doe_dx_native_commands *commands = fresh(&fixture,0);
    assert(doe_dx_native_finish(commands,NULL) == DOE_DX_OK);
    doe_dx_native_commands_recycle(commands);
    if (allocator_failure) fixture.allocators[0].reset_result = result;
    else fixture.lists[0].reset_result = result;
    doe_dx_native_commands sentinel = {0};
    doe_dx_native_commands *untouched = &sentinel;
    doe_dx_error error = {0};
    assert(doe_dx_native_commands_create(&fixture.context,&untouched,&error) == expected);
    assert(untouched == &sentinel && error.status == expected && error.native_code == (int32_t)result);
    assert(!strcmp(error.operation,allocator_failure ? "ID3D12CommandAllocator.Reset" :
        "ID3D12GraphicsCommandList.Reset"));
    assert(fixture.allocator_creates == 1 && fixture.list_creates == 1); /* No retry. */
    assert(fixture.allocators[0].resets == 1 && fixture.lists[0].resets == (allocator_failure ? 0u : 1u));
    assert(fixture.allocators[0].releases == 1 && fixture.lists[0].releases == 1);
    assert(!idle_allocator(&fixture.context) && !idle_list(&fixture.context));
    doe_dx_native_commands_cache_close(&fixture.context);
    commands = fresh(&fixture,1); /* A separate later call may create a new pair. */
    doe_dx_native_commands_destroy(commands);
}

static void fresh_native_failure(bool allocator_failure)
{
    sdk_fixture fixture;
    initialize(&fixture);
    if (allocator_failure) fixture.allocator_result = E_OUTOFMEMORY;
    else fixture.list_result = E_OUTOFMEMORY;
    doe_dx_native_commands sentinel = {0};
    doe_dx_native_commands *untouched = &sentinel;
    doe_dx_error error = {0};
    assert(doe_dx_native_commands_create(&fixture.context,&untouched,&error) == DOE_DX_OUT_OF_MEMORY);
    assert(untouched == &sentinel && error.status == DOE_DX_OUT_OF_MEMORY);
    assert(error.native_code == (int32_t)E_OUTOFMEMORY);
    assert(!strcmp(error.operation,allocator_failure ? "ID3D12Device.CreateCommandAllocator" :
        "ID3D12Device.CreateCommandList"));
    assert(fixture.allocator_creates == 1 && fixture.list_creates == (allocator_failure ? 0u : 1u));
    assert(fixture.allocators[0].releases == (allocator_failure ? 0u : 1u));
    assert(!fixture.lists[0].releases && !idle_allocator(&fixture.context) && !idle_list(&fixture.context));
    doe_dx_native_commands_cache_close(&fixture.context);
}

int main(void)
{
    completed_pair_reuses_and_releases_descriptors();
    two_live_pairs_bound_cache_and_discard();
    reset_failure(true,E_FAIL,DOE_DX_NATIVE_FAILURE);
    reset_failure(false,E_FAIL,DOE_DX_NATIVE_FAILURE);
    reset_failure(true,E_OUTOFMEMORY,DOE_DX_OUT_OF_MEMORY);
    reset_failure(false,DXGI_ERROR_DEVICE_REMOVED,DOE_DX_DEVICE_LOST);
    fresh_native_failure(true);
    fresh_native_failure(false);
    puts("PASS official SDK CPU completed-command reuse: distinct live pairs, closed reset order, descriptor release, four-pair oldest-first bound, exact failures/no retry and final discard; no GPU/completion proof");
    return 0;
}
