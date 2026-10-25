/* SPDX-License-Identifier: MIT */
#include "d3d12_binding_internal.h"

static const DoeCompilerHandleV6 *handle(const doe_shader_view *view,uint32_t index)
{
    const DoeCompilerHandleV6 *result = NULL;
    if (view->handle_count && !view->handles) return NULL;
    for (size_t i = 0; i < view->handle_count; ++i) if (view->handles[i].resource_index == index) {
        if (result) return NULL;
        result = &view->handles[i];
    }
    return result;
}
static bool storage_format(uint32_t value,doe_dx_format *out,uint32_t *scalar)
{
    switch (value) {
    case DOE_COMPILER_FORMAT_RGBA8UNORM: *out=DOE_DX_RGBA8_UNORM; *scalar=DOE_COMPILER_LEAF_F32; return true;
    case DOE_COMPILER_FORMAT_RGBA8UINT: *out=DOE_DX_RGBA8_UINT; *scalar=DOE_COMPILER_LEAF_U32; return true;
    case DOE_COMPILER_FORMAT_R32FLOAT: *out=DOE_DX_R32_FLOAT; *scalar=DOE_COMPILER_LEAF_F32; return true;
    case DOE_COMPILER_FORMAT_R32UINT: *out=DOE_DX_R32_UINT; *scalar=DOE_COMPILER_LEAF_U32; return true;
    default: return false;
    }
}
static bool texture(const DoeCompilerResourceV4 *resource,const DoeCompilerHandleV6 *typed,
    const DoeCompilerHlslBindingV11 *target,doe_dx_texture_binding_layout *out,doe_dx_texture_contract *contract)
{
    if (!typed || typed->view_dimension != DOE_COMPILER_VIEW_2D || typed->flags || target->stride) return false;
    *out = (doe_dx_texture_binding_layout){.shader_register=target->shader_register,.register_space=target->register_space};
    if (resource->kind == DOE_COMPILER_STORAGE_TEXTURE) {
        uint32_t scalar = 0;
        if (!storage_format(typed->storage_format,&out->format,&scalar) || typed->sample_scalar != scalar) return false;
        if (resource->access == DOE_COMPILER_READ) out->access = DOE_DX_READ_ONLY;
        else if (resource->access == DOE_COMPILER_WRITE || resource->access == DOE_COMPILER_READ_WRITE)
            out->access = DOE_DX_READ_WRITE;
        else return false;
        *contract = (doe_dx_texture_contract){.storage=true};
    } else if (resource->kind == DOE_COMPILER_SAMPLED_TEXTURE && !typed->storage_format &&
        resource->access == DOE_COMPILER_READ) {
        out->access = DOE_DX_READ_ONLY;
        if (typed->sample_scalar == DOE_COMPILER_LEAF_F32) contract->sample_class=DOE_DX_SAMPLE_FLOAT;
        else if (typed->sample_scalar == DOE_COMPILER_LEAF_U32) contract->sample_class=DOE_DX_SAMPLE_UINT;
        else return false;
    } else return false;
    return target->register_kind == (out->access == DOE_DX_READ_ONLY ?
        DOE_COMPILER_HLSL_REGISTER_SRV : DOE_COMPILER_HLSL_REGISTER_UAV);
}

