/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

typedef struct texture_use {
    doe_dx_native_texture *texture;
    doe_dx_binding_access access;
    doe_dx_texture_view_desc view;
} texture_use;

static bool includes(const texture_use *use,uint32_t mip,uint32_t layer,uint32_t plane)
{
    const doe_dx_texture_view_desc *view=&use->view;
    if (doe_dx_format_stencil(use->texture->format) && plane!=(view->aspect==2 ? 1u : 0u)) return false;
    /* Zero spans only occur in the legacy direct SDK-control/private entry. */
    return (!view->mip_count || (mip>=view->base_mip && mip-view->base_mip<view->mip_count)) &&
        (!view->layer_count || (layer>=view->base_layer && layer-view->base_layer<view->layer_count));
}
static void transition_textures(ID3D12GraphicsCommandList *list,const texture_use *uses,size_t count,
    doe_dx_native_texture *attachment_depth,D3D12_RESOURCE_STATES read,bool restore)
{
    for (size_t i=0;i<count;++i) {
        doe_dx_native_texture *texture=uses[i].texture;
        bool repeated=false;
        for (size_t j=0;j<i;++j) if (uses[j].texture==texture) repeated=true;
        if (repeated || texture==attachment_depth) continue;
        uint32_t mips=texture->description.MipLevels ? texture->description.MipLevels : 1u;
        uint32_t layers=texture->description.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u :
            texture->description.DepthOrArraySize ? texture->description.DepthOrArraySize : 1u;
        uint32_t planes=doe_dx_format_stencil(texture->format) ? 2u : 1u;
        bool whole=planes==1;
        for (size_t j=i;j<count;++j) if (uses[j].texture==texture) {
            const doe_dx_texture_view_desc *view=&uses[j].view;
            if (uses[j].access!=uses[i].access || view->base_mip || view->base_layer ||
                (view->mip_count && view->mip_count!=mips) || (view->layer_count && view->layer_count!=layers)) whole=false;
        }
        if (whole) {
            D3D12_RESOURCE_STATES state=uses[i].access==DOE_DX_READ_ONLY ? read : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            doe_dx_transition(list,texture->resource,restore ? state : doe_dx_texture_rest(texture),
                restore ? doe_dx_texture_rest(texture) : state);
            continue;
        }
        for (uint32_t plane=0;plane<planes;++plane) for (uint32_t layer=0;layer<layers;++layer)
            for (uint32_t mip=0;mip<mips;++mip) {
                bool active=false;
                D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
                for (size_t j=i;j<count;++j) if (uses[j].texture==texture && includes(&uses[j],mip,layer,plane)) {
                    active=true;
                    state|=uses[j].access==DOE_DX_READ_ONLY ? read : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }
                if (!active) continue;
                D3D12_RESOURCE_BARRIER barrier={0}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource=texture->resource; barrier.Transition.Subresource=mip+layer*mips+plane*mips*layers;
                barrier.Transition.StateBefore=restore ? state : doe_dx_texture_rest(texture);
                barrier.Transition.StateAfter=restore ? doe_dx_texture_rest(texture) : state;
                ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
            }
    }
}

