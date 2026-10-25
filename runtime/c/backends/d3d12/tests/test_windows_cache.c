/* SPDX-License-Identifier: MIT
 * Actual SDK descriptor-value controls only; no device, PSO or driver cache. */
#include "../src/windows_cache.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
int main(void)
{
    uint8_t root[32],other_root[32];
    doe_dx_cache_root_key("root-one",8,root);
    doe_dx_cache_root_key("root-two",8,other_root);
    uint8_t code_a[]={1,2,3,4},code_b[]={1,2,3,4};
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute={0};
    compute.pRootSignature=(ID3D12RootSignature *)(uintptr_t)1;
    compute.CS=(D3D12_SHADER_BYTECODE){code_a,sizeof(code_a)};
    WCHAR a[65],b[65];
    assert(doe_dx_cache_compute_key(&compute,root,a));
    compute.pRootSignature=(ID3D12RootSignature *)(uintptr_t)2;
    compute.CS.pShaderBytecode=code_b;
    assert(doe_dx_cache_compute_key(&compute,root,b) && !wcscmp(a,b));
    assert(doe_dx_cache_compute_key(&compute,other_root,b) && wcscmp(a,b));
    code_b[0]=2;
    assert(doe_dx_cache_compute_key(&compute,root,b) && wcscmp(a,b));
    compute.CachedPSO.CachedBlobSizeInBytes=1;
    assert(!doe_dx_cache_compute_key(&compute,root,b));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics={0};
    graphics.VS=(D3D12_SHADER_BYTECODE){code_a,sizeof(code_a)};
    graphics.PS=(D3D12_SHADER_BYTECODE){code_b,sizeof(code_b)};
    graphics.NumRenderTargets=1; graphics.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics.SampleDesc.Count=1;
    char first[]="TEXCOORD",second[]="TEXCOORD";
    D3D12_INPUT_ELEMENT_DESC input={first,0,DXGI_FORMAT_R32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
    graphics.InputLayout=(D3D12_INPUT_LAYOUT_DESC){&input,1};
    assert(doe_dx_cache_graphics_key(&graphics,root,a));
    input.SemanticName=second;
    assert(doe_dx_cache_graphics_key(&graphics,root,b) && !wcscmp(a,b));
    input.SemanticIndex=1;
    assert(doe_dx_cache_graphics_key(&graphics,root,b) && wcscmp(a,b));
    input.SemanticIndex=0; graphics.SampleMask=3;
    assert(doe_dx_cache_graphics_key(&graphics,root,b) && wcscmp(a,b));
    graphics.SampleMask=0; graphics.RTVFormats[1]=DXGI_FORMAT_R32_UINT;
    assert(doe_dx_cache_graphics_key(&graphics,root,b) && wcscmp(a,b));
    graphics.RTVFormats[1]=DXGI_FORMAT_UNKNOWN; graphics.DepthStencilState.StencilReadMask=1;
    assert(doe_dx_cache_graphics_key(&graphics,root,b) && wcscmp(a,b));
    puts("D3D12 cache key SDK controls: exact root/code/state values, pointer-independent identity; no native cache execution");
    return 0;
}
