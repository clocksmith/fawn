/* SPDX-License-Identifier: MIT */
/* Per-recording buffer states and already-recorded list bindings.
 *
 * Default buffers rest at COPY_DEST between the operations that do not track
 * buffer state (README, "Default buffers use the retained COPY_DEST operation
 * boundary"). Every executed list ends in buffer decay: D3D12 returns each
 * buffer to COMMON when ExecuteCommandLists completes, and COMMON buffers are
 * implicitly promoted to the state of their first use in the next list
 * (Microsoft "Implicit state transitions"; Dawn relies on the same rules in
 * d3d12/BufferD3D12.cpp TrackUsageAndGetResourceBarrier).
 *
 * A recording therefore tracks the buffers it has touched:
 * - A dispatch moves each buffer from its tracked state to the dispatch state
 *   in one barrier call: no barrier for a first use in the list (implicit
 *   promotion), a transition when the state changes, a UAV barrier when a
 *   buffer stays UNORDERED_ACCESS, nothing for a read the state already covers.
 *   The buffer stays in that state for the next dispatch.
 * - A buffer copy returns its destination to COPY_DEST and tracks a DEFAULT
 *   source at COPY_SOURCE through the same rules, so repeated copies out of
 *   one buffer record no barrier; write ordering returns the buffers it names
 *   to COPY_DEST and tracks them there.
 * - Every other recording that can reach a buffer (texture transfers, query
 *   resolves, draws and render passes) settles all tracked buffers back to
 *   COPY_DEST first and marks the recording untracked: from then on an
 *   untouched buffer is assumed at COPY_DEST, the original explicit contract,
 *   because those operations do not report which buffers they touched.
 * - Close records nothing: decay ends every tracked state.
 *
 * Tracked state lives on the native buffer, tagged with the recording's unique
 * serial. A buffer belongs to one unsubmitted recording at a time (the owner's
 * exclusive reservation), so a serial that is not this recording's means the
 * buffer is untouched in this list: an earlier recording either executed (and
 * the buffer decayed) or never executed. */
#include "windows_internal.h"
#ifdef _WIN32

enum { BARRIER_CHUNK = 32 };

static const D3D12_RESOURCE_STATES READ_STATES = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
    D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
    D3D12_RESOURCE_STATE_COPY_SOURCE;

static bool read_only(D3D12_RESOURCE_STATES state)
{
    return state && !(state & ~READ_STATES);
}

