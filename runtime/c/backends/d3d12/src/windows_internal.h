/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_WINDOWS_INTERNAL_H
#define DOE_DX_WINDOWS_INTERNAL_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include "native.h"
#include <stdlib.h>
#include <string.h>

struct doe_dx_native_context {
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12Fence *fence;
};
struct doe_dx_native_buffer {
    ID3D12Resource *resource;
    uint64_t size;
    doe_dx_memory memory;
};
struct doe_dx_native_pipeline {
    ID3D12RootSignature *root;
    ID3D12PipelineState *state;
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
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
doe_dx_status doe_dx_hresult(HRESULT result, const char *operation, doe_dx_error *error);
void doe_dx_transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
#endif
#endif
