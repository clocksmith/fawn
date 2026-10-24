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
} texture_use;

static void transition_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,bool graphics,const doe_dx_native_binding *vertices,size_t vertex_count,bool restore)
{
    buffer_use buffers[DOE_DX_RENDER_DESCRIPTORS+DOE_DX_MAX_VERTEX_BUFFERS];
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
            textures[texture_count++]=(texture_use){set->resources->textures[i],set->textures[i].access};
    }
    for (size_t i=0;i<vertex_count;++i) if (vertices[i].buffer)
        buffers[buffer_count++]=(buffer_use){vertices[i].buffer,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};
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
    for (size_t i=0;i<texture_count;++i) {
        bool repeated=false;
        for (size_t j=0;j<i;++j) if (textures[j].texture==textures[i].texture) repeated=true;
        if (repeated) continue;
        D3D12_RESOURCE_STATES state=textures[i].access==DOE_DX_READ_ONLY ? read :
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        doe_dx_transition(commands->list,textures[i].texture->resource,
            restore ? state : D3D12_RESOURCE_STATE_COPY_DEST,
            restore ? D3D12_RESOURCE_STATE_COPY_DEST : state);
    }
}
void doe_dx_transition_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,bool graphics,bool restore)
{
    transition_sets(commands,sets,count,graphics,NULL,0,restore);
}
void doe_dx_transition_vertex_sets(doe_dx_native_commands *commands,const doe_dx_resource_set *sets,
    size_t count,const doe_dx_native_binding *vertices,size_t vertex_count,bool restore)
{
    transition_sets(commands,sets,count,true,vertices,vertex_count,restore);
}
void doe_dx_transition_resources(doe_dx_native_commands *commands,doe_dx_native_pipeline *pipeline,
    const doe_dx_native_resources *resources,bool graphics,bool restore)
{
    const doe_dx_resource_set set={pipeline->bindings,pipeline->textures,resources};
    doe_dx_transition_sets(commands,&set,1,graphics,restore);
}
#endif
