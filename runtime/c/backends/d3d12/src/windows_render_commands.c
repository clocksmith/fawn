/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

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
static void clear_color(ID3D12GraphicsCommandList *list,doe_dx_native_texture *texture,
    D3D12_CPU_DESCRIPTOR_HANDLE target,const exact_clear_view *exact,const float value[4])
{
    if (exact->heap) {
        UINT values[4]; memcpy(values,value,sizeof(values));
        doe_dx_transition(list,texture->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&exact->heap);
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
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage)
        if (use->storage_sizes[stage]) ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(commands->list,
            draw->pipeline->render->storage_sizes_root[stage],ID3D12Resource_GetGPUVirtualAddress(use->storage_sizes[stage]->resource));
    ID3D12DescriptorHeap *heaps[2];
    UINT count=0;
    if (use->heap) heaps[count++]=use->heap;
    if (use->sampler_heap) heaps[count++]=use->sampler_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(commands->list,count,heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE resources={0},samplers={0};
    if (use->heap) use->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(use->heap,&resources);
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
static void draw_packet(doe_dx_native_commands *commands,const doe_dx_native_render_draw *draw,
    doe_dx_descriptor_use *bindings)
{
    doe_dx_resource_set sets[DOE_DX_RENDER_STAGES];
    resource_sets(draw,sets);
    doe_dx_transition_render_sets(commands,sets,DOE_DX_RENDER_STAGES,draw,false);
    ID3D12GraphicsCommandList *list=commands->list;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,draw->pipeline->root);
    ID3D12GraphicsCommandList_SetPipelineState(list,draw->pipeline->state);
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        uint32_t words=draw->pipeline->render->stages[stage].immediate_bytes/4;
        if (words) ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(list,
            draw->pipeline->render->immediate_root[stage],words,draw->stages[stage].immediate_words,0);
    }
    if (bindings) bind_tables(commands,draw,bindings);
    const float *v=draw->viewport;
    const D3D12_VIEWPORT viewport={v[0],v[1],v[2],v[3],v[4],v[5]};
    const uint32_t *s=draw->scissor;
    const D3D12_RECT scissor={(LONG)s[0],(LONG)s[1],(LONG)(s[0]+s[2]),(LONG)(s[1]+s[3])};
    if (draw->pipeline->render->stencil_test) ID3D12GraphicsCommandList_OMSetStencilRef(list,draw->stencil_reference);
    ID3D12GraphicsCommandList_OMSetBlendFactor(list,draw->blend_constant);
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,draw->pipeline->render->topology);
    doe_dx_bind_vertex_views(commands,draw->pipeline,draw->vertices,draw->vertex_buffer_count);
    if (draw->indices.buffer) {
        D3D12_INDEX_BUFFER_VIEW indices={0};
        indices.BufferLocation=ID3D12Resource_GetGPUVirtualAddress(draw->indices.buffer->resource)+draw->indices.offset;
        indices.SizeInBytes=(UINT)draw->indices.size;
        indices.Format=draw->index_format==DOE_DX_INDEX_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        ID3D12GraphicsCommandList_IASetIndexBuffer(list,&indices);
    }
    if (draw->indirect.buffer) {
        ID3D12CommandSignature *signature=draw->indices.buffer ? commands->context->draw_indexed_signature :
            commands->context->draw_signature;
        ID3D12GraphicsCommandList_ExecuteIndirect(list,signature,1,draw->indirect.buffer->resource,
            draw->indirect.offset,NULL,0);
    } else ID3D12GraphicsCommandList_DrawInstanced(list,draw->vertex_count,draw->instance_count,
        draw->first_vertex,draw->first_instance);
    doe_dx_transition_render_sets(commands,sets,DOE_DX_RENDER_STAGES,draw,true);
}