static void transition(D3D12_RESOURCE_BARRIER *barrier, ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    *barrier = (D3D12_RESOURCE_BARRIER){.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    barrier->Transition.pResource = resource;
    barrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier->Transition.StateBefore = before;
    barrier->Transition.StateAfter = after;
}

static bool reserve_held(doe_dx_native_commands *commands, size_t added)
{
    if (added <= commands->held_capacity - commands->held_count) return true;
    const size_t limit = SIZE_MAX / sizeof(*commands->held);
    if (added > limit - commands->held_count) return false;
    const size_t needed = commands->held_count + added;
    size_t capacity = commands->held_capacity ? commands->held_capacity : 64u;
    while (capacity < needed) capacity = capacity <= limit / 2 ? capacity * 2 : needed;
    doe_dx_native_buffer **grown = realloc(commands->held, capacity * sizeof(*grown));
    if (!grown) return false;
    commands->held = grown;
    commands->held_capacity = capacity;
    return true;
}

/* Moves tracked buffers (all, or only those named) to COPY_DEST in batched
 * barriers; they stay tracked there. */
static void settle(doe_dx_native_commands *commands, doe_dx_native_buffer *const *only, size_t only_count)
{
    D3D12_RESOURCE_BARRIER barriers[BARRIER_CHUNK];
    UINT pending = 0;
    const size_t count = only ? only_count : commands->held_count;
    for (size_t index = 0; index < count; ++index) {
        doe_dx_native_buffer *buffer = only ? only[index] : commands->held[index];
        if (!buffer || buffer->held_serial != commands->serial) continue;
        if (buffer->held_state == D3D12_RESOURCE_STATE_COPY_DEST) continue;
        transition(&barriers[pending++], buffer->resource, buffer->held_state, D3D12_RESOURCE_STATE_COPY_DEST);
        buffer->held_state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (pending == BARRIER_CHUNK) {
            ID3D12GraphicsCommandList_ResourceBarrier(commands->list, pending, barriers);
            pending = 0;
        }
    }
    if (pending) ID3D12GraphicsCommandList_ResourceBarrier(commands->list, pending, barriers);
}

void doe_dx_settle_buffers(doe_dx_native_commands *commands)
{
    settle(commands, NULL, 0);
    commands->untracked = true;
}

void doe_dx_settle_tracked(doe_dx_native_commands *commands, doe_dx_native_buffer *const *buffers, size_t count)
{
    settle(commands, buffers, count);
    size_t added = 0;
    for (size_t index = 0; index < count; ++index)
        if (buffers[index] && buffers[index]->held_serial != commands->serial) added++;
    if (!reserve_held(commands, added)) {
        /* Cannot remember them: fall back to the explicit COPY_DEST contract. */
        commands->untracked = true;
        return;
    }
    for (size_t index = 0; index < count; ++index) {
        doe_dx_native_buffer *buffer = buffers[index];
        if (!buffer || buffer->held_serial == commands->serial) continue;
        commands->held[commands->held_count++] = buffer;
        buffer->held_serial = commands->serial;
        buffer->held_state = D3D12_RESOURCE_STATE_COPY_DEST;
    }
}

void doe_dx_forget_buffers(doe_dx_native_commands *commands)
{
    commands->held_count = 0;
}

bool doe_dx_hold_buffers(doe_dx_native_commands *commands, const doe_dx_buffer_state *states, size_t count)
{
    size_t added = 0;
    for (size_t index = 0; index < count; ++index)
        if (states[index].buffer->held_serial != commands->serial) added++;
    if (!reserve_held(commands, added)) return false;
    D3D12_RESOURCE_BARRIER barriers[BARRIER_CHUNK];
    UINT pending = 0;
    for (size_t index = 0; index < count; ++index) {
        doe_dx_native_buffer *buffer = states[index].buffer;
        const D3D12_RESOURCE_STATES wanted = states[index].state;
        const bool held = buffer->held_serial == commands->serial;
        const D3D12_RESOURCE_STATES current = held ? buffer->held_state :
            commands->untracked ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES next = wanted;
        if (current == D3D12_RESOURCE_STATE_COMMON) {
            /* First use in this list: implicit promotion from the decayed state. */
        } else if (current == wanted && wanted == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            barriers[pending] = (D3D12_RESOURCE_BARRIER){.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV};
            barriers[pending++].UAV.pResource = buffer->resource;
        } else if (read_only(current) && read_only(wanted)) {
            /* Read to read needs no ordering; keep the union so alternating
             * read kinds stop transitioning. */
            next = current | wanted;
            if (next != current) transition(&barriers[pending++], buffer->resource, current, next);
        } else if (current != wanted) {
            transition(&barriers[pending++], buffer->resource, current, wanted);
        }
        if (!held) commands->held[commands->held_count++] = buffer;
        buffer->held_serial = commands->serial;
        buffer->held_state = next;
        if (pending == BARRIER_CHUNK) {
            ID3D12GraphicsCommandList_ResourceBarrier(commands->list, pending, barriers);
            pending = 0;
        }
    }
    if (pending) ID3D12GraphicsCommandList_ResourceBarrier(commands->list, pending, barriers);
    return true;
}

void doe_dx_apply_buffer_states(ID3D12GraphicsCommandList *list, const doe_dx_buffer_state *states,
    size_t count, bool restore)
{
    D3D12_RESOURCE_BARRIER barriers[BARRIER_CHUNK];
    UINT pending = 0;
    for (size_t index = 0; index < count; ++index) {
        const D3D12_RESOURCE_STATES state = states[index].state;
        if (state == D3D12_RESOURCE_STATE_COPY_DEST) continue;
        transition(&barriers[pending++], states[index].buffer->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST, restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
        if (pending == BARRIER_CHUNK) {
            ID3D12GraphicsCommandList_ResourceBarrier(list, pending, barriers);
            pending = 0;
        }
    }
    if (pending) ID3D12GraphicsCommandList_ResourceBarrier(list, pending, barriers);
}

size_t doe_dx_add_buffer_state(doe_dx_buffer_state *states, size_t count, doe_dx_native_buffer *buffer,
    D3D12_RESOURCE_STATES state)
{
    for (size_t index = 0; index < count; ++index) {
        if (states[index].buffer != buffer) continue;
        states[index].state |= state;
        return count;
    }
    states[count] = (doe_dx_buffer_state){buffer, state};
    return count + 1;
}

void doe_dx_bind_state(doe_dx_native_commands *commands, ID3D12PipelineState *state)
{
    if (commands->bound.state == state) return;
    ID3D12GraphicsCommandList_SetPipelineState(commands->list, state);
    commands->bound.state = state;
}

bool doe_dx_bind_compute_root(doe_dx_native_commands *commands, ID3D12RootSignature *root)
{
    if (commands->bound.compute_root == root) return false;
    ID3D12GraphicsCommandList_SetComputeRootSignature(commands->list, root);
    commands->bound.compute_root = root;
    commands->bound.compute_use = NULL;
    commands->bound.compute_sizes = 0;
    return true;
}

bool doe_dx_bind_graphics_root(doe_dx_native_commands *commands, ID3D12RootSignature *root)
{
    if (commands->bound.graphics_root == root) return false;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(commands->list, root);
    commands->bound.graphics_root = root;
    commands->bound.graphics_use = NULL;
    for (size_t stage = 0; stage < DOE_DX_RENDER_STAGES; ++stage) commands->bound.graphics_sizes[stage] = 0;
    return true;
}

void doe_dx_bind_heaps(doe_dx_native_commands *commands, ID3D12DescriptorHeap *const *heaps, UINT count)
{
    doe_dx_bound_state *bound = &commands->bound;
    if (count == bound->heap_count && (!count || heaps[0] == bound->heaps[0]) &&
        (count < 2 || heaps[1] == bound->heaps[1])) return;
    ID3D12DescriptorHeap *copy[2] = {count ? heaps[0] : NULL, count > 1 ? heaps[1] : NULL};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(commands->list, count, copy);
    bound->heaps[0] = copy[0];
    bound->heaps[1] = copy[1];
    bound->heap_count = count;
    bound->compute_use = NULL;
    bound->graphics_use = NULL;
}

/* Every field that descriptor writing reads (write_set, storage-size table).
 * All compared element types are padding-free. Immediates are root constants,
 * not descriptors, and are set per dispatch/draw. */
bool doe_dx_same_resources(const doe_dx_native_resources *a, const doe_dx_native_resources *b)
{
    return a->buffer_count == b->buffer_count && a->texture_count == b->texture_count &&
        a->sampler_count == b->sampler_count && a->logical_size_count == b->logical_size_count &&
        a->buffer_count <= DOE_DX_MAX_BINDINGS && a->texture_count <= DOE_DX_MAX_BINDINGS &&
        a->sampler_count <= DOE_DX_MAX_DYNAMIC_SAMPLERS && a->logical_size_count <= DOE_DX_MAX_BINDINGS &&
        !memcmp(a->buffers, b->buffers, a->buffer_count * sizeof(a->buffers[0])) &&
        !memcmp(a->textures, b->textures, a->texture_count * sizeof(a->textures[0])) &&
        !memcmp(a->texture_views, b->texture_views, a->texture_count * sizeof(a->texture_views[0])) &&
        !memcmp(a->samplers, b->samplers, a->sampler_count * sizeof(a->samplers[0])) &&
        !memcmp(a->logical_size_indices, b->logical_size_indices,
            a->logical_size_count * sizeof(a->logical_size_indices[0]));
}

/* Copies only the fields doe_dx_same_resources compares. */
void doe_dx_copy_resources(doe_dx_native_resources *to, const doe_dx_native_resources *from)
{
    to->buffer_count = from->buffer_count;
    to->texture_count = from->texture_count;
    to->sampler_count = from->sampler_count;
    to->logical_size_count = from->logical_size_count;
    memcpy(to->buffers, from->buffers, from->buffer_count * sizeof(from->buffers[0]));
    memcpy(to->textures, from->textures, from->texture_count * sizeof(from->textures[0]));
    memcpy(to->texture_views, from->texture_views, from->texture_count * sizeof(from->texture_views[0]));
    memcpy(to->samplers, from->samplers, from->sampler_count * sizeof(from->samplers[0]));
    memcpy(to->logical_size_indices, from->logical_size_indices,
        from->logical_size_count * sizeof(from->logical_size_indices[0]));
}
#endif
