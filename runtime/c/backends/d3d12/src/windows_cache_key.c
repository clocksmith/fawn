/* SPDX-License-Identifier: MIT */
#include "windows_cache.h"
#ifdef _WIN32
#include "../../../common/include/doe_sha256.h"
/* Hash a bounded canonical value encoding, never native pointers or padding.
 * Root bytes are the actual serialized root signature used by PSO creation. */
typedef struct cache_key { uint8_t bytes[32768]; size_t count; bool valid; } cache_key;
static void key_bytes(cache_key *key,const void *data,size_t size)
{
    if (!key->valid || size > sizeof(key->bytes)-key->count || (size && !data)) { key->valid=false; return; }
    if (size) memcpy(key->bytes+key->count,data,size);
    key->count+=size;
}
static void key_u64(cache_key *key,uint64_t value)
{
    uint8_t bytes[8];
    for (unsigned i=0;i<8;++i) bytes[i]=(uint8_t)(value>>(8u*i));
    key_bytes(key,bytes,sizeof(bytes));
}
static void key_float(cache_key *key,float value)
{
    uint32_t bits; memcpy(&bits,&value,sizeof(bits)); key_u64(key,bits);
}
static void key_string(cache_key *key,const char *value)
{
    if (!value) { key_u64(key,0); return; }
    size_t count=0;
    while (count<256 && value[count]) ++count;
    if (count==256) { key->valid=false; return; }
    key_u64(key,count+1); key_bytes(key,value,count+1);
}
static void key_shader(cache_key *key,D3D12_SHADER_BYTECODE shader)
{
    key_u64(key,shader.BytecodeLength);
    if (!shader.BytecodeLength) return;
    if (!shader.pShaderBytecode) { key->valid=false; return; }
    uint8_t digest[32]; doe_common_sha256(shader.pShaderBytecode,shader.BytecodeLength,digest);
    key_bytes(key,digest,sizeof(digest));
}
static bool key_finish(cache_key *key,WCHAR name[65])
{
    if (!key->valid) return false;
    uint8_t digest[32]; char hex[65];
    doe_common_sha256(key->bytes,key->count,digest); doe_common_sha256_hex(digest,hex);
    for (unsigned i=0;i<65;++i) name[i]=(WCHAR)(unsigned char)hex[i];
    return true;
}
void doe_dx_cache_root_key(const void *bytes,size_t size,uint8_t digest[32])
{
    doe_common_sha256(bytes,size,digest);
}
bool doe_dx_cache_compute_key(const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
    const uint8_t root[32],WCHAR name[65])
{
    cache_key key={.valid=true}; key_u64(&key,1); key_u64(&key,1);
    key_bytes(&key,root,32); key_shader(&key,desc->CS);
    key_u64(&key,desc->NodeMask); key_u64(&key,desc->Flags);
    if (desc->CachedPSO.CachedBlobSizeInBytes) return false;
    return key_finish(&key,name);
}
static void key_stencil(cache_key *key,D3D12_DEPTH_STENCILOP_DESC stencil)
{
    key_u64(key,stencil.StencilFailOp); key_u64(key,stencil.StencilDepthFailOp);
    key_u64(key,stencil.StencilPassOp); key_u64(key,stencil.StencilFunc);
}
bool doe_dx_cache_graphics_key(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *d,
    const uint8_t root[32],WCHAR name[65])
{
    cache_key key={.valid=true}; key_u64(&key,1); key_u64(&key,2); key_bytes(&key,root,32);
    key_shader(&key,d->VS); key_shader(&key,d->PS); key_shader(&key,d->DS);
    key_shader(&key,d->HS); key_shader(&key,d->GS);
    key_u64(&key,d->StreamOutput.NumEntries); key_u64(&key,d->StreamOutput.NumStrides);
    key_u64(&key,d->StreamOutput.RasterizedStream);
    if (d->StreamOutput.NumEntries>128 || d->StreamOutput.NumStrides>4 ||
        (d->StreamOutput.NumEntries && !d->StreamOutput.pSODeclaration) ||
        (d->StreamOutput.NumStrides && !d->StreamOutput.pBufferStrides)) return false;
    for (UINT i=0;i<d->StreamOutput.NumEntries;++i) {
        const D3D12_SO_DECLARATION_ENTRY *row=&d->StreamOutput.pSODeclaration[i];
        key_u64(&key,row->Stream); key_string(&key,row->SemanticName); key_u64(&key,row->SemanticIndex);
        key_u64(&key,row->StartComponent); key_u64(&key,row->ComponentCount); key_u64(&key,row->OutputSlot);
    }
    for (UINT i=0;i<d->StreamOutput.NumStrides;++i) key_u64(&key,d->StreamOutput.pBufferStrides[i]);
    key_u64(&key,d->BlendState.AlphaToCoverageEnable); key_u64(&key,d->BlendState.IndependentBlendEnable);
    for (unsigned i=0;i<8;++i) {
        const D3D12_RENDER_TARGET_BLEND_DESC *r=&d->BlendState.RenderTarget[i];
        key_u64(&key,r->BlendEnable); key_u64(&key,r->LogicOpEnable);
        key_u64(&key,r->SrcBlend); key_u64(&key,r->DestBlend); key_u64(&key,r->BlendOp);
        key_u64(&key,r->SrcBlendAlpha); key_u64(&key,r->DestBlendAlpha); key_u64(&key,r->BlendOpAlpha);
        key_u64(&key,r->LogicOp); key_u64(&key,r->RenderTargetWriteMask);
    }
    key_u64(&key,d->SampleMask);
    const D3D12_RASTERIZER_DESC *r=&d->RasterizerState;
    key_u64(&key,r->FillMode); key_u64(&key,r->CullMode); key_u64(&key,r->FrontCounterClockwise);
    key_u64(&key,(uint32_t)r->DepthBias); key_float(&key,r->DepthBiasClamp); key_float(&key,r->SlopeScaledDepthBias);
    key_u64(&key,r->DepthClipEnable); key_u64(&key,r->MultisampleEnable); key_u64(&key,r->AntialiasedLineEnable);
    key_u64(&key,r->ForcedSampleCount); key_u64(&key,r->ConservativeRaster);
    const D3D12_DEPTH_STENCIL_DESC *z=&d->DepthStencilState;
    key_u64(&key,z->DepthEnable); key_u64(&key,z->DepthWriteMask); key_u64(&key,z->DepthFunc);
    key_u64(&key,z->StencilEnable); key_u64(&key,z->StencilReadMask); key_u64(&key,z->StencilWriteMask);
    key_stencil(&key,z->FrontFace); key_stencil(&key,z->BackFace);
    if (d->InputLayout.NumElements>64 || (d->InputLayout.NumElements && !d->InputLayout.pInputElementDescs)) return false;
    key_u64(&key,d->InputLayout.NumElements);
    for (UINT i=0;i<d->InputLayout.NumElements;++i) {
        const D3D12_INPUT_ELEMENT_DESC *v=&d->InputLayout.pInputElementDescs[i];
        key_string(&key,v->SemanticName); key_u64(&key,v->SemanticIndex); key_u64(&key,v->Format);
        key_u64(&key,v->InputSlot); key_u64(&key,v->AlignedByteOffset); key_u64(&key,v->InputSlotClass);
        key_u64(&key,v->InstanceDataStepRate);
    }
    key_u64(&key,d->IBStripCutValue); key_u64(&key,d->PrimitiveTopologyType); key_u64(&key,d->NumRenderTargets);
    for (unsigned i=0;i<8;++i) key_u64(&key,d->RTVFormats[i]);
    key_u64(&key,d->DSVFormat); key_u64(&key,d->SampleDesc.Count); key_u64(&key,d->SampleDesc.Quality);
    key_u64(&key,d->NodeMask); key_u64(&key,d->Flags);
    if (d->CachedPSO.CachedBlobSizeInBytes) return false;
    return key_finish(&key,name);
}
#endif
