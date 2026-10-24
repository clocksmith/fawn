/* SPDX-License-Identifier: MIT */
#include "d3d12_graphics_internal.h"
#include <stdlib.h>

#if defined(_WIN32)
static bool descriptor(const native_device *device,const WGPURenderPipelineDescriptor *source,
    const native_stage *stages,size_t count,doe_dx_render_pipeline_desc *out,doe_resource_render_pipeline_desc *resource)
{
    const WGPUFragmentState *fragment=source->fragment;
    if (!fragment || count!=2 || fragment->targetCount!=1 || !fragment->targets || source->vertex.bufferCount ||
        source->depthStencil || source->primitive.stripIndexFormat!=WGPUIndexFormat_Undefined) return false;
    const WGPUColorTargetState *color=&fragment->targets[0];
    if (color->format==WGPUTextureFormat_RGBA8Unorm) out->color_format=DOE_DX_RGBA8_UNORM;
    else if (color->format==WGPUTextureFormat_BGRA8Unorm) out->color_format=DOE_DX_BGRA8_UNORM;
    else return false;
    doe_vk_render_pipeline_desc fixed={0};
    /* Direct3D guarantees32 four-component VS outputs/PS inputs. The actual SDK
     * constants are asserted by the private Windows graphics source; this is
     * a native interface guarantee, not a queried adapter capability. */
    const native_graphics_limits interfaces={128,128,false};
    if (!native_render_fixed(device->features,source,&fixed) || fixed.topology!=DOE_VK_TRIANGLES ||
        fixed.cull_mask || fixed.unclipped_depth || fixed.sample_count!=1 || fixed.alpha_to_coverage ||
        fixed.sample_mask!=UINT32_MAX || color->blend || color->writeMask!=WGPUColorWriteMask_All ||
        !native_render_interfaces(stages,count,&source->vertex,fragment,false,false,&interfaces,&device->backend.limits) ||
        !native_render_fragment_depth(stages,count,NULL)) return false;
    out->front_counter_clockwise=!fixed.clockwise_front;
    *resource=(doe_resource_render_pipeline_desc){.color_count=1,.samples=1,.topology=WGPUPrimitiveTopology_TriangleList,
        .color_formats={color->format}};
    return true;
}
#endif

WGPURenderPipeline native_d3d12_create_render_pipeline(native_device *device,
    const WGPURenderPipelineDescriptor *source,const native_stage *stages,size_t count,native_pipeline_error *error)
{
#if defined(_WIN32)
    *error=(native_pipeline_error){WGPUErrorType_Validation,"D3D12 render state or selected interfaces unsupported"};
    doe_dx_render_pipeline_desc desc={0};
    doe_resource_render_pipeline_desc resource={0};
    if (!descriptor(device,source,stages,count,&desc,&resource)) return NULL;
    doe_dx_shader *compiled[2]={0};
    native_failure failure=native_failure_status(NATIVE_OK);
    for (size_t i=0;i<2 && failure.status==NATIVE_OK;++i) failure=native_d3d12_compile_stage(&stages[i],&compiled[i]);
    native_pipeline *payload=NULL;
    if (failure.status==NATIVE_OK) {
        payload=calloc(1,sizeof(*payload));
        if (payload) payload->d3d12_graphics=calloc(1,sizeof(*payload->d3d12_graphics));
        if (!payload || !payload->d3d12_graphics) failure=native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    doe_resource_render_pipeline *pipeline=NULL;
    doe_resource_pipeline_layout *layout=(doe_resource_pipeline_layout *)source->layout;
    bool automatic=!layout;
    if (failure.status==NATIVE_OK) {
        payload->device=device;
        native_lock(device);
        failure=native_failure_status(native_device_result(device));
        doe_resource_status status=automatic && failure.status==NATIVE_OK ?
            native_automatic_layout(device,stages,count,&layout) : DOE_RESOURCE_OK;
        if (status==DOE_RESOURCE_OK && failure.status==NATIVE_OK && (!layout ||
            doe_resource_pipeline_layout_device(layout)!=device || !native_pipeline_bindings(payload,stages,count,layout)))
            status=DOE_RESOURCE_LAYOUT;
        native_d3d12_layout layouts[2];
        if (status==DOE_RESOURCE_OK && failure.status==NATIVE_OK &&
            !native_d3d12_graphics_maps(payload,stages,layouts,payload->d3d12_graphics)) status=DOE_RESOURCE_UNSUPPORTED;
        if (status==DOE_RESOURCE_OK && failure.status==NATIVE_OK) {
            for (size_t i=0;i<2;++i) {
                const native_d3d12_compute *map=&payload->d3d12_graphics->stages[i];
                size_t bytes=0;
                const uint8_t *code=doe_dx_shader_bytes(compiled[i],&bytes);
                desc.stages[i]=(doe_dx_dynamic_pipeline_desc){.resources={.buffers={code,bytes,layouts[i].buffers,map->buffer_count},
                    .textures=layouts[i].textures,.texture_count=map->texture_count},.texture_contracts=layouts[i].contracts,
                    .samplers=layouts[i].samplers,.sampler_count=map->sampler_count};
            }
            doe_dx_error detail={0};
            doe_dx_status created=doe_dx_pipeline_create_render(device->backend.value.d3d12.context,&desc,&payload->handle.d3d12,&detail);
            failure=native_failure_d3d12(created,&detail);
            if (failure.status==NATIVE_OK) status=doe_resource_render_pipeline_create(&device->resources,layout,&resource,
                (doe_resource_native){payload,NULL,native_pipeline_release},&pipeline);
        }
        if (failure.status==NATIVE_OK && status!=DOE_RESOURCE_OK) {
            error->type=native_resource_error(status);
            native_failure_format(&(native_failure){.status=NATIVE_INVALID_ARGUMENT},"D3D12 graphics layout admission/allocation failed",error->message);
        }
        if (automatic) doe_resource_pipeline_layout_release(layout);
        native_unlock(device);
    }
    for (size_t i=0;i<2;++i) native_d3d12_compiler_release(compiled[i]);
    if (!pipeline) {
        if (payload) {
            if (payload->device) native_pipeline_release(NULL,payload);
            else { free(payload->d3d12_graphics); free(payload); }
        }
        if (failure.status!=NATIVE_OK) {
            error->type=native_result_error(failure.status);
            native_failure_format(&failure,"D3D12 graphics preparation failed",error->message);
        }
        return NULL;
    }
    size_t size;
    doe_label_status label=native_string_size(source->label,&size) ?
        doe_label_store_set(device->labels,pipeline,(const unsigned char *)source->label.data,size) : DOE_LABEL_INVALID_ARGUMENT;
    if (label!=DOE_LABEL_OK) {
        doe_resource_render_pipeline_release(pipeline);
        *error=(native_pipeline_error){native_label_error(label),"D3D12 graphics label allocation failed"};
        return NULL;
    }
    return (WGPURenderPipeline)pipeline;
#else
    (void)device; (void)source; (void)stages; (void)count;
    *error=(native_pipeline_error){WGPUErrorType_Validation,"D3D12 graphics requires Windows SDK provider"};
    return NULL;
#endif
}