static doe_dx_status prepare_draw_signature(doe_dx_native_context *context,bool indexed,doe_dx_error *error)
{
    ID3D12CommandSignature **slot=indexed ? &context->draw_indexed_signature : &context->draw_signature;
    if (*slot) return DOE_DX_OK;
    _Static_assert(sizeof(D3D12_DRAW_ARGUMENTS)==16 && sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)==20,
        "Shared guard argument prefix matches SDK draw records");
    D3D12_INDIRECT_ARGUMENT_DESC argument={0};
    argument.Type=indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC desc={0};
    desc.ByteStride=indexed ? (UINT)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) : (UINT)sizeof(D3D12_DRAW_ARGUMENTS);
    desc.NumArgumentDescs=1; desc.pArgumentDescs=&argument;
    ID3D12CommandSignature *signature=NULL;
    HRESULT result=ID3D12Device_CreateCommandSignature(context->device,&desc,NULL,
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
            barrier.Transition.StateBefore=restore ? state : D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter=restore ? D3D12_RESOURCE_STATE_COPY_DEST : state;
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
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_render_draw *draw=&draws[i];
        if (draw->query) continue;
        if (draw->indirect.buffer) {
            status=prepare_draw_signature(commands->context,draw->indices.buffer!=NULL,error);
            if (status!=DOE_DX_OK) goto fail;
        } else if (!draw->vertex_count || !draw->instance_count) continue;
        doe_dx_resource_set sets[DOE_DX_RENDER_STAGES];
        resource_sets(draw,sets);
        size_t descriptors=0;
        for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage)
            descriptors+=draw->stages[stage].buffer_count+draw->stages[stage].texture_count+draw->stages[stage].sampler_count;
        if (!descriptors) continue;
        status=doe_dx_prepare_sets(commands,sets,DOE_DX_RENDER_STAGES,&bindings[i],error);
        if (status!=DOE_DX_OK) goto fail;
        bindings[i]->next=head;
        head=bindings[i];
    }
    /* Every heap/view is prepared. Only now publish descriptor lifetime and
     * record commands, so first failure cannot leave a partially cleared pass. */
    doe_dx_descriptor_use *tail=head;
    while (tail->next) tail=tail->next;
    tail->next=commands->descriptors;
    commands->descriptors=head;
    ID3D12GraphicsCommandList *list=commands->list;
    for (uint32_t i=0;i<color_count;++i) if (colors[i].texture) {
        bool prior=false;
        for (uint32_t j=0;j<i;++j) if (colors[j].texture==colors[i].texture) prior=true;
        if (!prior) doe_dx_transition(list,colors[i].texture->resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
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
        clear_color(list,colors[i].texture,targets[i],&clears[i],colors[i].value);
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_render_draw *draw=&draws[i];
        if (draw->query) {
            if (draw->query_begin) ID3D12GraphicsCommandList_BeginQuery(list,draw->query->heap,D3D12_QUERY_TYPE_OCCLUSION,draw->query_index);
            else ID3D12GraphicsCommandList_EndQuery(list,draw->query->heap,D3D12_QUERY_TYPE_OCCLUSION,draw->query_index);
        } else if (draw->indirect.buffer || (draw->vertex_count && draw->instance_count)) draw_packet(commands,draw,bindings[i]);
    }
    for (uint32_t i=0;i<color_count;++i) {
        const doe_dx_native_render_color *color=&colors[i];
        if (!color->texture) continue;
        if (color->resolve) {
            doe_dx_transition(list,color->texture->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            doe_dx_transition(list,color->resolve->resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RESOLVE_DEST);
            ID3D12GraphicsCommandList_ResolveSubresource(list,color->resolve->resource,
                color->resolve_view.base_mip+color->resolve_view.base_layer*color->resolve->description.MipLevels,
                color->texture->resource,color->view.base_mip+color->view.base_layer*color->texture->description.MipLevels,
                doe_dx_texture_format(color->view.dimension ? color->view.format : color->texture->format));
            doe_dx_transition(list,color->resolve->resource,D3D12_RESOURCE_STATE_RESOLVE_DEST,D3D12_RESOURCE_STATE_COPY_DEST);
            doe_dx_transition(list,color->texture->resource,D3D12_RESOURCE_STATE_RESOLVE_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        if (color->discard) {
            const float zero[4]={0};
            clear_color(list,color->texture,targets[i],&clears[i],zero);
        }
        bool later=false;
        for (uint32_t j=i+1;j<color_count;++j) if (colors[j].texture==color->texture) later=true;
        if (!later) doe_dx_transition(list,color->texture->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
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
