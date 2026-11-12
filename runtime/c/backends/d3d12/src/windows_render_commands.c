/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

_Static_assert(DOE_DX_VIEWPORT_BOUNDS_MIN==D3D12_VIEWPORT_BOUNDS_MIN && DOE_DX_VIEWPORT_BOUNDS_MAX==D3D12_VIEWPORT_BOUNDS_MAX,
    "portable viewport admission must use the SDK viewport bounds");

typedef struct exact_clear_view {
    ID3D12DescriptorHeap *heap;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
} exact_clear_view;
static doe_dx_status prepare_exact_clear(doe_dx_native_commands *commands,doe_dx_native_texture *texture,
    const doe_dx_texture_view_desc *span,uint32_t slice,
    doe_dx_descriptor_use **head,exact_clear_view *out,doe_dx_error *error)
{
    doe_dx_descriptor_use *gpu=calloc(1,sizeof(*gpu)),*cpu=calloc(1,sizeof(*cpu));
    if (!gpu || !cpu) { free(gpu); free(cpu); return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.integer_clear.allocate",error); }
    D3D12_DESCRIPTOR_HEAP_DESC heap={0};
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors=1;
    heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12Device *device=commands->context->device;
    HRESULT result=ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void **)&gpu->heap);
    if (SUCCEEDED(result)) {
        heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        result=ID3D12Device_CreateDescriptorHeap(device,&heap,&IID_ID3D12DescriptorHeap,(void **)&cpu->heap);
    }
    if (FAILED(result)) {
        doe_dx_release_descriptors(gpu); doe_dx_release_descriptors(cpu);
        return doe_dx_hresult(result,"CreateDescriptorHeap.integer_clear",error);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE visible={0};
    gpu->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(gpu->heap,&visible);
    gpu->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(gpu->heap,&out->gpu);
    cpu->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(cpu->heap,&out->cpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC view={0};
    doe_dx_texture_uav(texture,span,&view);
    if (span->dimension==6) { view.Texture3D.FirstWSlice=slice; view.Texture3D.WSize=1; }
    ID3D12Device_CreateUnorderedAccessView(device,texture->resource,NULL,&view,visible);
    ID3D12Device_CreateUnorderedAccessView(device,texture->resource,NULL,&view,out->cpu);
    out->heap=gpu->heap;
    cpu->next=*head; gpu->next=cpu; *head=gpu;
    return DOE_DX_OK;
}
static void clear_color(doe_dx_native_commands *commands,doe_dx_native_texture *texture,
    D3D12_CPU_DESCRIPTOR_HANDLE target,const exact_clear_view *exact,const float value[4])
{
    ID3D12GraphicsCommandList *list=commands->list;
    if (exact->heap) {
        UINT values[4]; memcpy(values,value,sizeof(values));
        doe_dx_transition(list,texture->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        doe_dx_bind_heaps(commands,&exact->heap,1);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(list,exact->gpu,exact->cpu,texture->resource,values,0,NULL);
        doe_dx_transition(list,texture->resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
        return;
    }
    doe_dx_format_info info;
    bool known=doe_dx_format_properties(texture->format,&info);
    float converted[4];
    if (known && (info.kind==2 || info.kind==3)) {
        uint32_t unsigned_values[4]; int32_t signed_values[4];
        memcpy(unsigned_values,value,sizeof(unsigned_values)); memcpy(signed_values,value,sizeof(signed_values));
        /* Every in-range <=16-bit component is exactly representable in f32;
         * out-of-range inputs are clamped by the native format conversion. */
        for (size_t i=0;i<4;++i) converted[i]=info.kind==2 ? (float)unsigned_values[i] : (float)signed_values[i];
        value=converted;
    }
    ID3D12GraphicsCommandList_ClearRenderTargetView(list,target,value,0,NULL);
}

static void resource_sets(const doe_dx_native_render_draw *draw,doe_dx_resource_set sets[DOE_DX_RENDER_STAGES])
{
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        const doe_dx_binding_plan *plan=&draw->pipeline->render->stages[stage];
        sets[stage]=(doe_dx_resource_set){plan->bindings,plan->textures,&draw->stages[stage]};
    }
}
static void bind_tables(doe_dx_native_commands *commands,const doe_dx_native_render_draw *draw,
    doe_dx_descriptor_use *use)
{
    ID3D12DescriptorHeap *heaps[2];
    UINT count=0;
    if (use->heap) heaps[count++]=use->heap;
    if (use->sampler_heap) heaps[count++]=use->sampler_heap;
    doe_dx_bind_heaps(commands,heaps,count);
    /* Same range, root signature and heaps: the tables are still bound. */
    if (commands->bound.graphics_use==use) return;
    commands->bound.graphics_use=use;
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage)
        if (use->storage_sizes[stage]) ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(commands->list,
            draw->pipeline->render->storage_sizes_root[stage],use->storage_sizes[stage]);
    D3D12_GPU_DESCRIPTOR_HANDLE resources={0},samplers={0};
    if (use->heap) doe_dx_descriptor_gpu_start(use,&resources);
    if (use->sampler_heap) use->sampler_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(use->sampler_heap,&samplers);
    UINT resource_stride=ID3D12Device_GetDescriptorHandleIncrementSize(commands->context->device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT sampler_stride=ID3D12Device_GetDescriptorHandleIncrementSize(commands->context->device,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    const doe_dx_windows_render_layout *layout=draw->pipeline->render;
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        if (layout->resource_table[stage]!=UINT32_MAX) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle={resources.ptr+(UINT64)layout->resource_offset[stage]*resource_stride};
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commands->list,layout->resource_table[stage],handle);
        }
        if (layout->sampler_table[stage]!=UINT32_MAX) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle={samplers.ptr+(UINT64)layout->sampler_offset[stage]*sampler_stride};
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(commands->list,layout->sampler_table[stage],handle);
        }
    }
}
static bool same_index_view(const doe_dx_native_render_draw *a,const doe_dx_native_render_draw *b)
{
    return a->indices.buffer==b->indices.buffer && a->indices.offset==b->indices.offset &&
        a->indices.size==b->indices.size && a->index_format==b->index_format;
}
/* previous is the last draw recorded in this pass (NULL first); fixed-function
 * and input-assembler state it already set is not set again. Root constants,
 * tables and pipeline go through the bound-state owner. */