static D3D12_RESOURCE_STATES shader_read(bool graphics)
{
    return graphics ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE :
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}
size_t doe_dx_set_buffer_states(const doe_dx_resource_set *sets,size_t count,bool graphics,doe_dx_buffer_state *out)
{
    size_t used=0;
    for (size_t stage=0;stage<count;++stage) {
        const doe_dx_resource_set *set=&sets[stage];
        for (size_t i=0;i<set->resources->buffer_count;++i) {
            D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
            switch (doe_dx_buffer_descriptor_kind(&set->buffers[i])) {
            case DOE_DX_DESCRIPTOR_CBV: state=D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; break;
            case DOE_DX_DESCRIPTOR_SRV: state=shader_read(graphics); break;
            case DOE_DX_DESCRIPTOR_UAV: state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; break;
            case DOE_DX_DESCRIPTOR_INVALID: abort();
            }
            used=doe_dx_add_buffer_state(out,used,set->resources->buffers[i].buffer,state);
        }
    }
    return used;
}
static size_t set_textures(const doe_dx_resource_set *sets,size_t count,texture_use *out)
{
    size_t used=0;
    for (size_t stage=0;stage<count;++stage)
        for (size_t i=0;i<sets[stage].resources->texture_count;++i)
            out[used++]=(texture_use){sets[stage].resources->textures[i],sets[stage].textures[i].access,
                sets[stage].resources->texture_views[i]};
    return used;
}
static void transition_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,bool graphics,const doe_dx_native_binding *vertices,size_t vertex_count,
    const doe_dx_native_binding *indices,const doe_dx_native_binding *indirect,doe_dx_native_texture *attachment_depth,bool restore)
{
    doe_dx_buffer_state buffers[DOE_DX_RENDER_DESCRIPTORS+DOE_DX_MAX_VERTEX_BUFFERS+2];
    texture_use textures[DOE_DX_RENDER_DESCRIPTORS];
    size_t buffer_count=doe_dx_set_buffer_states(sets,count,graphics,buffers);
    for (size_t i=0;i<vertex_count;++i) if (vertices[i].buffer)
        buffer_count=doe_dx_add_buffer_state(buffers,buffer_count,vertices[i].buffer,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    if (indices && indices->buffer)
        buffer_count=doe_dx_add_buffer_state(buffers,buffer_count,indices->buffer,D3D12_RESOURCE_STATE_INDEX_BUFFER);
    if (indirect && indirect->buffer)
        buffer_count=doe_dx_add_buffer_state(buffers,buffer_count,indirect->buffer,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    doe_dx_apply_buffer_states(commands->list,buffers,buffer_count,restore);
    transition_textures(commands->list,textures,set_textures(sets,count,textures),attachment_depth,shader_read(graphics),restore);
}
void doe_dx_transition_set_textures(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,bool graphics,bool restore)
{
    texture_use textures[DOE_DX_RENDER_DESCRIPTORS];
    const size_t used=set_textures(sets,count,textures);
    if (used) transition_textures(commands->list,textures,used,NULL,shader_read(graphics),restore);
}
void doe_dx_transition_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,bool graphics,bool restore)
{
    transition_sets(commands,sets,count,graphics,NULL,0,NULL,NULL,NULL,restore);
}
void doe_dx_transition_vertex_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,const doe_dx_native_binding *vertices,size_t vertex_count,bool restore)
{
    transition_sets(commands,sets,count,true,vertices,vertex_count,NULL,NULL,NULL,restore);
}
void doe_dx_transition_render_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,const doe_dx_native_render_draw *draw,bool restore)
{
    transition_sets(commands,sets,count,true,draw->vertices,draw->vertex_buffer_count,&draw->indices,&draw->indirect,draw->attachment_depth,restore);
}
void doe_dx_transition_resources(doe_dx_native_commands *commands,doe_dx_native_pipeline *pipeline,
    const doe_dx_native_resources *resources,bool graphics,bool restore)
{
    const doe_dx_resource_set set={pipeline->bindings,pipeline->textures,resources};
    doe_dx_transition_sets(commands,&set,1,graphics,restore);
}

/* Whole render pass state union (WebGPU makes the pass one usage scope, as
 * Dawn's PrepareResourcesForRenderPass assumes): each resource transitions
 * once at pass start and back once at pass end. A resource written as UAV by
 * more than one draw gets a UAV barrier before each later writer, keeping the
 * ordering the former per-draw round trip gave. */
