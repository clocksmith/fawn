/* SPDX-License-Identifier: MIT */
#include "d3d12_graphics_internal.h"
#include <limits.h>

bool native_d3d12_interstage_mask(const native_stage *stage,uint32_t *out)
{
    if (!stage || !out || !stage->entry || !stage->view.entries ||
        (stage->view.interface_count && !stage->view.interfaces)) return false;
    const doe_shader_view *view=&stage->view;
    size_t selected=SIZE_MAX;
    for (size_t i=0;i<view->counts.entries;++i) if (&view->entries[i]==stage->entry) selected=i;
    if (selected==SIZE_MAX || stage->entry->stage!=DOE_COMPILER_STAGE_FRAGMENT) return false;
    uint32_t mask=0;
    for (size_t i=0;i<view->interface_count;++i) {
        const DoeCompilerStageInterfaceV7 *item=&view->interfaces[i];
        if (item->entry_index!=selected || item->direction!=DOE_COMPILER_INTERFACE_INPUT || item->builtin) continue;
        if (item->location>=sizeof(mask)*CHAR_BIT) return false;
        uint32_t bit=UINT32_C(1)<<item->location;
        if (mask&bit) return false;
        mask|=bit;
    }
    *out=mask;
    return true;
}

bool native_d3d12_graphics_maps_count(const native_pipeline *pipeline,const native_stage *stages,size_t count,
    native_d3d12_layout out[2],native_d3d12_graphics *maps)
{
    if (!pipeline || !stages || !out || !maps || (count!=1 && count!=2) || pipeline->binding_count>NATIVE_MAX_PIPELINE_BINDINGS) return false;
    native_d3d12_graphics result={.stage_count=count};
    native_d3d12_layout layouts[2]={0};
    for (size_t stage=0;stage<count;++stage) {
        const doe_shader_view *view=&stages[stage].view;
        const DoeCompilerEntryV5 *entry=stages[stage].entry;
        if (!entry || (view->counts.entry_references && !view->entry_references) ||
            (view->counts.resources && !view->resources) ||
            entry->first_resource_reference>view->counts.entry_references ||
            entry->resource_reference_count>view->counts.entry_references-entry->first_resource_reference ||
            entry->resource_reference_count>NATIVE_MAX_PIPELINE_BINDINGS) return false;
        native_pipeline selected={.device=pipeline->device};
        size_t canonical[NATIVE_MAX_PIPELINE_BINDINGS];
        for (size_t i=0;i<entry->resource_reference_count;++i) {
            uint32_t index=view->entry_references[entry->first_resource_reference+i];
            if (index>=view->counts.resources) return false;
            const DoeCompilerResourceV4 *resource=&view->resources[index];
            size_t found=SIZE_MAX;
            for (size_t j=0;j<pipeline->binding_count;++j)
                if (pipeline->bindings[j].group==resource->group && pipeline->bindings[j].binding==resource->binding) {
                    if (found!=SIZE_MAX) return false;
                    found=j;
                }
            if (found==SIZE_MAX) return false;
            canonical[selected.binding_count]=found;
            selected.bindings[selected.binding_count++]=pipeline->bindings[found];
        }
        native_d3d12_compute *map=&result.stages[stage];
        if (!native_d3d12_stage_bindings_for(&selected,&stages[stage],
            stage==0 ? DOE_COMPILER_STAGE_VERTEX : DOE_COMPILER_STAGE_FRAGMENT,&layouts[stage],map) ||
            map->dispatch_index!=UINT32_MAX || map->guard) return false;
        for (size_t i=0;i<map->buffer_count;++i) map->buffer_indices[i]=canonical[map->buffer_indices[i]];
        for (size_t i=0;i<map->texture_count;++i) map->texture_indices[i]=canonical[map->texture_indices[i]];
        for (size_t i=0;i<map->sampler_count;++i) map->sampler_indices[i]=canonical[map->sampler_indices[i]];
    }
    memcpy(out,layouts,sizeof(layouts));
    *maps=result;
    return true;
}

bool native_d3d12_graphics_maps(const native_pipeline *pipeline,const native_stage stages[2],
    native_d3d12_layout out[2],native_d3d12_graphics *maps)
{
    return native_d3d12_graphics_maps_count(pipeline,stages,2,out,maps);
}

native_graphics_limits native_d3d12_interface_limits(void)
{
    /* Real SM6 pixel semantics plus the FL11+ 32-register stage envelope. */
    return (native_graphics_limits){128,128,true};
}
