/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

typedef struct buffer_use {
    doe_dx_native_buffer *buffer;
    D3D12_RESOURCE_STATES state;
} buffer_use;
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
            doe_dx_transition(list,texture->resource,restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
                restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
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
                barrier.Transition.StateBefore=restore ? state : D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter=restore ? D3D12_RESOURCE_STATE_COPY_DEST : state;
                ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
            }
    }
}

static void transition_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,bool graphics,const doe_dx_native_binding *vertices,size_t vertex_count,
    const doe_dx_native_binding *indices,const doe_dx_native_binding *indirect,doe_dx_native_texture *attachment_depth,bool restore)
{
    buffer_use buffers[DOE_DX_RENDER_DESCRIPTORS+DOE_DX_MAX_VERTEX_BUFFERS+2];
    texture_use textures[DOE_DX_RENDER_DESCRIPTORS];
    size_t buffer_count=0,texture_count=0;
    D3D12_RESOURCE_STATES read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (graphics) read|=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    for (size_t stage=0;stage<count;++stage) {
        const doe_dx_resource_set *set=&sets[stage];
        for (size_t i=0;i<set->resources->buffer_count;++i) {
            D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
            switch (doe_dx_buffer_descriptor_kind(&set->buffers[i])) {
            case DOE_DX_DESCRIPTOR_CBV: state=D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; break;
            case DOE_DX_DESCRIPTOR_SRV: state=read; break;
            case DOE_DX_DESCRIPTOR_UAV: state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; break;
            case DOE_DX_DESCRIPTOR_INVALID: abort();
            }
            buffers[buffer_count++]=(buffer_use){set->resources->buffers[i].buffer,state};
        }
        for (size_t i=0;i<set->resources->texture_count;++i)
            textures[texture_count++]=(texture_use){set->resources->textures[i],set->textures[i].access,set->resources->texture_views[i]};
    }
    for (size_t i=0;i<vertex_count;++i) if (vertices[i].buffer)
        buffers[buffer_count++]=(buffer_use){vertices[i].buffer,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
    if (indices && indices->buffer) buffers[buffer_count++]=(buffer_use){indices->buffer,D3D12_RESOURCE_STATE_INDEX_BUFFER};
    if (indirect && indirect->buffer) buffers[buffer_count++]=(buffer_use){indirect->buffer,D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT};
    for (size_t i=0;i<buffer_count;++i) {
        bool repeated=false;
        for (size_t j=0;j<i;++j) if (buffers[j].buffer==buffers[i].buffer) repeated=true;
        if (repeated) continue;
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
        for (size_t j=i;j<buffer_count;++j) {
            if (buffers[j].buffer!=buffers[i].buffer) continue;
            state|=buffers[j].state;
        }
        doe_dx_transition(commands->list,buffers[i].buffer->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
    }
    transition_textures(commands->list,textures,texture_count,attachment_depth,read,restore);
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
#endif
