/* SPDX-License-Identifier: MIT */
#include "d3d12_draw_guard.h"
#include "d3d12_texture.h"
#include "../../common/include/doe_draw_guard.h"

#if defined(_WIN32)
/* Upload host-owned guard metadata only. User arguments and indices never map. */
static doe_dx_status metadata(native_command_handle *commands,const uint32_t *words,size_t size,
    doe_dx_buffer **out,doe_dx_error *error)
{
    doe_dx_buffer *upload=NULL,*buffer=NULL;
    doe_dx_status status=doe_dx_buffer_create(commands->value.d3d12.context,size,DOE_DX_UPLOAD,&upload,error);
    if (status==DOE_DX_OK) status=doe_dx_buffer_write(upload,0,words,size,error);
    if (status==DOE_DX_OK) status=doe_dx_buffer_create(commands->value.d3d12.context,size,DOE_DX_DEFAULT,&buffer,error);
    if (status==DOE_DX_OK) status=doe_dx_commands_copy(commands->value.d3d12.commands,upload,0,buffer,0,size,error);
    doe_dx_buffer_release(upload);
    if (status==DOE_DX_OK) *out=buffer; else doe_dx_buffer_release(buffer);
    return status;
}
static doe_dx_status scan_indices(native_command_handle *commands,const doe_resource_render_packet *packet,
    const native_d3d12_graphics *pipeline,const doe_dx_render_draw *draw,doe_dx_buffer *scratch,
    uint64_t vertex_limit,uint32_t max_groups,doe_dx_error *error)
{
    if (!max_groups) return DOE_DX_INVALID_ARGUMENT;
    if (max_groups>65535) max_groups=65535;
    const doe_dx_buffer_binding *index=draw->indices;
    uint64_t element=draw->index_format==DOE_DX_INDEX_UINT16 ? 2u : 4u;
    uint64_t first=packet->indirect ? 0 : packet->first;
    uint64_t limit=index->size/element;
    if (!packet->indirect && (uint64_t)packet->first+packet->count<limit)
        limit=(uint64_t)packet->first+packet->count;
    doe_dx_status status=DOE_DX_OK;
    while (status==DOE_DX_OK && first<limit && vertex_limit!=UINT64_MAX) {
        uint64_t offset=index->offset+first*element;
        uint64_t aligned=offset&~UINT64_C(3);
        uint64_t maximum=(UINT64_C(0xfffffffc)-(offset-aligned))/element;
        uint64_t remaining=limit-first;
        uint32_t count=(uint32_t)(remaining<maximum ? remaining : maximum);
        uint64_t bytes=((offset-aligned)+(uint64_t)count*element+3)&~UINT64_C(3);
        uint32_t groups=count/64+(count%64!=0);
        if (groups>max_groups) groups=max_groups;
        const doe_resource_render_pipeline_desc *description=doe_resource_render_pipeline_description(packet->pipeline);
        bool strip=description->topology==WGPUPrimitiveTopology_TriangleStrip || description->topology==WGPUPrimitiveTopology_LineStrip;
        uint32_t params[9]={(uint32_t)first,(uint32_t)(first>>32),count,(uint32_t)((offset-aligned)/2),
            draw->index_format==DOE_DX_INDEX_UINT16,(uint32_t)vertex_limit,(uint32_t)(vertex_limit>>32),strip,groups*64};
        doe_dx_buffer *parameter=NULL;
        status=metadata(commands,params,sizeof(params),&parameter,error);
        const doe_dx_buffer_binding bindings[3]={{scratch,0,DOE_DRAW_GUARD_RECORD_V2_BYTES},
            {index->buffer,aligned,bytes},{parameter,0,sizeof(params)}};
        if (status==DOE_DX_OK) status=doe_dx_commands_dispatch(commands->value.d3d12.commands,
            pipeline->index_scan,bindings,3,groups,1,1,error);
        doe_dx_buffer_release(parameter);
        first+=count;
    }
    return status;
}
#endif