struct doe_dx_pass_states {
    doe_dx_buffer_state *buffers;
    bool *buffer_written;
    size_t buffer_count;
    texture_use *textures;
    bool *texture_written;
    size_t texture_count;
    bool any_uav;
    doe_dx_native_texture *attachment_depth;
};
static bool recorded_draw(const doe_dx_native_render_draw *draw)
{
    return !draw->query && (draw->indirect.buffer || (draw->vertex_count && draw->instance_count));
}
static void draw_sets(const doe_dx_native_render_draw *draw,doe_dx_resource_set sets[DOE_DX_RENDER_STAGES])
{
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        const doe_dx_binding_plan *plan=&draw->pipeline->render->stages[stage];
        sets[stage]=(doe_dx_resource_set){plan->bindings,plan->textures,&draw->stages[stage]};
    }
}
static bool same_texture_use(const texture_use *a,const texture_use *b)
{
    return a->texture==b->texture && a->access==b->access && !memcmp(&a->view,&b->view,sizeof(a->view));
}
static bool pass_union(doe_dx_pass_states *pass,const doe_dx_native_render_draw *draws,size_t count)
{
    for (size_t i=0;i<count;++i) {
        const doe_dx_native_render_draw *draw=&draws[i];
        if (!recorded_draw(draw)) continue;
        doe_dx_resource_set sets[DOE_DX_RENDER_STAGES];
        draw_sets(draw,sets);
        doe_dx_buffer_state local[DOE_DX_RENDER_DESCRIPTORS];
        const size_t used=doe_dx_set_buffer_states(sets,DOE_DX_RENDER_STAGES,true,local);
        for (size_t j=0;j<used;++j)
            pass->buffer_count=doe_dx_add_buffer_state(pass->buffers,pass->buffer_count,local[j].buffer,local[j].state);
        for (size_t j=0;j<draw->vertex_buffer_count;++j) if (draw->vertices[j].buffer)
            pass->buffer_count=doe_dx_add_buffer_state(pass->buffers,pass->buffer_count,draw->vertices[j].buffer,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        if (draw->indices.buffer)
            pass->buffer_count=doe_dx_add_buffer_state(pass->buffers,pass->buffer_count,draw->indices.buffer,
                D3D12_RESOURCE_STATE_INDEX_BUFFER);
        if (draw->indirect.buffer)
            pass->buffer_count=doe_dx_add_buffer_state(pass->buffers,pass->buffer_count,draw->indirect.buffer,
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        texture_use textures[DOE_DX_RENDER_DESCRIPTORS];
        const size_t texture_count=set_textures(sets,DOE_DX_RENDER_STAGES,textures);
        for (size_t j=0;j<texture_count;++j) {
            bool present=false;
            for (size_t k=0;k<pass->texture_count && !present;++k) {
                if (pass->textures[k].texture==textures[j].texture && pass->textures[k].access!=textures[j].access)
                    return false; /* One state per resource cannot hold both. */
                present=same_texture_use(&pass->textures[k],&textures[j]);
            }
            if (!present) pass->textures[pass->texture_count++]=textures[j];
            if (textures[j].access!=DOE_DX_READ_ONLY) pass->any_uav=true;
        }
    }
    for (size_t i=0;i<pass->buffer_count;++i) {
        const D3D12_RESOURCE_STATES state=pass->buffers[i].state;
        if (!(state&D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) continue;
        if (state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS) return false; /* UAV is exclusive. */
        pass->any_uav=true;
    }
    return true;
}
static void pass_states_free(doe_dx_pass_states *pass)
{
    free(pass->buffers);
    free(pass->buffer_written);
    free(pass->textures);
    free(pass->texture_written);
    free(pass);
}
bool doe_dx_pass_states_begin(doe_dx_native_commands *commands,const doe_dx_native_render_draw *draws,size_t count,
    doe_dx_native_texture *attachment_depth,doe_dx_pass_states **out)
{
    *out=NULL;
    size_t buffers=0,textures=0;
    for (size_t i=0;i<count;++i) {
        if (!recorded_draw(&draws[i])) continue;
        size_t draw_buffers=draws[i].vertex_buffer_count+2u,draw_textures=0;
        for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
            draw_buffers+=draws[i].stages[stage].buffer_count;
            draw_textures+=draws[i].stages[stage].texture_count;
        }
        if (draw_buffers>SIZE_MAX/sizeof(doe_dx_buffer_state)-buffers ||
            draw_textures>SIZE_MAX/sizeof(texture_use)-textures) return false;
        buffers+=draw_buffers;
        textures+=draw_textures;
    }
    doe_dx_pass_states *pass=calloc(1,sizeof(*pass));
    if (!pass) return false;
    pass->attachment_depth=attachment_depth;
    pass->buffers=buffers ? calloc(buffers,sizeof(*pass->buffers)) : NULL;
    pass->buffer_written=buffers ? calloc(buffers,sizeof(*pass->buffer_written)) : NULL;
    pass->textures=textures ? calloc(textures,sizeof(*pass->textures)) : NULL;
    pass->texture_written=textures ? calloc(textures,sizeof(*pass->texture_written)) : NULL;
    if ((buffers && (!pass->buffers || !pass->buffer_written)) ||
        (textures && (!pass->textures || !pass->texture_written)) || !pass_union(pass,draws,count)) {
        pass_states_free(pass);
        return false;
    }
    doe_dx_apply_buffer_states(commands->list,pass->buffers,pass->buffer_count,false);
    transition_textures(commands->list,pass->textures,pass->texture_count,attachment_depth,shader_read(true),false);
    *out=pass;
    return true;
}
void doe_dx_pass_states_draw(doe_dx_native_commands *commands,doe_dx_pass_states *pass,
    const doe_dx_native_render_draw *draw)
{
    if (!pass->any_uav) return;
    doe_dx_resource_set sets[DOE_DX_RENDER_STAGES];
    draw_sets(draw,sets);
    D3D12_RESOURCE_BARRIER barriers[DOE_DX_RENDER_DESCRIPTORS];
    UINT pending=0;
    doe_dx_buffer_state local[DOE_DX_RENDER_DESCRIPTORS];
    const size_t used=doe_dx_set_buffer_states(sets,DOE_DX_RENDER_STAGES,true,local);
    for (size_t j=0;j<used;++j) {
        if (local[j].state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS) continue;
        for (size_t k=0;k<pass->buffer_count;++k) {
            if (pass->buffers[k].buffer!=local[j].buffer) continue;
            if (pass->buffer_written[k]) {
                barriers[pending]=(D3D12_RESOURCE_BARRIER){.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV};
                barriers[pending++].UAV.pResource=local[j].buffer->resource;
            }
            pass->buffer_written[k]=true;
            break;
        }
    }
    texture_use textures[DOE_DX_RENDER_DESCRIPTORS];
    const size_t texture_count=set_textures(sets,DOE_DX_RENDER_STAGES,textures);
    for (size_t j=0;j<texture_count;++j) {
        if (textures[j].access==DOE_DX_READ_ONLY) continue;
        bool repeated=false;
        for (size_t k=0;k<j;++k) if (textures[k].texture==textures[j].texture) repeated=true;
        if (repeated) continue;
        bool written=false;
        for (size_t k=0;k<pass->texture_count;++k) if (pass->textures[k].texture==textures[j].texture) {
            written=written || pass->texture_written[k];
            pass->texture_written[k]=true;
        }
        if (written) {
            barriers[pending]=(D3D12_RESOURCE_BARRIER){.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV};
            barriers[pending++].UAV.pResource=textures[j].texture->resource;
        }
    }
    if (pending) ID3D12GraphicsCommandList_ResourceBarrier(commands->list,pending,barriers);
}
void doe_dx_pass_states_end(doe_dx_native_commands *commands,doe_dx_pass_states *pass)
{
    doe_dx_apply_buffer_states(commands->list,pass->buffers,pass->buffer_count,true);
    transition_textures(commands->list,pass->textures,pass->texture_count,pass->attachment_depth,shader_read(true),true);
    pass_states_free(pass);
}
#endif
