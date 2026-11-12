/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_WINDOWS_INTERNAL_H
#define DOE_DX_WINDOWS_INTERNAL_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcommon.h>
#include <d3d11on12.h>
#include "native.h"
#include "../include/doe_d3d12_shared.h"
#include <stdlib.h>
#include <string.h>

struct doe_dx_pipeline_cache;
struct doe_dx_buffer_heap;
struct doe_dx_descriptor_ring;
#define DOE_DX_IDLE_COMMAND_PAIRS 4u
struct doe_dx_native_context {
    struct doe_dx_pipeline_cache *cache;
    uint64_t cache_driver_version;
    bool cache_driver_known;
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12Fence *fence;
    HANDLE shared_timeline_handle;
    HANDLE completion_event;
    bool completion_registered;
    doe_dx_wait_record *waits; /* Native event registrations, never GPU submissions. */
    ID3D12CommandSignature *dispatch_signature;
    ID3D12CommandSignature *draw_signature, *draw_indexed_signature;
    /* Closed pairs from successfully retired commands, reused oldest first. */
    ID3D12CommandAllocator *idle_allocators[DOE_DX_IDLE_COMMAND_PAIRS];
    ID3D12GraphicsCommandList *idle_lists[DOE_DX_IDLE_COMMAND_PAIRS];
    uint32_t idle_pairs;
    struct doe_dx_buffer_heap *buffer_heaps; /* Placed-buffer heaps, one memory class each. */
    struct doe_dx_descriptor_ring *descriptor_rings; /* Shader-visible CBV/SRV/UAV ranges. */
    doe_dx_limits limits;
    uint64_t recording_serial; /* Last native recording identity; never zero once used. */
};
struct doe_dx_native_buffer {
    ID3D12Resource *resource;
    void *upload_mapping;
    uint64_t upload_written;
    uint64_t size;
    doe_dx_memory memory;
    struct doe_dx_buffer_heap *heap; /* Non-NULL for a placed allocation. */
    uint64_t heap_mask;              /* Exactly the 64 KiB slots this resource owns. */
    bool zeroed; /* D3D12 zeroed it at creation: committed, or never-placed slots. */
    /* Shader state a dispatch left this buffer in, valid only while held_serial
     * names the recording that holds it (see windows_states.c); otherwise the
     * buffer rests at COPY_DEST. The holding recording owns a buffer lease. */
    uint64_t held_serial;
    D3D12_RESOURCE_STATES held_state;
};
struct doe_dx_native_texture {
    ID3D12Resource *resource;
    D3D12_RESOURCE_DESC description;
    doe_dx_format format;
    bool resolve_supported;
    bool shared; /* Simultaneous-access imports rest at COMMON. */
    bool keyed_acquired;
    IDXGIKeyedMutex *keyed_mutex;
    ID3D11Resource *wrapped;
    ID3D11On12Device *on12;
    ID3D11DeviceContext *on12_context;
    HMODULE on12_module;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT *footprints;
    uint32_t footprint_count;
    uint64_t upload_bytes;
};
struct doe_dx_native_timestamps { ID3D12QueryHeap *heap; D3D12_QUERY_TYPE kind; };
typedef struct doe_dx_windows_render_layout {
    uint32_t immediate_root[DOE_DX_RENDER_STAGES],storage_sizes_root[DOE_DX_RENDER_STAGES];
    bool stencil_test;
    uint32_t draw_offsets_root;
    ID3D12CommandSignature *draw_signature, *draw_indexed_signature;
    D3D_PRIMITIVE_TOPOLOGY topology;
    doe_dx_binding_plan stages[DOE_DX_RENDER_STAGES];
    uint32_t resource_table[DOE_DX_RENDER_STAGES],sampler_table[DOE_DX_RENDER_STAGES];
    uint32_t resource_offset[DOE_DX_RENDER_STAGES],sampler_offset[DOE_DX_RENDER_STAGES];
} doe_dx_windows_render_layout;
typedef struct doe_dx_resource_set {
    const doe_dx_binding_layout *buffers;
    const doe_dx_texture_binding_layout *textures;
    const doe_dx_native_resources *resources;
} doe_dx_resource_set;
struct doe_dx_native_pipeline {
    uint32_t immediate_root,immediate_words,storage_sizes_root;
    uint8_t root_digest[32];
    ID3D12RootSignature *root;
    ID3D12PipelineState *state;
    uint32_t resource_table, sampler_table;
    doe_dx_windows_render_layout *render;
    doe_dx_vertex_layout vertex_buffers[DOE_DX_MAX_VERTEX_BUFFERS];
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_binding_layout textures[DOE_DX_MAX_BINDINGS];
};
typedef struct doe_dx_descriptor_use {
    D3D12_GPU_VIRTUAL_ADDRESS storage_sizes[DOE_DX_RENDER_STAGES]; /* Ring constant slots; 0 when absent. */
    ID3D12DescriptorHeap *heap;
    ID3D12DescriptorHeap *sampler_heap;
    struct doe_dx_descriptor_ring *ring; /* Borrowed heap range instead of an owned heap. */
    UINT ring_first, ring_count;
    UINT constant_first, constant_count; /* Storage-size slots in the same ring. */
    struct doe_dx_descriptor_use *next;
} doe_dx_descriptor_use;
/* GPU start of this use's resource descriptors (heap start plus ring offset). */
void doe_dx_descriptor_gpu_start(const doe_dx_descriptor_use *use, D3D12_GPU_DESCRIPTOR_HANDLE *out);
/* Returns a ring range; called only when the owning command is discarded,
 * terminally lost or successfully retired, never while GPU use may remain. */
