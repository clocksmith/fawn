/* SPDX-License-Identifier: MIT */
#include "d3d12_graphics_internal.h"

bool native_d3d12_graphics_maps(const native_pipeline *pipeline,const native_stage stages[2],
    native_d3d12_layout out[2],native_d3d12_graphics *maps)
{
    if (!pipeline || !stages || !out || !maps || pipeline->binding_count>NATIVE_MAX_PIPELINE_BINDINGS) return false;
    native_d3d12_graphics result={0};
    native_d3d12_layout layouts[2];
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
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