native_failure native_d3d12_draw_prepare(native_command_handle *commands,const doe_resource_render_packet *packet,
    doe_dx_render_draw *draw,native_d3d12_graphics_snapshot *storage)
{
#if defined(_WIN32)
    if (!packet->indexed && !packet->indirect) return native_failure_status(NATIVE_OK);
    native_pipeline *pipeline=doe_resource_render_pipeline_native(packet->pipeline);
    if (!pipeline || !pipeline->d3d12_graphics || !pipeline->d3d12_graphics->draw_normalize ||
        !pipeline->d3d12_graphics->index_scan) return native_failure_status(NATIVE_INVALID_STATE);
    uint64_t capacities[2];
    doe_dx_status status=doe_dx_render_draw_capacities(draw,capacities);
    if (status!=DOE_DX_OK) return native_failure_d3d12(status,NULL);
    uint64_t count_limit=capacities[0];
    if (packet->indexed) {
        native_buffer *index=doe_resource_buffer_native(packet->index.buffer);
        if (!index || !index->handle.d3d12) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_d3d12_buffer *native=index->handle.d3d12;
        uint64_t offset=packet->index.offset,size=packet->index.size;
        uint32_t element=packet->index_format==WGPUIndexFormat_Uint16 ? 2u : 4u;
        if ((packet->index_format!=WGPUIndexFormat_Uint16 && packet->index_format!=WGPUIndexFormat_Uint32) ||
            offset%element || size%element || offset>native->allocation_size || size>native->allocation_size-offset ||
            offset+size>UINT64_MAX-3 || ((offset+size+3)&~UINT64_C(3))>native->allocation_size)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        storage->indices=(doe_dx_buffer_binding){native->allocation,offset,size};
        draw->indices=&storage->indices;
        draw->index_format=element==2 ? DOE_DX_INDEX_UINT16 : DOE_DX_INDEX_UINT32;
        count_limit=size/element;
    }
    bool first_allowed=!packet->indirect ||
        (pipeline->device->features & NATIVE_FEATURE(WGPUFeatureName_IndirectFirstInstance));
    uint32_t words[10]={packet->count,packet->instances,packet->first,
        packet->indexed ? (uint32_t)packet->base_vertex : packet->first_instance,0,0,0,
        (packet->indexed ? 1u : 0u)|(first_allowed ? 2u : 0u),
        (uint32_t)(count_limit>>32),(uint32_t)(capacities[1]>>32)};
    if (packet->indexed) { words[4]=packet->first_instance; words[5]=(uint32_t)count_limit; words[6]=(uint32_t)capacities[1]; }
    else { words[4]=(uint32_t)count_limit; words[5]=(uint32_t)capacities[1]; }
    doe_dx_error error={0};
    doe_dx_buffer *scratch=NULL;
    status=metadata(commands,words,sizeof(words),&scratch,&error);
    if (status==DOE_DX_OK && packet->indirect) {
        native_buffer *arguments=doe_resource_buffer_native(packet->indirect);
        if (!arguments || !arguments->handle.d3d12) status=DOE_DX_INVALID_ARGUMENT;
        else status=doe_dx_commands_copy(commands->value.d3d12.commands,arguments->handle.d3d12->allocation,
            packet->indirect_offset,scratch,0,packet->indexed ? 20u : 16u,&error);
    }
    const doe_dx_buffer_binding binding={scratch,0,sizeof(words)};
    if (status==DOE_DX_OK) status=doe_dx_commands_dispatch(commands->value.d3d12.commands,
        pipeline->d3d12_graphics->draw_normalize,&binding,1,1,1,1,&error);
    if (status==DOE_DX_OK && packet->indexed) status=scan_indices(commands,packet,pipeline->d3d12_graphics,
        draw,scratch,capacities[0],pipeline->device->backend.limits.maxComputeWorkgroupsPerDimension,&error);
    if (status==DOE_DX_OK && (pipeline->d3d12_graphics->stages[0].entry_flags & DOE_COMPILER_HLSL_ENTRY_DRAW_OFFSETS)) {
        /* GPU-only projection of already normalized arguments. The signature
         * consumes uint2 root constants followed by the unchanged SDK record. */
        doe_dx_buffer *projected=NULL;
        uint64_t bytes=packet->indexed ? 20u : 16u;
        status=doe_dx_buffer_create(commands->value.d3d12.context,bytes+8,DOE_DX_DEFAULT,&projected,&error);
        /* Whole initialization is real GPU data, not a synthetic initialized
         * mark: the shared copy owner intentionally does not union partials. */
        if (status==DOE_DX_OK) status=doe_dx_commands_copy(commands->value.d3d12.commands,
            scratch,0,projected,0,bytes+8,&error);
        if (status==DOE_DX_OK) status=doe_dx_commands_copy(commands->value.d3d12.commands,
            scratch,packet->indexed ? 12u : 8u,projected,0,8,&error);
        if (status==DOE_DX_OK) status=doe_dx_commands_copy(commands->value.d3d12.commands,
            scratch,0,projected,8,bytes,&error);
        if (status==DOE_DX_OK) {
            doe_dx_buffer_release(scratch);
            scratch=projected;
            storage->arguments=(doe_dx_buffer_binding){scratch,0,bytes+8};
        } else doe_dx_buffer_release(projected);
    } else if (status==DOE_DX_OK) storage->arguments=binding;
    if (status==DOE_DX_OK) draw->indirect=&storage->arguments;
    /* Normalizer acquired scratch; the frontend retains this temporary owner
     * through render preparation, then releases it even when preparation fails. */
    if (status!=DOE_DX_OK) doe_dx_buffer_release(scratch);
    return native_failure_d3d12(status,&error);
#else
    (void)commands; (void)packet; (void)draw; (void)storage;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