void doe_dx_descriptor_ring_free(doe_dx_descriptor_use *use);
void doe_dx_descriptor_rings_close(doe_dx_native_context *context);
#define DOE_DX_DESCRIPTOR_CONSTANT_SLOT 256u /* Root-CBV alignment. */
/* A storage-size table a published use of the recording owns (address 0:
 * none). prepare_sets reuses it for an equal one-slot table and records the
 * last one-slot table it writes; NULL disables sharing. The caller passes a
 * cache whose owner is published, or retires with the uses it prepares. */
typedef struct doe_dx_sizes_cache {
    D3D12_GPU_VIRTUAL_ADDRESS address;
    uint8_t bytes[DOE_DX_DESCRIPTOR_CONSTANT_SLOT];
} doe_dx_sizes_cache;
/* Borrows count contiguous descriptors and slots contiguous 256-byte constant
 * slots (at most 256, needing count>0) from one ring. Slots return with the
 * descriptors; the CPU pointer is the slot's persistently mapped bytes. */
doe_dx_status doe_dx_descriptor_ring_take_slots(doe_dx_native_context *context, UINT count, UINT slots,
    doe_dx_descriptor_use *use, D3D12_CPU_DESCRIPTOR_HANDLE *cpu, doe_dx_error *error);
uint8_t *doe_dx_descriptor_constants(const doe_dx_descriptor_use *use, UINT slot);
/* Borrows count contiguous descriptors into use and returns their CPU start. */
doe_dx_status doe_dx_descriptor_ring_take(doe_dx_native_context *context, UINT count,
    doe_dx_descriptor_use *use, D3D12_CPU_DESCRIPTOR_HANDLE *cpu, doe_dx_error *error);
doe_dx_status doe_dx_prepare_color_view(doe_dx_native_commands *,doe_dx_native_texture *,const doe_dx_texture_view_desc *,
    uint32_t,doe_dx_descriptor_use **,D3D12_CPU_DESCRIPTOR_HANDLE *,doe_dx_error *);
doe_dx_status doe_dx_prepare_depth_view(doe_dx_native_commands *,doe_dx_native_texture *,const doe_dx_texture_view_desc *,
    bool,bool,doe_dx_descriptor_use **,D3D12_CPU_DESCRIPTOR_HANDLE *,doe_dx_error *);
UINT doe_dx_storage_size_slots(const doe_dx_native_resources *);
doe_dx_status doe_dx_write_storage_sizes(const doe_dx_native_resources *,uint8_t *,size_t,doe_dx_error *);
struct doe_dx_shared_wait;
/* Native list bindings already recorded, so equal sets are skipped. A root
 * signature or heap change forgets the bound tables (D3D12 invalidates them). */
typedef struct doe_dx_bound_state {
    ID3D12PipelineState *state;
    ID3D12RootSignature *compute_root, *graphics_root;
    ID3D12DescriptorHeap *heaps[2];
    UINT heap_count;
    const doe_dx_descriptor_use *compute_use, *graphics_use;
    /* Storage-size root CBVs set under the current root signatures. */
    D3D12_GPU_VIRTUAL_ADDRESS compute_sizes, graphics_sizes[DOE_DX_RENDER_STAGES];
} doe_dx_bound_state;
/* The descriptor use of the previous compute dispatch and the exact bindings
 * it was written for; an identical next dispatch borrows the same range. */
