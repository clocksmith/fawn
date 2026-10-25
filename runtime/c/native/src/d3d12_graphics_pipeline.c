/* SPDX-License-Identifier: MIT */
#include "d3d12_draw_guard.h"
#include <stdlib.h>

#if defined(_WIN32)
static bool descriptor(const native_device *device,const WGPURenderPipelineDescriptor *source,
    const native_stage *stages,size_t count,doe_dx_render_pipeline_desc *out,doe_resource_render_pipeline_desc *resource)
{
    const WGPUFragmentState *fragment=source->fragment;
    if (!fragment || count!=2 || fragment->targetCount!=1 || !fragment->targets ||
        (source->depthStencil && source->depthStencil->format!=WGPUTextureFormat_Depth32Float)) return false;
    const WGPUColorTargetState *color=&fragment->targets[0];
    if (color->format==WGPUTextureFormat_RGBA8Unorm) out->color_format=DOE_DX_RGBA8_UNORM;
    else if (color->format==WGPUTextureFormat_BGRA8Unorm) out->color_format=DOE_DX_BGRA8_UNORM;
    else return false;
    doe_vk_render_pipeline_desc fixed={0};
    /* Direct3D guarantees32 four-component VS outputs/PS inputs. The actual SDK
     * constants are asserted by the private Windows graphics source; this is
     * a native interface guarantee, not a queried adapter capability. */
    const native_graphics_limits interfaces={128,128,false};
    if (!native_render_fixed(device->features,source,&fixed) ||
        fixed.unclipped_depth || fixed.sample_count!=1 || fixed.alpha_to_coverage ||
        !native_render_interfaces(stages,count,&source->vertex,fragment,false,fixed.topology==DOE_VK_POINTS,&interfaces,&device->backend.limits) ||
        !native_render_depth(device->features,source->depthStencil,&fixed,resource) ||
        !native_render_fragment_depth(stages,count,source->depthStencil)) return false;
    switch (fixed.topology) {
    case DOE_VK_TRIANGLES: out->topology=DOE_DX_TRIANGLES; break;
    case DOE_VK_TRIANGLE_STRIP: out->topology=DOE_DX_TRIANGLE_STRIP; break;
    case DOE_VK_LINES: out->topology=DOE_DX_LINES; break;
    case DOE_VK_LINE_STRIP: out->topology=DOE_DX_LINE_STRIP; break;
    case DOE_VK_POINTS: out->topology=DOE_DX_POINTS; break;
    }
    switch (source->primitive.stripIndexFormat) {
    case WGPUIndexFormat_Undefined: break;
    case WGPUIndexFormat_Uint16: out->strip_indexed=true; out->strip_index_format=DOE_DX_INDEX_UINT16; break;
    case WGPUIndexFormat_Uint32: out->strip_indexed=true; out->strip_index_format=DOE_DX_INDEX_UINT32; break;
    case WGPUIndexFormat_Force32: return false;
    }
    if (out->strip_indexed && fixed.topology!=DOE_VK_LINE_STRIP && fixed.topology!=DOE_VK_TRIANGLE_STRIP) return false;
    resource->strip_index_format=source->primitive.stripIndexFormat;
    out->front_counter_clockwise=!fixed.clockwise_front;
    switch (fixed.cull_mask) {
    case 0: out->cull=DOE_DX_CULL_NONE; break;
    case 1: out->cull=DOE_DX_CULL_FRONT; break;
    case 2: out->cull=DOE_DX_CULL_BACK; break;
    default: return false;
    }
    if (color->writeMask & ~WGPUColorWriteMask_All) return false;
    out->depth_enabled=source->depthStencil!=NULL;
    out->depth_write=fixed.depth_write;
    out->depth_compare=(doe_dx_compare)fixed.depth_compare;
    out->depth_bias=fixed.depth_bias;
    out->depth_bias_slope=fixed.depth_bias_slope_scale;
    out->depth_bias_clamp=fixed.depth_bias_clamp;
    const doe_vk_color_target *target=&fixed.color_targets[0];
    out->blend_enabled=target->blend_enabled;
    out->color_blend=(doe_dx_blend_component){target->color_source_factor,
        target->color_destination_factor,target->color_operation};
    out->alpha_blend=(doe_dx_blend_component){target->alpha_source_factor,
        target->alpha_destination_factor,target->alpha_operation};
    out->masks_explicit=true;
    out->sample_mask=fixed.sample_mask;
    out->color_write_mask=(uint32_t)color->writeMask;
    resource->color_count=1; resource->samples=1; resource->topology=source->primitive.topology == WGPUPrimitiveTopology_Undefined ?
        WGPUPrimitiveTopology_TriangleList : source->primitive.topology;
    resource->color_formats[0]=color->format;
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
    doe_dx_vertex_layout vertices[DOE_DX_MAX_VERTEX_BUFFERS];
    doe_dx_vertex_attribute attributes[DOE_DX_MAX_VERTEX_ATTRIBUTES];
    /* Validate every caller-owned layout/attribute span before common interface
     * validation traverses it; constructor scratch survives native PSO creation. */
    if (!native_d3d12_graphics_vertices(&source->vertex,device,&desc,&resource,vertices,attributes) ||
        !descriptor(device,source,stages,count,&desc,&resource)) return NULL;
    uint32_t interstage_mask=0;
    if (!native_d3d12_interstage_mask(&stages[1],&interstage_mask)) {
        *error=(native_pipeline_error){WGPUErrorType_Validation,"D3D12 fragment input projection unsupported"};
        return NULL;
    }
    doe_dx_shader *compiled[2]={0};
    native_failure failure=native_failure_status(NATIVE_OK);
    failure=native_d3d12_compile_vertex(&stages[0],interstage_mask,&compiled[0]);
    if (failure.status==NATIVE_OK) failure=native_d3d12_compile_stage(&stages[1],&compiled[1]);
    native_d3d12_kernel_code *guards[2]={0};
    if (failure.status==NATIVE_OK) failure=native_d3d12_draw_compile(false,&guards[0]);
    if (failure.status==NATIVE_OK) failure=native_d3d12_draw_compile(true,&guards[1]);
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
        payload->vertex_count=resource.vertex_count;
        native_lock(device);
        if (failure.status==NATIVE_OK) failure=native_failure_status(native_device_result(device));
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
            if (failure.status==NATIVE_OK) failure=native_d3d12_kernel_pipeline(device,guards[0],&payload->d3d12_graphics->draw_normalize);
            if (failure.status==NATIVE_OK) failure=native_d3d12_kernel_pipeline(device,guards[1],&payload->d3d12_graphics->index_scan);
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
    for (size_t i=0;i<2;++i) {
        native_d3d12_compiler_release(compiled[i]);
        native_d3d12_kernel_code_release(guards[i]);
    }
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
