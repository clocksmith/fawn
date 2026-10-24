/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

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
    doe_dx_transition_vertex_sets(commands,sets,DOE_DX_RENDER_STAGES,draw->vertices,draw->vertex_buffer_count,false);
    ID3D12GraphicsCommandList *list=commands->list;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list,draw->pipeline->root);
    ID3D12GraphicsCommandList_SetPipelineState(list,draw->pipeline->state);
    if (bindings) bind_tables(commands,draw,bindings);
    const float *v=draw->viewport;
    const D3D12_VIEWPORT viewport={v[0],v[1],v[2],v[3],v[4],v[5]};
    const uint32_t *s=draw->scissor;
    const D3D12_RECT scissor={(LONG)s[0],(LONG)s[1],(LONG)(s[0]+s[2]),(LONG)(s[1]+s[3])};
    ID3D12GraphicsCommandList_RSSetViewports(list,1,&viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(list,1,&scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    doe_dx_bind_vertex_views(commands,draw->pipeline,draw->vertices,draw->vertex_buffer_count);
    ID3D12GraphicsCommandList_DrawInstanced(list,draw->vertex_count,draw->instance_count,
        draw->first_vertex,draw->first_instance);
    doe_dx_transition_vertex_sets(commands,sets,DOE_DX_RENDER_STAGES,draw->vertices,draw->vertex_buffer_count,true);
}

doe_dx_status doe_dx_native_render(doe_dx_native_commands *commands,doe_dx_native_texture *color,
    bool clear,const float clear_color[4],bool discard,const doe_dx_native_render_draw *draws,
    size_t count,doe_dx_error *error)
{
    doe_dx_descriptor_use **bindings=count ? calloc(count,sizeof(*bindings)) : NULL;
    if (count && !bindings) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.descriptor_indices",error);
    doe_dx_descriptor_use *head=NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE target={0};
    doe_dx_status status=doe_dx_prepare_color_target(commands,color,&head,&target,error);
    if (status!=DOE_DX_OK) { free(bindings); return status; }
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_render_draw *draw=&draws[i];
        if (!draw->vertex_count || !draw->instance_count) continue;
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
    doe_dx_transition(list,color->resource,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list,1,&target,FALSE,NULL);
    if (clear) ID3D12GraphicsCommandList_ClearRenderTargetView(list,target,clear_color,0,NULL);
    for (size_t i=0;i<count;++i)
        if (draws[i].vertex_count && draws[i].instance_count) draw_packet(commands,&draws[i],bindings[i]);
    if (discard) {
        const float zero[4]={0};
        /* Eager real initialization preserves WebGPU's later zero observation
         * without a frontend availability bit or a second lazy-init owner. */
        ID3D12GraphicsCommandList_ClearRenderTargetView(list,target,zero,0,NULL);
    }
    doe_dx_transition(list,color->resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
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