typedef struct doe_dx_compute_reuse {
    const doe_dx_native_pipeline *pipeline;
    doe_dx_descriptor_use *use;
    doe_dx_native_resources resources;
} doe_dx_compute_reuse;
struct doe_dx_native_commands {
    doe_dx_native_context *context; /* Context cannot close with live commands. */
    struct doe_dx_shared_wait *shared_waits;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    doe_dx_descriptor_use *descriptors;
    uint64_t serial; /* Unique recording identity for held buffer states. */
    doe_dx_native_buffer **held; /* Buffers touched in this list; leases are this command's. */
    size_t held_count, held_capacity;
    bool untracked; /* An operation touched buffers without reporting them. */
    doe_dx_bound_state bound;
    doe_dx_compute_reuse reuse;
    doe_dx_sizes_cache sizes; /* Owned by a use already in descriptors. */
};
typedef struct doe_dx_buffer_state {
    doe_dx_native_buffer *buffer;
    D3D12_RESOURCE_STATES state;
} doe_dx_buffer_state;
/* Buffer state tracking (windows_states.c). Settle returns every tracked
 * buffer to COPY_DEST and marks the recording untracked, for operations that
 * do not report their buffers. Settle-tracked does so for the named buffers
 * only (NULL entries skipped) and records them as touched at COPY_DEST. Forget
 * drops tracking at Close, where buffer decay ends every state. */
void doe_dx_settle_buffers(doe_dx_native_commands *commands);
void doe_dx_settle_tracked(doe_dx_native_commands *commands, doe_dx_native_buffer *const *buffers, size_t count);
void doe_dx_forget_buffers(doe_dx_native_commands *commands);
/* Moves each unique buffer to its dispatch state from its tracked state (an
 * untouched buffer is promoted implicitly, or is at COPY_DEST once untracked)
 * in one barrier call, with a UAV barrier for UAV->UAV, and tracks it. False,
 * with nothing recorded, when the tracked list cannot grow. */
bool doe_dx_hold_buffers(doe_dx_native_commands *commands, const doe_dx_buffer_state *states, size_t count);
/* COPY_DEST <-> state transitions for unique states, batched; restore reverses. */
void doe_dx_apply_buffer_states(ID3D12GraphicsCommandList *list, const doe_dx_buffer_state *states,
    size_t count, bool restore);
/* Appends or merges (state OR) one buffer use; returns the new count. */
size_t doe_dx_add_buffer_state(doe_dx_buffer_state *states, size_t count, doe_dx_native_buffer *buffer,
    D3D12_RESOURCE_STATES state);
size_t doe_dx_set_buffer_states(const doe_dx_resource_set *sets, size_t count, bool graphics,
    doe_dx_buffer_state *out);
/* Whole-pass transitions (windows_transitions.c). Begin records nothing and
 * returns false when the pass cannot use one state per resource or storage
 * is unavailable; the caller then keeps per-draw transitions. */
typedef struct doe_dx_pass_states doe_dx_pass_states;
bool doe_dx_pass_states_begin(doe_dx_native_commands *commands, const doe_dx_native_render_draw *draws,
    size_t count, doe_dx_native_texture *attachment_depth, doe_dx_pass_states **out);
void doe_dx_pass_states_draw(doe_dx_native_commands *commands, doe_dx_pass_states *pass,
    const doe_dx_native_render_draw *draw);
void doe_dx_pass_states_end(doe_dx_native_commands *commands, doe_dx_pass_states *pass);
void doe_dx_transition_set_textures(doe_dx_native_commands *commands, const doe_dx_resource_set *sets,
    size_t count, bool graphics, bool restore);
void doe_dx_bind_state(doe_dx_native_commands *commands, ID3D12PipelineState *state);
bool doe_dx_bind_compute_root(doe_dx_native_commands *commands, ID3D12RootSignature *root);
bool doe_dx_bind_graphics_root(doe_dx_native_commands *commands, ID3D12RootSignature *root);
void doe_dx_bind_heaps(doe_dx_native_commands *commands, ID3D12DescriptorHeap *const *heaps, UINT count);
bool doe_dx_same_resources(const doe_dx_native_resources *a, const doe_dx_native_resources *b);
void doe_dx_copy_resources(doe_dx_native_resources *to, const doe_dx_native_resources *from);
ID3D12Fence *doe_dx_shared_fence_native(const doe_dx_shared_fence *);
doe_dx_status doe_dx_shared_waits_execute(doe_dx_native_context *,doe_dx_native_commands *,doe_dx_error *);
void doe_dx_shared_waits_close(doe_dx_native_commands *);
void doe_dx_native_commands_cache_close(doe_dx_native_context *context);
/* Releases empty placed-buffer heaps; a heap with live placements is detached
 * and released by its last buffer. Context close normally finds none live. */
void doe_dx_buffer_heaps_close(doe_dx_native_context *context);
doe_dx_status doe_dx_open_adapter(IDXGIFactory4 *factory, const doe_dx_options *options,
    IDXGIAdapter1 **out, doe_dx_caps *caps, doe_dx_error *error);