static void draw_packet(doe_dx_native_commands *commands,const doe_dx_native_render_draw *draw,
    doe_dx_descriptor_use *bindings,const doe_dx_native_render_draw *previous,doe_dx_pass_states *pass)
{
    doe_dx_resource_set sets[DOE_DX_RENDER_STAGES];
    resource_sets(draw,sets);
    if (pass) doe_dx_pass_states_draw(commands,pass,draw);
    else doe_dx_transition_render_sets(commands,sets,DOE_DX_RENDER_STAGES,draw,false);
    ID3D12GraphicsCommandList *list=commands->list;
    const doe_dx_windows_render_layout *layout=draw->pipeline->render;
    const bool root_changed=doe_dx_bind_graphics_root(commands,draw->pipeline->root);
    doe_dx_bind_state(commands,draw->pipeline->state);
    /* Root constants persist until the root signature changes; an indirect draw
     * with draw offsets rewrites its constant through the command signature. */
    const bool same_root=previous && !root_changed && previous->pipeline->root==draw->pipeline->root;
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        uint32_t words=layout->stages[stage].immediate_bytes/4;
        if (!words) continue;
        if (same_root && previous->pipeline==draw->pipeline &&
            !memcmp(previous->stages[stage].immediate_words,draw->stages[stage].immediate_words,words*sizeof(uint32_t)))
            continue;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,
            layout->immediate_root[stage],words,draw->stages[stage].immediate_words,0);
    }
    if (layout->draw_offsets_root!=UINT32_MAX && !draw->indirect.buffer &&
        !(same_root && !previous->indirect.buffer && previous->pipeline->render->draw_offsets_root==layout->draw_offsets_root &&
          previous->first_vertex==draw->first_vertex && previous->first_instance==draw->first_instance)) {
        const uint32_t offsets[2]={draw->first_vertex,draw->first_instance};
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,layout->draw_offsets_root,2,offsets,0);
    }
    if (bindings) bind_tables(commands,draw,bindings);
    if (layout->stencil_test && !(previous && previous->pipeline->render->stencil_test &&
        previous->stencil_reference==draw->stencil_reference))
        ID3D12GraphicsCommandList_OMSetStencilRef(list,draw->stencil_reference);
    if (!previous || memcmp(previous->blend_constant,draw->blend_constant,sizeof(draw->blend_constant)))
        ID3D12GraphicsCommandList_OMSetBlendFactor(list,draw->blend_constant);
    if (!previous || memcmp(previous->viewport,draw->viewport,sizeof(draw->viewport))) {
        const float *v=draw->viewport;
        const D3D12_VIEWPORT viewport={v[0],v[1],v[2],v[3],v[4],v[5]};
        ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);
    }
    if (!previous || memcmp(previous->scissor,draw->scissor,sizeof(draw->scissor))) {
        const uint32_t *s=draw->scissor;
        const D3D12_RECT scissor={(LONG)s[0],(LONG)s[1],(LONG)(s[0]+s[2]),(LONG)(s[1]+s[3])};
        ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
    }
    if (!previous || previous->pipeline->render->topology!=layout->topology)
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,layout->topology);
    if (!previous || previous->pipeline!=draw->pipeline || previous->vertex_buffer_count!=draw->vertex_buffer_count ||
        memcmp(previous->vertices,draw->vertices,draw->vertex_buffer_count*sizeof(draw->vertices[0])))
        doe_dx_bind_vertex_views(commands,draw->pipeline,draw->vertices,draw->vertex_buffer_count);
    if (draw->indices.buffer && !(previous && same_index_view(previous,draw))) {
        D3D12_INDEX_BUFFER_VIEW indices={0};
        indices.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(draw->indices.buffer->resource)+draw->indices.offset;
        indices.SizeInBytes=(UINT)draw->indices.size;
        indices.Format=draw->index_format==DOE_DX_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D12GraphicsCommandList_IASetIndexBuffer(list,&indices);
    }
    if (draw->indirect.buffer) {
        ID3D12CommandSignature *signature=draw->indices.buffer ? commands->context->draw_indexed_signature :
            commands->context->draw_signature;
        if (layout->draw_offsets_root!=UINT32_MAX)
            signature=draw->indices.buffer ? layout->draw_indexed_signature : layout->draw_signature;
        ID3D12GraphicsCommandList_ExecuteIndirect(list,signature,1,draw->indirect.buffer->resource,
            draw->indirect.offset,NULL,0);
    } else ID3D12GraphicsCommandList_DrawInstanced(list,draw->vertex_count,draw->instance_count,
        draw->first_vertex,draw->first_instance);
    if (!pass) doe_dx_transition_render_sets(commands,sets,DOE_DX_RENDER_STAGES,draw,true);
}