bool native_d3d12_stage_bindings_for(const native_pipeline *pipeline,const native_stage *stage,
    uint32_t expected_stage,native_d3d12_layout *out,native_d3d12_compute *map)
{
    if (!pipeline || !stage || !out || !map ||
        (expected_stage!=DOE_COMPILER_STAGE_COMPUTE && expected_stage!=DOE_COMPILER_STAGE_VERTEX &&
         expected_stage!=DOE_COMPILER_STAGE_FRAGMENT)) return false;
    const doe_shader_view *view = &stage->view;
    const DoeCompilerEntryV5 *entry = stage->entry;
    if (view->target != DOE_SHADER_HLSL || view->target_language_version != 600 ||
        !view->entries || !entry || (view->counts.resources && !view->resources) ||
        (view->counts.entry_references && !view->entry_references) ||
        (view->hlsl_binding_count && !view->hlsl_bindings) || !view->hlsl_entries ||
        pipeline->binding_count > NATIVE_MAX_PIPELINE_BINDINGS)
        return false;
    size_t selected = SIZE_MAX;
    for (size_t i = 0; i < view->counts.entries; ++i) if (&view->entries[i] == entry) selected=i;
    if (selected == SIZE_MAX || entry->stage != expected_stage ||
        entry->first_resource_reference > view->counts.entry_references ||
        entry->resource_reference_count > view->counts.entry_references - entry->first_resource_reference ||
        entry->resource_reference_count != pipeline->binding_count) return false;
    native_d3d12_layout layout = {0};
    native_d3d12_compute result = {.dispatch_index=UINT32_MAX};
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        const native_binding *public = &pipeline->bindings[i];
        const DoeCompilerResourceV4 *resource = NULL;
        uint32_t index = UINT32_MAX;
        for (size_t j = 0; j < entry->resource_reference_count; ++j) {
            uint32_t candidate = view->entry_references[entry->first_resource_reference + j];
            if (candidate >= view->counts.resources) return false;
            if (view->resources[candidate].group != public->group || view->resources[candidate].binding != public->binding) continue;
            if (resource) return false;
            resource = &view->resources[candidate];
            index = candidate;
        }
        const DoeCompilerHlslBindingV11 *target = NULL;
        for (size_t j = 0; j < view->hlsl_binding_count; ++j) if (view->hlsl_bindings[j].resource_index == index) {
            if (target) return false;
            target = &view->hlsl_bindings[j];
        }
        if (!resource || !target || target->shader_register == UINT32_MAX || target->register_space == UINT32_MAX)
            return false;
        if (public->kind == DOE_BIND_BUFFER) {
            if (result.buffer_count == DOE_DX_MAX_BINDINGS) return false;
            size_t slot = result.buffer_count++;
            bool uniform = resource->kind == DOE_COMPILER_UNIFORM_BUFFER;
            if ((!uniform && resource->kind != DOE_COMPILER_STORAGE_BUFFER) ||
                (uniform && resource->access != DOE_COMPILER_READ)) return false;
            WGPUBufferBindingType required = uniform ? WGPUBufferBindingType_Uniform :
                resource->access == DOE_COMPILER_READ ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
            uint32_t expected = uniform ? DOE_COMPILER_HLSL_REGISTER_CBV :
                resource->access == DOE_COMPILER_READ ? DOE_COMPILER_HLSL_REGISTER_SRV :
                resource->access == DOE_COMPILER_READ_WRITE ? DOE_COMPILER_HLSL_REGISTER_UAV : 0;
            if (public->buffer_type != required || !expected || target->register_kind != expected ||
                target->stride != (uniform ? 0u : 4u)) return false;
            layout.buffers[slot] = (doe_dx_binding_layout){.shader_register=target->shader_register,
                .register_space=target->register_space,.stride=target->stride,
                .access=expected == DOE_COMPILER_HLSL_REGISTER_UAV ? DOE_DX_READ_WRITE : DOE_DX_READ_ONLY,
                .view=uniform ? DOE_DX_UNIFORM : DOE_DX_STRUCTURED};
            result.buffer_indices[slot] = i;
            result.uniform[slot] = uniform;
        } else if (public->kind == DOE_BIND_TEXTURE || public->kind == DOE_BIND_STORAGE_TEXTURE) {
            if (result.texture_count == DOE_DX_MAX_BINDINGS ||
                (public->kind == DOE_BIND_TEXTURE) != (resource->kind == DOE_COMPILER_SAMPLED_TEXTURE)) return false;
            size_t slot = result.texture_count++;
            if (!texture(resource,handle(view,index),target,&layout.textures[slot],&layout.contracts[slot])) return false;
            result.texture_indices[slot] = i;
        } else if (public->kind == DOE_BIND_SAMPLER) {
            const DoeCompilerHandleV6 *typed = handle(view,index);
            if (result.sampler_count == DOE_DX_MAX_DYNAMIC_SAMPLERS || resource->kind != DOE_COMPILER_SAMPLER ||
                resource->access != DOE_COMPILER_READ || !typed || typed->view_dimension || typed->sample_scalar ||
                typed->storage_format || (typed->flags & ~DOE_COMPILER_HANDLE_COMPARISON) ||
                target->register_kind != DOE_COMPILER_HLSL_REGISTER_SAMPLER || target->stride) return false;
            size_t slot = result.sampler_count++;
            layout.samplers[slot] = (doe_dx_sampler_layout){target->shader_register,target->register_space,
                (typed->flags & DOE_COMPILER_HANDLE_COMPARISON) != 0};
            result.sampler_indices[slot] = i;
        } else return false;
    }
    const DoeCompilerHlslEntryV11 *native = NULL;
    for (size_t i = 0; i < view->hlsl_entry_count; ++i) if (view->hlsl_entries[i].entry_index == selected) {
        if (native) return false;
        native = &view->hlsl_entries[i];
    }
    if (!native || native->stage != expected_stage || native->flags) return false;
    if (expected_stage != DOE_COMPILER_STAGE_COMPUTE && native->dispatch_bytes) return false;
    if (!native->dispatch_bytes) {
        if (native->dispatch_register != UINT32_MAX || native->dispatch_space != UINT32_MAX) return false;
    } else {
        if (native->dispatch_bytes != 16 || result.buffer_count == DOE_DX_MAX_BINDINGS ||
            native->dispatch_register == UINT32_MAX || native->dispatch_space == UINT32_MAX) return false;
        result.dispatch_index = (uint32_t)result.buffer_count;
        layout.buffers[result.buffer_count++] = (doe_dx_binding_layout){.shader_register=native->dispatch_register,
            .register_space=native->dispatch_space,.access=DOE_DX_READ_ONLY,.view=DOE_DX_UNIFORM};
    }
    if (result.buffer_count + result.texture_count > DOE_DX_MAX_BINDINGS) return false;
    *out = layout;
    *map = result;
    return true;
}

