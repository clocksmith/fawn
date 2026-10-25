/* SPDX-License-Identifier: MIT */
#include "native_import_layout.h"

/* Native source imports have no WGSL reflection. Explicit layout entries are
 * the caller's binding contract, not inferred shader usage or TranslationInfo.
 * The ordinary resource owner still validates and retains every bound object. */
bool native_import_layout_bindings(native_pipeline *pipeline, const native_stage *stage,
    doe_resource_pipeline_layout *layout)
{
    if (!pipeline || !stage || !stage->imported || !layout ||
        doe_resource_pipeline_layout_immediate_size(layout)) return false;
    const WGPULimits *limits = &pipeline->device->backend.limits;
    uint32_t uniforms=0,storage=0,textures=0,storage_textures=0,samplers=0;
    for (uint32_t group=0; group<DOE_RESOURCE_MAX_GROUPS; ++group) {
        doe_resource_layout *owner=doe_resource_pipeline_layout_group(layout,group);
        size_t count=0;
        const doe_resource_layout_entry *entries=doe_resource_layout_entries(owner,&count);
        if (count && group>=limits->maxBindGroups) return false;
        for (size_t i=0; i<count; ++i) {
            const doe_resource_layout_entry *entry=&entries[i];
            if (!(entry->visibility & stage->visibility)) continue;
            /* Raw HLSL retained no native resource-register/element contract.
             * Reject visible rows before graphics per-stage projection could
             * otherwise erase them and accidentally claim an empty signature. */
            if (stage->imported->target==DOE_SHADER_HLSL) return false;
            if (entry->binding>=limits->maxBindingsPerBindGroup) return false;
            bool writable=false;
            switch (entry->kind) {
            case DOE_BIND_BUFFER:
                if (entry->buffer_type==WGPUBufferBindingType_Uniform) {
                    if (++uniforms>limits->maxUniformBuffersPerShaderStage) return false;
                } else {
                    if (++storage>limits->maxStorageBuffersPerShaderStage) return false;
                    writable=entry->buffer_type==WGPUBufferBindingType_Storage;
                }
                break;
            case DOE_BIND_TEXTURE:
            case DOE_BIND_EXTERNAL_TEXTURE:
                if (++textures>limits->maxSampledTexturesPerShaderStage) return false;
                break;
            case DOE_BIND_STORAGE_TEXTURE:
                if (++storage_textures>limits->maxStorageTexturesPerShaderStage) return false;
                writable=entry->storage_access!=WGPUStorageTextureAccess_ReadOnly;
                break;
            case DOE_BIND_SAMPLER:
                if (++samplers>limits->maxSamplersPerShaderStage) return false;
                break;
            default: return false;
            }
            if (writable && stage->visibility==WGPUShaderStage_Vertex) return false;
            if (writable && stage->visibility==WGPUShaderStage_Fragment) {
                const native_backend *backend=&pipeline->device->backend;
                if (backend->kind==WGPUBackendType_Vulkan && !backend->value.vulkan.caps.fragment_stores_and_atomics)
                    return false;
                if (backend->kind==WGPUBackendType_D3D12 && (backend->value.d3d12.caps.feature_level<0xb000 ||
                    backend->value.d3d12.caps.shader_model<0x60)) return false;
            }
            size_t at=0;
            while (at<pipeline->binding_count && (pipeline->bindings[at].group!=group ||
                pipeline->bindings[at].binding!=entry->binding)) ++at;
            if (at==pipeline->binding_count) {
                if (at==NATIVE_MAX_PIPELINE_BINDINGS) return false;
                pipeline->bindings[at]=(native_binding){group,entry->binding,entry->kind,entry->buffer_type};
                ++pipeline->binding_count;
            } else if (pipeline->bindings[at].kind!=entry->kind ||
                pipeline->bindings[at].buffer_type!=entry->buffer_type) return false;
            pipeline->imported_binding_visibility[at] |= stage->visibility;
            if (pipeline->minimum_sizes[at]<entry->minimum_size)
                pipeline->minimum_sizes[at]=entry->minimum_size;
        }
    }
    return true;
}
