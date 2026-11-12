/* SPDX-License-Identifier: MIT
 * Official SDK CPU control for held dispatch buffer states and recorded-binding
 * skips (windows_states.c). A fake command list records every barrier and
 * binding call; no device is created and no GPU executes. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct sdk_list {
    ID3D12GraphicsCommandList iface;
    D3D12_RESOURCE_BARRIER barriers[64];
    unsigned barrier_count, barrier_calls, pipelines, compute_roots, graphics_roots, heap_sets, copies;
} sdk_list;

static void STDMETHODCALLTYPE barrier(ID3D12GraphicsCommandList *raw, UINT count, const D3D12_RESOURCE_BARRIER *values)
{
    sdk_list *list = (sdk_list *)raw;
    assert(count && list->barrier_count + count <= 64);
    for (UINT i = 0; i < count; ++i) list->barriers[list->barrier_count++] = values[i];
    list->barrier_calls++;
}
static void STDMETHODCALLTYPE set_pipeline(ID3D12GraphicsCommandList *raw, ID3D12PipelineState *state)
{
    assert(state);
    ((sdk_list *)raw)->pipelines++;
}
static void STDMETHODCALLTYPE set_compute_root(ID3D12GraphicsCommandList *raw, ID3D12RootSignature *root)
{
    assert(root);
    ((sdk_list *)raw)->compute_roots++;
}
static void STDMETHODCALLTYPE set_graphics_root(ID3D12GraphicsCommandList *raw, ID3D12RootSignature *root)
{
    assert(root);
    ((sdk_list *)raw)->graphics_roots++;
}
static void STDMETHODCALLTYPE set_heaps(ID3D12GraphicsCommandList *raw, UINT count, ID3D12DescriptorHeap *const *heaps)
{
    assert(count <= 2 && (!count || heaps[0]));
    ((sdk_list *)raw)->heap_sets++;
}
static void STDMETHODCALLTYPE copy_region(ID3D12GraphicsCommandList *raw, ID3D12Resource *destination, UINT64 to,
    ID3D12Resource *source, UINT64 from, UINT64 size)
{
    (void)to; (void)from;
    assert(destination && source && destination != source && size);
    ((sdk_list *)raw)->copies++;
}
static CONST_VTBL ID3D12GraphicsCommandListVtbl list_methods = {
    .CopyBufferRegion=copy_region,.ResourceBarrier=barrier,.SetPipelineState=set_pipeline,.SetComputeRootSignature=set_compute_root,
    .SetGraphicsRootSignature=set_graphics_root,.SetDescriptorHeaps=set_heaps};

static void expect_transition(const sdk_list *list, unsigned index, const ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    const D3D12_RESOURCE_BARRIER *b = &list->barriers[index];
    assert(b->Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && b->Transition.pResource == resource);
    assert(b->Transition.StateBefore == before && b->Transition.StateAfter == after);
    assert(b->Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
}
static void expect_uav(const sdk_list *list, unsigned index, const ID3D12Resource *resource)
{
    assert(list->barriers[index].Type == D3D12_RESOURCE_BARRIER_TYPE_UAV);
    assert(list->barriers[index].UAV.pResource == resource);
}

int main(void)
{
    const D3D12_RESOURCE_STATES copy = D3D12_RESOURCE_STATE_COPY_DEST, uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const D3D12_RESOURCE_STATES srv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES cbv = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    const D3D12_RESOURCE_STATES args = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    sdk_list list = {.iface={.lpVtbl=&list_methods}};
    doe_dx_native_context context = {0};
    ID3D12Resource ra = {0}, rb = {0}, rc = {0};
    doe_dx_native_buffer a = {.resource=&ra}, b = {.resource=&rb}, c = {.resource=&rc};
    doe_dx_native_commands first = {.context=&context,.list=&list.iface,.serial=++context.recording_serial};

    /* First use in a list: implicit promotion from the decayed state, no barrier. */
    doe_dx_buffer_state uses[3] = {{&a, uav}, {&b, srv}};
    assert(doe_dx_hold_buffers(&first, uses, 2));
    assert(!list.barrier_calls && first.held_count == 2 && a.held_state == uav && b.held_state == srv);
    /* UAV -> UAV keeps the state and orders writes with a UAV barrier; a read
     * buffer that becomes writable transitions. One call. */
    uses[0] = (doe_dx_buffer_state){&a, uav};
    uses[1] = (doe_dx_buffer_state){&b, uav};
    assert(doe_dx_hold_buffers(&first, uses, 2));
    assert(list.barrier_calls == 1 && list.barrier_count == 2 && first.held_count == 2);
    expect_uav(&list, 0, &ra);
    expect_transition(&list, 1, &rb, srv, uav);
    /* Write -> read transitions; a new indirect argument buffer is promoted. */
    uses[0] = (doe_dx_buffer_state){&a, srv};
    uses[1] = (doe_dx_buffer_state){&b, cbv};
    uses[2] = (doe_dx_buffer_state){&c, args};
    assert(doe_dx_hold_buffers(&first, uses, 3));
    assert(list.barrier_calls == 2 && list.barrier_count == 4 && first.held_count == 3 && c.held_state == args);
    expect_transition(&list, 2, &ra, uav, srv);
    expect_transition(&list, 3, &rb, uav, cbv);
    /* Read -> read: a covered read records nothing; a new read kind widens the union. */
    uses[0] = (doe_dx_buffer_state){&a, srv};
    assert(doe_dx_hold_buffers(&first, uses, 1));
    assert(list.barrier_calls == 2);
    uses[0] = (doe_dx_buffer_state){&b, srv};
    assert(doe_dx_hold_buffers(&first, uses, 1));
    assert(list.barrier_calls == 3 && list.barrier_count == 5);
    expect_transition(&list, 4, &rb, cbv, cbv | srv);
    uses[0] = (doe_dx_buffer_state){&b, cbv};
    assert(doe_dx_hold_buffers(&first, uses, 1));
    assert(list.barrier_calls == 3 && b.held_state == (cbv | srv));
    /* A copy naming a tracked buffer returns only it to COPY_DEST; an untouched
     * copy buffer becomes tracked at COPY_DEST, so its next dispatch use
     * transitions explicitly (the copy wrote it in this list). */
    ID3D12Resource rd = {0};
    doe_dx_native_buffer d = {.resource=&rd};
    doe_dx_native_buffer *copied[2] = {&a, &d};
    doe_dx_settle_tracked(&first, copied, 2);
    assert(list.barrier_calls == 4 && list.barrier_count == 6 && first.held_count == 4 && !first.untracked);
    expect_transition(&list, 5, &ra, srv, copy);
    assert(a.held_state == copy && d.held_state == copy && b.held_state == (cbv | srv));
    uses[0] = (doe_dx_buffer_state){&a, uav};
    uses[1] = (doe_dx_buffer_state){&d, srv};
    assert(doe_dx_hold_buffers(&first, uses, 2));
    assert(list.barrier_calls == 5 && list.barrier_count == 8);
    expect_transition(&list, 6, &ra, copy, uav);
    expect_transition(&list, 7, &rd, copy, srv);
    /* An operation that does not report its buffers settles everything in one
     * call; untouched buffers are then assumed at COPY_DEST. */
    doe_dx_settle_buffers(&first);
    assert(list.barrier_calls == 6 && list.barrier_count == 12 && first.untracked);
    expect_transition(&list, 8, &ra, uav, copy);
    expect_transition(&list, 9, &rb, cbv | srv, copy);
    expect_transition(&list, 10, &rc, args, copy);
    expect_transition(&list, 11, &rd, srv, copy);
    doe_dx_settle_buffers(&first);
    assert(list.barrier_calls == 6);
    ID3D12Resource re = {0};
    doe_dx_native_buffer e = {.resource=&re};
    uses[0] = (doe_dx_buffer_state){&e, srv};
    assert(doe_dx_hold_buffers(&first, uses, 1));
    expect_transition(&list, 12, &re, copy, srv);
    /* Close records nothing: decay ends every tracked state. */
    const unsigned closed = list.barrier_calls;
    doe_dx_forget_buffers(&first);
    assert(!first.held_count && list.barrier_calls == closed);

    /* The next recording sees a stale serial as untouched and promotes again. */
    doe_dx_native_commands second = {.context=&context,.list=&list.iface,.serial=++context.recording_serial};
    uses[0] = (doe_dx_buffer_state){&a, srv};
    uses[1] = (doe_dx_buffer_state){&e, uav};
    assert(doe_dx_hold_buffers(&second, uses, 2));
    assert(list.barrier_calls == closed && a.held_serial == second.serial);
    doe_dx_forget_buffers(&second);

    /* Row-by-row copies out of one DEFAULT buffer: the source is promoted to
     * COPY_SOURCE once and stays there; a later copy into it returns it to
     * COPY_DEST (write after read) and a dispatch then transitions it. */
    doe_dx_native_commands copies = {.context=&context,.list=&list.iface,.serial=++context.recording_serial};
    ID3D12Resource rf = {0}, rg = {0};
    doe_dx_native_buffer staging = {.resource=&rf,.memory=DOE_DX_DEFAULT}, readback = {.resource=&rg,.memory=DOE_DX_READBACK};
    const unsigned calls = list.barrier_calls, start = list.barrier_count;
    for (UINT64 row = 0; row < 8; ++row) doe_dx_native_copy(&copies, &staging, row * 256, &readback, row * 256, 256);
    assert(list.copies == 8 && list.barrier_calls == calls && staging.held_state == D3D12_RESOURCE_STATE_COPY_SOURCE);
    assert(readback.held_serial != copies.serial); /* Fixed-state heaps are never tracked. */
    doe_dx_native_buffer other = {.resource=&rc,.memory=DOE_DX_DEFAULT};
    doe_dx_native_copy(&copies, &other, 0, &staging, 0, 256);
    assert(list.barrier_calls == calls + 1 && list.barrier_count == start + 1);
    expect_transition(&list, start, &rf, D3D12_RESOURCE_STATE_COPY_SOURCE, copy);
    assert(other.held_state == D3D12_RESOURCE_STATE_COPY_SOURCE && staging.held_state == copy);
    uses[0] = (doe_dx_buffer_state){&staging, srv};
    assert(doe_dx_hold_buffers(&copies, uses, 1));
    expect_transition(&list, start + 1, &rf, copy, srv);
    doe_dx_forget_buffers(&copies);
    free(copies.held);
    free(first.held);
    free(second.held);

    /* Unheld round trip used when the held list cannot grow. */
    doe_dx_buffer_state round[2] = {{&a, uav}, {&b, copy}};
    const unsigned count = list.barrier_count;
    doe_dx_apply_buffer_states(&list.iface, round, 2, false);
    doe_dx_apply_buffer_states(&list.iface, round, 2, true);
    assert(list.barrier_count == count + 2);
    expect_transition(&list, count, &ra, copy, uav);
    expect_transition(&list, count + 1, &ra, uav, copy);
    /* Merging duplicate uses ORs their states. */
    doe_dx_buffer_state merged[2];
    size_t used = doe_dx_add_buffer_state(merged, 0, &a, cbv);
    used = doe_dx_add_buffer_state(merged, used, &a, srv);
    assert(used == 1 && merged[0].state == (cbv | srv));

    /* Recorded bindings: equal pipeline/root/heaps are not set again; a root or
     * heap change forgets the bound descriptor tables. */
    doe_dx_native_commands third = {.context=&context,.list=&list.iface,.serial=++context.recording_serial};
    ID3D12PipelineState *state = (ID3D12PipelineState *)&rc, *other_state = (ID3D12PipelineState *)&rb;
    ID3D12RootSignature *root = (ID3D12RootSignature *)&ra, *other_root = (ID3D12RootSignature *)&rb;
    ID3D12DescriptorHeap *heap = (ID3D12DescriptorHeap *)&rc, *sampler = (ID3D12DescriptorHeap *)&rb;
    doe_dx_descriptor_use use = {0};
    doe_dx_bind_state(&third, state);
    doe_dx_bind_state(&third, state);
    doe_dx_bind_state(&third, other_state);
    assert(list.pipelines == 2);
    assert(doe_dx_bind_compute_root(&third, root) && !doe_dx_bind_compute_root(&third, root));
    assert(doe_dx_bind_graphics_root(&third, root) && list.compute_roots == 1 && list.graphics_roots == 1);
    third.bound.compute_use = &use;
    third.bound.graphics_use = &use;
    doe_dx_bind_heaps(&third, &heap, 1);
    assert(list.heap_sets == 1 && !third.bound.compute_use && !third.bound.graphics_use);
    third.bound.compute_use = &use;
    doe_dx_bind_heaps(&third, &heap, 1);
    assert(list.heap_sets == 1 && third.bound.compute_use == &use);
    ID3D12DescriptorHeap *pair[2] = {heap, sampler};
    doe_dx_bind_heaps(&third, pair, 2);
    assert(list.heap_sets == 2 && !third.bound.compute_use);
    third.bound.compute_use = &use;
    assert(doe_dx_bind_compute_root(&third, other_root) && !third.bound.compute_use);

    /* Descriptor reuse compares exactly the fields descriptor writing reads. */
    doe_dx_native_resources x = {0}, y = {0};
    x.buffer_count = 2;
    x.buffers[0] = (doe_dx_native_binding){&a, 0, 256};
    x.buffers[1] = (doe_dx_native_binding){&b, 256, 512};
    x.immediate_words[0] = 7;
    doe_dx_copy_resources(&y, &x);
    assert(doe_dx_same_resources(&x, &y) && !y.immediate_words[0]);
    y.buffers[1].offset = 0;
    assert(!doe_dx_same_resources(&x, &y));
    doe_dx_copy_resources(&y, &x);
    y.buffers[5] = (doe_dx_native_binding){&c, 0, 4}; /* Beyond the counts: ignored. */
    assert(doe_dx_same_resources(&x, &y));
    y.buffer_count = 1;
    assert(!doe_dx_same_resources(&x, &y));
    puts("PASS actual SDK CPU tracked buffer states (implicit first-use promotion, UAV->UAV barrier, read unions, "
        "write->read, indirect arguments, single-call batching, copy-named and full settles, untracked fallback, "
        "barrier-free row copies from one source, "
        "barrier-free Close, stale serial), unheld round trip, recorded binding skips and descriptor reuse "
        "keys; no device/GPU");
    return 0;
}