static doe_dx_status prepare_draw_signature(doe_dx_native_context *context,doe_dx_native_pipeline *pipeline,bool indexed,doe_dx_error *error)
{
    ID3D12CommandSignature **slot=indexed ? &context->draw_indexed_signature : &context->draw_signature;
    bool offsets=pipeline->render->draw_offsets_root!=UINT32_MAX;
    if (offsets) slot=indexed ? &pipeline->render->draw_indexed_signature : &pipeline->render->draw_signature;
    if (*slot) return DOE_DX_OK;
    _Static_assert(sizeof(D3D12_DRAW_ARGUMENTS)==16 && sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)==20,
        "Shared guard argument prefix matches SDK draw records");
    D3D12_INDIRECT_ARGUMENT_DESC arguments[2]={0};
    if (offsets) {
        arguments[0].Type=D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        arguments[0].Constant.RootParameterIndex=pipeline->render->draw_offsets_root;
        arguments[0].Constant.Num32BitValuesToSet=2;
    }
    arguments[offsets ? 1 : 0].Type=indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC desc={0};
    desc.ByteStride=indexed ? (UINT)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) : (UINT)sizeof(D3D12_DRAW_ARGUMENTS);
    desc.ByteStride+=offsets ? 8u : 0u;
    desc.NumArgumentDescs=offsets ? 2u : 1u; desc.pArgumentDescs=arguments;
    ID3D12CommandSignature *signature=NULL;
    HRESULT result=ID3D12Device_CreateCommandSignature(context->device,&desc,offsets ? pipeline->root : NULL,
        &IID_ID3D12CommandSignature,(void **)&signature);
    if (FAILED(result)) return doe_dx_hresult(result,"CreateCommandSignature.draw",error);
    *slot=signature;
    return DOE_DX_OK;
}