static inline D3D12_RESOURCE_STATES doe_dx_texture_rest(const doe_dx_native_texture *texture) {
    return texture->shared ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_DEST;
}
void doe_dx_shared_texture_native_close(doe_dx_native_texture *);
DXGI_FORMAT doe_dx_texture_resource_format(doe_dx_format);
void doe_dx_texture_srv(const doe_dx_native_texture *,const doe_dx_texture_view_desc *,D3D12_SHADER_RESOURCE_VIEW_DESC *);
void doe_dx_texture_uav(const doe_dx_native_texture *,const doe_dx_texture_view_desc *,D3D12_UNORDERED_ACCESS_VIEW_DESC *);
doe_dx_status doe_dx_texture_native_footprints(doe_dx_native_context *,doe_dx_native_texture *,doe_dx_error *);
doe_dx_status doe_dx_query_caps(doe_dx_native_context *context, doe_dx_caps *out, doe_dx_error *error);
doe_dx_status doe_dx_hresult(HRESULT result, const char *operation, doe_dx_error *error);
doe_dx_status doe_dx_create_root(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline *pipeline,
    bool input_assembler, doe_dx_error *error);
void doe_dx_sampler_native(const doe_dx_sampler_desc *sampler,D3D12_SAMPLER_DESC *out);
void doe_dx_bind_descriptors(doe_dx_native_commands *commands,doe_dx_native_pipeline *pipeline,
    doe_dx_descriptor_use *use,bool graphics);
void doe_dx_release_descriptors(doe_dx_descriptor_use *use);
void doe_dx_transition_resources(doe_dx_native_commands *commands, doe_dx_native_pipeline *pipeline,
    const doe_dx_native_resources *resources, bool graphics, bool restore);
doe_dx_status doe_dx_input_layout_rows(doe_dx_native_context *,const doe_dx_vertex_layout *,size_t,
    const doe_dx_vertex_attribute *,size_t,doe_dx_native_pipeline *,D3D12_INPUT_ELEMENT_DESC *,doe_dx_error *);
void doe_dx_bind_vertex_views(doe_dx_native_commands *,doe_dx_native_pipeline *,const doe_dx_native_binding *,size_t);
void doe_dx_transition_render_sets(doe_dx_native_commands *,const doe_dx_resource_set *,size_t,
    const doe_dx_native_render_draw *,bool);
void doe_dx_transition_vertex_sets(doe_dx_native_commands *,const doe_dx_resource_set *,size_t,
    const doe_dx_native_binding *,size_t,bool);
doe_dx_status doe_dx_input_layout(doe_dx_native_context *context, const doe_dx_mesh_pipeline_desc *desc,
    doe_dx_native_pipeline *pipeline, D3D12_INPUT_ELEMENT_DESC *out, doe_dx_error *error);
D3D12_COMPARISON_FUNC doe_dx_depth_compare(doe_dx_compare compare);
void doe_dx_geometry_transition(doe_dx_native_commands *commands, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *draw, bool restore);
void doe_dx_bind_geometry(doe_dx_native_commands *commands, doe_dx_native_pipeline *pipeline,
    const doe_dx_native_geometry *geometry, const doe_dx_mesh_draw_desc *draw);
DXGI_FORMAT doe_dx_texture_format(doe_dx_format format);
doe_dx_status doe_dx_prepare_sets(doe_dx_native_commands *,const doe_dx_resource_set *,size_t,
    doe_dx_sizes_cache *,doe_dx_descriptor_use **,doe_dx_error *);
void doe_dx_transition_sets(doe_dx_native_commands *,const doe_dx_resource_set *,size_t,bool,bool);
doe_dx_status doe_dx_prepare_depth_target(doe_dx_native_commands *,doe_dx_native_texture *,
    doe_dx_descriptor_use **,D3D12_CPU_DESCRIPTOR_HANDLE *,doe_dx_error *);
doe_dx_status doe_dx_prepare_color_target(doe_dx_native_commands *,doe_dx_native_texture *,
    doe_dx_descriptor_use **,D3D12_CPU_DESCRIPTOR_HANDLE *,doe_dx_error *);
void doe_dx_graphics_defaults(D3D12_GRAPHICS_PIPELINE_STATE_DESC *,doe_dx_format);
bool doe_dx_descriptor_range_type(doe_dx_descriptor_kind,D3D12_DESCRIPTOR_RANGE_TYPE *);
doe_dx_status doe_dx_prepare_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_descriptor_use **out, doe_dx_error *error);
void doe_dx_transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
#endif
#endif
