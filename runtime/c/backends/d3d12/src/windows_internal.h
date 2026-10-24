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
#include "native.h"
#include <stdlib.h>
#include <string.h>

struct doe_dx_native_context {
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12Fence *fence;
    ID3D12CommandSignature *dispatch_signature;
    doe_dx_limits limits;
};
struct doe_dx_native_buffer {
    ID3D12Resource *resource;
    uint64_t size;
    doe_dx_memory memory;
};
struct doe_dx_native_texture {
    ID3D12Resource *resource;
    D3D12_RESOURCE_DESC description;
};
struct doe_dx_native_timestamps { ID3D12QueryHeap *heap; };
struct doe_dx_native_pipeline {
    ID3D12RootSignature *root;
    ID3D12PipelineState *state;
    doe_dx_vertex_layout vertex_buffers[DOE_DX_MAX_VERTEX_BUFFERS];
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_binding_layout textures[DOE_DX_MAX_BINDINGS];
};
typedef struct doe_dx_descriptor_use {
    ID3D12DescriptorHeap *heap;
    struct doe_dx_descriptor_use *next;
} doe_dx_descriptor_use;
struct doe_dx_native_commands {
    doe_dx_native_context *context; /* Context cannot close with live commands. */
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    doe_dx_descriptor_use *descriptors;
};
doe_dx_status doe_dx_open_adapter(IDXGIFactory4 *factory, const doe_dx_options *options,
    IDXGIAdapter1 **out, doe_dx_caps *caps, doe_dx_error *error);
doe_dx_status doe_dx_query_caps(doe_dx_native_context *context, doe_dx_caps *out, doe_dx_error *error);
doe_dx_status doe_dx_hresult(HRESULT result, const char *operation, doe_dx_error *error);
doe_dx_status doe_dx_create_root(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline *pipeline,
    bool input_assembler, doe_dx_error *error);
void doe_dx_transition_resources(doe_dx_native_commands *commands, doe_dx_native_pipeline *pipeline,
    const doe_dx_native_resources *resources, bool graphics, bool restore);
doe_dx_status doe_dx_input_layout(doe_dx_native_context *context, const doe_dx_mesh_pipeline_desc *desc,
    doe_dx_native_pipeline *pipeline, D3D12_INPUT_ELEMENT_DESC *out, doe_dx_error *error);
D3D12_COMPARISON_FUNC doe_dx_depth_compare(doe_dx_compare compare);
void doe_dx_geometry_transition(doe_dx_native_commands *commands, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *draw, bool restore);
void doe_dx_bind_geometry(doe_dx_native_commands *commands, doe_dx_native_pipeline *pipeline,
    const doe_dx_native_geometry *geometry, const doe_dx_mesh_draw_desc *draw);
DXGI_FORMAT doe_dx_texture_format(doe_dx_format format);
doe_dx_status doe_dx_prepare_descriptors(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_descriptor_use **out, doe_dx_error *error);
void doe_dx_transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
#endif
#endif