bool native_d3d12_stage_bindings(const native_pipeline *pipeline,const native_stage *stage,
    native_d3d12_layout *out,native_d3d12_compute *map)
{
    return native_d3d12_stage_bindings_for(pipeline,stage,DOE_COMPILER_STAGE_COMPUTE,out,map);
}

bool native_d3d12_storage_kernel_layout(const doe_shader_view *view,uint32_t workgroup,size_t count,doe_dx_binding_layout out[3])
{
    if (!view || !out || !count || count>3 || !workgroup || view->counts.entries!=1 || !view->entries || view->counts.resources!=count || !view->resources)
        return false;
    const DoeCompilerEntryV5 *entry=&view->entries[0];
    if (entry->workgroup_storage_bytes || entry->workgroup_size[0]!=workgroup ||
        entry->workgroup_size[1]!=1 || entry->workgroup_size[2]!=1) return false;
    native_pipeline expected={.binding_count=count};
    for (size_t i=0;i<count;++i) {
        expected.bindings[i]=(native_binding){.group=0,.binding=(uint32_t)i,.kind=DOE_BIND_BUFFER,
            .buffer_type=i ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage};
    }
    native_stage stage={.view=*view,.entry=entry,.visibility=WGPUShaderStage_Compute};
    native_d3d12_layout layout;
    native_d3d12_compute map;
    if (!native_d3d12_stage_bindings(&expected,&stage,&layout,&map) || map.buffer_count!=count ||
        map.texture_count || map.sampler_count || map.dispatch_index!=UINT32_MAX) return false;
    for (size_t i=0;i<count;++i) {
        if (map.buffer_indices[i]!=i || map.uniform[i]) return false;
    }
    memcpy(out,layout.buffers,count*sizeof(*out));
    return true;
}