static void depth_transitions(ID3D12GraphicsCommandList *list,const doe_dx_native_render_depth *depth,
    uint32_t sampled_planes,bool restore)
{
    const doe_dx_native_texture *texture=depth->texture;
    uint32_t layers=texture->description.DepthOrArraySize;
    uint32_t slices=layers*texture->description.MipLevels;
    uint32_t planes=doe_dx_format_stencil(texture->format) ? 2u : 1u;
    for (uint32_t plane=0;plane<planes;++plane) {
        bool read_only=plane ? depth->stencil_read_only : depth->depth_read_only;
        D3D12_RESOURCE_STATES state=read_only ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if (sampled_planes & (1u<<plane)) state|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for (uint32_t slice=0;slice<slices;++slice) {
            D3D12_RESOURCE_BARRIER barrier={0}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=texture->resource; barrier.Transition.Subresource=plane*slices+slice;
            barrier.Transition.StateBefore=restore ? state : doe_dx_texture_rest(texture);
            barrier.Transition.StateAfter=restore ? doe_dx_texture_rest(texture) : state;
            ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
        }
    }
}
doe_dx_status doe_dx_native_render(doe_dx_native_commands *commands,doe_dx_native_texture *color,
    bool clear,const float clear_color[4],bool discard,const doe_dx_native_render_depth *depth,
    const doe_dx_native_render_draw *draws,size_t count,doe_dx_error *error) {
    return doe_dx_native_render_resolve(commands,color,NULL,clear,clear_color,discard,depth,draws,count,error);
}
doe_dx_status doe_dx_native_render_resolve(doe_dx_native_commands *commands,doe_dx_native_texture *color,
    doe_dx_native_texture *resolve,bool clear,const float clear_color[4],bool discard,const doe_dx_native_render_depth *depth,
    const doe_dx_native_render_draw *draws,
    size_t count,doe_dx_error *error)
{
    doe_dx_native_render_color target={.texture=color,.resolve=resolve,.clear=clear,.discard=discard};
    memcpy(target.value,clear_color,sizeof(target.value));
    return doe_dx_native_render_targets(commands,&target,color ? 1u : 0u,depth,draws,count,error);
}
doe_dx_status doe_dx_native_render_targets(doe_dx_native_commands *commands,
    const doe_dx_native_render_color *colors,uint32_t color_count,const doe_dx_native_render_depth *depth,
    const doe_dx_native_render_draw *draws,size_t count,doe_dx_error *error)
{
    doe_dx_descriptor_use **bindings=count ? calloc(count,sizeof(*bindings)) : NULL;
    if (count && !bindings) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.descriptor_indices",error);
    doe_dx_descriptor_use *head=NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE targets[DOE_DX_MAX_COLOR_TARGETS]={0};
    exact_clear_view clears[DOE_DX_MAX_COLOR_TARGETS]={0};
    doe_dx_status status=DOE_DX_OK;
    for (uint32_t i=0;i<color_count;++i) {
        doe_dx_descriptor_use *rtv=NULL;
        status=doe_dx_prepare_color_view(commands,colors[i].texture,colors[i].view.dimension ? &colors[i].view : NULL,colors[i].depth_slice,&rtv,&targets[i],error);
        if (status!=DOE_DX_OK) goto fail;
        rtv->next=head; head=rtv;
        if (colors[i].resolve && !colors[i].texture->resolve_supported) {
            status=doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.native_resolve_format",error); goto fail;
        }
        if (colors[i].texture && (colors[i].clear || colors[i].discard) && doe_dx_format_exact_clear(colors[i].texture->format)) {
            status=prepare_exact_clear(commands,colors[i].texture,&colors[i].view,colors[i].depth_slice,&head,&clears[i],error);
            if (status!=DOE_DX_OK) goto fail;
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE depth_handle={0};
    if (depth) {
        doe_dx_descriptor_use *dsv=NULL;
        status=doe_dx_prepare_depth_view(commands,depth->texture,depth->view.dimension ? &depth->view : NULL,
            depth->depth_read_only,depth->stencil_read_only,&dsv,&depth_handle,error);
        if (status!=DOE_DX_OK) goto fail;
        dsv->next=head; head=dsv;
    }
    const doe_dx_native_render_draw *described=NULL;
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_render_draw *draw=&draws[i];
        if (draw->query) continue;
        if (draw->indirect.buffer) {
            status=prepare_draw_signature(commands->context,draw->pipeline,draw->indices.buffer!=NULL,error);
            if (status!=DOE_DX_OK) goto fail;
        } else if (!draw->vertex_count || !draw->instance_count) continue;
        doe_dx_resource_set sets[DOE_DX_RENDER_STAGES];
        resource_sets(draw,sets);
        size_t descriptors=0;
        for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage)
            descriptors+=draw->stages[stage].buffer_count+draw->stages[stage].texture_count+draw->stages[stage].sampler_count;
        if (!descriptors) continue;
        /* Same pipeline and bindings as the previous described draw: borrow its
         * range, already written and owned by this pass's descriptor list. */
        if (described && described->pipeline==draw->pipeline &&
            doe_dx_same_resources(&described->stages[0],&draw->stages[0]) &&
            doe_dx_same_resources(&described->stages[1],&draw->stages[1])) {
            bindings[i]=bindings[described-draws];
            continue;
        }
        status=doe_dx_prepare_sets(commands,sets,DOE_DX_RENDER_STAGES,&bindings[i],error);
        if (status!=DOE_DX_OK) goto fail;
        bindings[i]->next=head;
        head=bindings[i];
        described=draw;
    }
    /* Every heap/view is prepared. Only now publish descriptor lifetime and
     * record commands, so first failure cannot leave a partially cleared pass. */
    doe_dx_descriptor_use *tail=head;
    while (tail->next) tail=tail->next;
    tail->next=commands->descriptors;
    commands->descriptors=head;
    doe_dx_settle_buffers(commands);
    ID3D12GraphicsCommandList *list=commands->list;
    for (uint32_t i=0;i<color_count;++i) if (colors[i].texture) {
        bool prior=false;
        for (uint32_t j=0;j<i;++j) if (colors[j].texture==colors[i].texture) prior=true;
        if (!prior) doe_dx_transition(list,colors[i].texture->resource,doe_dx_texture_rest(colors[i].texture),D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    uint32_t sampled_planes=0;
    if (depth) {
        for (size_t i=0;i<count;++i) if (!draws[i].query)
            for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage)
                for (size_t j=0;j<draws[i].stages[stage].texture_count;++j)
                    if (draws[i].stages[stage].textures[j]==depth->texture)
                        sampled_planes|=draws[i].stages[stage].texture_views[j].aspect==2 ? 2u : 1u;
        depth_transitions(list,depth,sampled_planes,false);
    }
    ID3D12GraphicsCommandList_OMSetRenderTargets(list,color_count,color_count ? targets : NULL,FALSE,depth ? &depth_handle : NULL);
    if (depth && (depth->clear || depth->clear_stencil)) {
        D3D12_CLEAR_FLAGS flags=(D3D12_CLEAR_FLAGS)((depth->clear ? D3D12_CLEAR_FLAG_DEPTH : 0) |
            (depth->clear_stencil ? D3D12_CLEAR_FLAG_STENCIL : 0));
        ID3D12GraphicsCommandList_ClearDepthStencilView(list,depth_handle,flags,depth->value,(UINT8)depth->stencil,0,NULL);
    }
    for (uint32_t i=0;i<color_count;++i) if (colors[i].texture && colors[i].clear)
        clear_color(commands,colors[i].texture,targets[i],&clears[i],colors[i].value);
    /* Without whole-pass states (conflicting uses or no memory) each draw
     * transitions and restores its own resources, as before. */
    doe_dx_pass_states *pass=NULL;
    (void)doe_dx_pass_states_begin(commands,draws,count,depth ? depth->texture : NULL,&pass);
    const doe_dx_native_render_draw *previous=NULL;
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_render_draw *draw=&draws[i];
        if (draw->query) {
            if (draw->query_begin) ID3D12GraphicsCommandList_BeginQuery(list,draw->query->heap,D3D12_QUERY_TYPE_OCCLUSION,draw->query_index);
            else ID3D12GraphicsCommandList_EndQuery(list,draw->query->heap,D3D12_QUERY_TYPE_OCCLUSION,draw->query_index);
        } else if (draw->indirect.buffer || (draw->vertex_count && draw->instance_count)) {
            draw_packet(commands,draw,bindings[i],previous,pass);
            previous=draw;
        }
    }
    if (pass) doe_dx_pass_states_end(commands,pass);
    for (uint32_t i=0;i<color_count;++i) {
        const doe_dx_native_render_color *color=&colors[i];
        if (!color->texture) continue;
        if (color->resolve) {
            doe_dx_transition(list,color->texture->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            doe_dx_transition(list,color->resolve->resource,doe_dx_texture_rest(color->resolve),D3D12_RESOURCE_STATE_RESOLVE_DEST);
            ID3D12GraphicsCommandList_ResolveSubresource(list,color->resolve->resource,
                color->resolve_view.base_mip+color->resolve_view.base_layer*color->resolve->description.MipLevels,
                color->texture->resource,color->view.base_mip+color->view.base_layer*color->texture->description.MipLevels,
                doe_dx_texture_format(color->view.dimension ? color->view.format : color->texture->format));
            doe_dx_transition(list,color->resolve->resource,D3D12_RESOURCE_STATE_RESOLVE_DEST,doe_dx_texture_rest(color->resolve));
            doe_dx_transition(list,color->texture->resource,D3D12_RESOURCE_STATE_RESOLVE_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        if (color->discard) {
            const float zero[4]={0};
            clear_color(commands,color->texture,targets[i],&clears[i],zero);
        }
        bool later=false;
        for (uint32_t j=i+1;j<color_count;++j) if (colors[j].texture==color->texture) later=true;
        if (!later) doe_dx_transition(list,color->texture->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,doe_dx_texture_rest(color->texture));
    }
    if (depth) {
        if (depth->discard || depth->discard_stencil) {
            D3D12_CLEAR_FLAGS flags=(D3D12_CLEAR_FLAGS)((depth->discard ? D3D12_CLEAR_FLAG_DEPTH : 0) |
                (depth->discard_stencil ? D3D12_CLEAR_FLAG_STENCIL : 0));
            ID3D12GraphicsCommandList_ClearDepthStencilView(list,depth_handle,flags,0,0,0,NULL);
        }
        depth_transitions(list,depth,sampled_planes,true);
    }
    free(bindings);
    return doe_dx_fail(DOE_DX_OK,0,"render.native.pass",error);
fail:
    while (head) {
        doe_dx_descriptor_use *next=head->next;
        doe_dx_release_descriptors(head);
        head=next;
    }
    free(bindings);
    return status;
}
#endif
