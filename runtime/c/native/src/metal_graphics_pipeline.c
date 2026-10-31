/* SPDX-License-Identifier: MIT */
#include "metal_graphics_internal.h"
#include <stdlib.h>

static void release_graphics(void *userdata, void *handle)
{
    (void)userdata;
    native_pipeline *pipeline = handle;
    if (!pipeline) return;
    native_lock(pipeline->device);
    doe_mtl_render_pipeline_release(pipeline->handle.metal_render);
    native_unlock(pipeline->device);
    free(pipeline->metal_graphics);
    free(pipeline);
}

/* Exact SDK-bound MTLVertexFormat conversion; scalar/layout admission is shared. */
static bool vertex_format(WGPUVertexFormat format, uint32_t *out)
{
    switch (format) {
    case WGPUVertexFormat_Uint8: *out = DOE_MTL_VERTEX_UCHAR; return true;
    case WGPUVertexFormat_Uint8x2: *out = DOE_MTL_VERTEX_UCHAR2; return true;
    case WGPUVertexFormat_Uint8x4: *out = DOE_MTL_VERTEX_UCHAR4; return true;
    case WGPUVertexFormat_Sint8: *out = DOE_MTL_VERTEX_CHAR; return true;
    case WGPUVertexFormat_Sint8x2: *out = DOE_MTL_VERTEX_CHAR2; return true;
    case WGPUVertexFormat_Sint8x4: *out = DOE_MTL_VERTEX_CHAR4; return true;
    case WGPUVertexFormat_Unorm8: *out = DOE_MTL_VERTEX_UCHAR_NORMALIZED; return true;
    case WGPUVertexFormat_Unorm8x2: *out = DOE_MTL_VERTEX_UCHAR2_NORMALIZED; return true;
    case WGPUVertexFormat_Unorm8x4: *out = DOE_MTL_VERTEX_UCHAR4_NORMALIZED; return true;
    case WGPUVertexFormat_Snorm8: *out = DOE_MTL_VERTEX_CHAR_NORMALIZED; return true;
    case WGPUVertexFormat_Snorm8x2: *out = DOE_MTL_VERTEX_CHAR2_NORMALIZED; return true;
    case WGPUVertexFormat_Snorm8x4: *out = DOE_MTL_VERTEX_CHAR4_NORMALIZED; return true;
    case WGPUVertexFormat_Uint16: *out = DOE_MTL_VERTEX_USHORT; return true;
    case WGPUVertexFormat_Uint16x2: *out = DOE_MTL_VERTEX_USHORT2; return true;
    case WGPUVertexFormat_Uint16x4: *out = DOE_MTL_VERTEX_USHORT4; return true;
    case WGPUVertexFormat_Sint16: *out = DOE_MTL_VERTEX_SHORT; return true;
    case WGPUVertexFormat_Sint16x2: *out = DOE_MTL_VERTEX_SHORT2; return true;
    case WGPUVertexFormat_Sint16x4: *out = DOE_MTL_VERTEX_SHORT4; return true;
    case WGPUVertexFormat_Unorm16: *out = DOE_MTL_VERTEX_USHORT_NORMALIZED; return true;
    case WGPUVertexFormat_Unorm16x2: *out = DOE_MTL_VERTEX_USHORT2_NORMALIZED; return true;
    case WGPUVertexFormat_Unorm16x4: *out = DOE_MTL_VERTEX_USHORT4_NORMALIZED; return true;
    case WGPUVertexFormat_Snorm16: *out = DOE_MTL_VERTEX_SHORT_NORMALIZED; return true;
    case WGPUVertexFormat_Snorm16x2: *out = DOE_MTL_VERTEX_SHORT2_NORMALIZED; return true;
    case WGPUVertexFormat_Snorm16x4: *out = DOE_MTL_VERTEX_SHORT4_NORMALIZED; return true;
    case WGPUVertexFormat_Float16: *out = DOE_MTL_VERTEX_HALF; return true;
    case WGPUVertexFormat_Float16x2: *out = DOE_MTL_VERTEX_HALF2; return true;
    case WGPUVertexFormat_Float16x4: *out = DOE_MTL_VERTEX_HALF4; return true;
    case WGPUVertexFormat_Float32: *out = DOE_MTL_VERTEX_FLOAT; return true;
    case WGPUVertexFormat_Float32x2: *out = DOE_MTL_VERTEX_FLOAT2; return true;
    case WGPUVertexFormat_Float32x3: *out = DOE_MTL_VERTEX_FLOAT3; return true;
    case WGPUVertexFormat_Float32x4: *out = DOE_MTL_VERTEX_FLOAT4; return true;
    case WGPUVertexFormat_Sint32: *out = DOE_MTL_VERTEX_INT; return true;
    case WGPUVertexFormat_Sint32x2: *out = DOE_MTL_VERTEX_INT2; return true;
    case WGPUVertexFormat_Sint32x3: *out = DOE_MTL_VERTEX_INT3; return true;
    case WGPUVertexFormat_Sint32x4: *out = DOE_MTL_VERTEX_INT4; return true;
    case WGPUVertexFormat_Uint32: *out = DOE_MTL_VERTEX_UINT; return true;
    case WGPUVertexFormat_Uint32x2: *out = DOE_MTL_VERTEX_UINT2; return true;
    case WGPUVertexFormat_Uint32x3: *out = DOE_MTL_VERTEX_UINT3; return true;
    case WGPUVertexFormat_Uint32x4: *out = DOE_MTL_VERTEX_UINT4; return true;
    case WGPUVertexFormat_Unorm10_10_10_2: *out = DOE_MTL_VERTEX_UINT1010102_NORMALIZED; return true;
    case WGPUVertexFormat_Unorm8x4BGRA: *out = DOE_MTL_VERTEX_UCHAR4_NORMALIZED_BGRA; return true;
    default: return false;
    }
}

static bool vertices(native_pipeline *pipeline, const WGPUVertexState *source,
    uint32_t sizes_slot, doe_resource_render_pipeline_desc *resource,
    doe_mtl_render_pipeline_desc *native, doe_mtl_vertex_layout *layouts,
    doe_mtl_vertex_attribute *attributes)
{
    const WGPULimits *limits = &pipeline->device->backend.limits;
    if (source->bufferCount > DOE_RESOURCE_MAX_VERTEX_BUFFERS ||
        source->bufferCount > limits->maxVertexBuffers || (source->bufferCount && !source->buffers)) return false;
    resource->vertex_count = pipeline->vertex_count = source->bufferCount;
    bool used[DOE_MTL_BUFFER_SLOTS] = {0};
    if (sizes_slot != UINT32_MAX) used[sizes_slot] = true;
    if (pipeline->metal_graphics->stages[0].immediate_bytes)
        used[DOE_COMPILER_METAL_IMMEDIATE_SLOT] = true;
    for (size_t i = 0; i < pipeline->binding_count; ++i)
        if (pipeline->metal_graphics->stages[0].tables[i] == DOE_COMPILER_TEXT_TABLE_BUFFER)
            used[pipeline->metal_graphics->stages[0].slots[i]] = true;
    for (size_t slot = 0; slot < source->bufferCount; ++slot) {
        const WGPUVertexBufferLayout *buffer = &source->buffers[slot];
        bool hole = !buffer->attributeCount && buffer->stepMode == WGPUVertexStepMode_Undefined;
        WGPUVertexStepMode step = buffer->stepMode;
        if (!step && buffer->attributeCount) step = WGPUVertexStepMode_Vertex;
        if (buffer->nextInChain || (!hole && buffer->arrayStride > limits->maxVertexBufferArrayStride) ||
            (step != WGPUVertexStepMode_Undefined && step != WGPUVertexStepMode_Vertex && step != WGPUVertexStepMode_Instance) ||
            buffer->attributeCount > limits->maxVertexAttributes - native->attribute_count ||
            (buffer->attributeCount && !buffer->attributes)) return false;
        resource->vertices[slot] = (doe_resource_vertex_layout){hole ? 0 : buffer->arrayStride, 0, step};
        pipeline->metal_graphics->vertex_slots[slot] = UINT32_MAX;
        if (!buffer->attributeCount) continue;
        uint32_t target = 0;
        while (target < DOE_MTL_BUFFER_SLOTS && used[target]) ++target;
        if (target == DOE_MTL_BUFFER_SLOTS) return false;
        used[target] = true;
        pipeline->metal_graphics->vertex_slots[slot] = target;
        layouts[native->layout_count++] = (doe_mtl_vertex_layout){target, (uint32_t)buffer->arrayStride,
            buffer->arrayStride ? (step == WGPUVertexStepMode_Instance ? 2 : 1) : 0};
        for (size_t i = 0; i < buffer->attributeCount; ++i) {
            const WGPUVertexAttribute *attribute = &buffer->attributes[i];
            doe_vk_vertex_format admitted;
            uint32_t scalar, bytes, format;
            /* Metal's native component alignment may be weaker. Public vertex
             * offsets use min(4, canonical format bytes), including half/bytes. */
            if (attribute->nextInChain || attribute->shaderLocation >= limits->maxVertexAttributes ||
                attribute->offset > UINT32_MAX || !native_vertex_format(attribute->format, &admitted, &scalar, &bytes) ||
                !bytes || attribute->offset % (bytes < 4 ? bytes : 4) != 0 ||
                !vertex_format(attribute->format, &format) || attribute->offset > UINT32_MAX - bytes ||
                attribute->offset + bytes > (buffer->arrayStride ? buffer->arrayStride :
                    limits->maxVertexBufferArrayStride)) return false;
            for (size_t j = 0; j < native->attribute_count; ++j)
                if (attributes[j].location == attribute->shaderLocation) return false;
            uint64_t end = attribute->offset + bytes;
            if (resource->vertices[slot].last_attribute_end < end) resource->vertices[slot].last_attribute_end = end;
            attributes[native->attribute_count++] = (doe_mtl_vertex_attribute){attribute->shaderLocation,
                target, (uint32_t)attribute->offset, format};
        }
    }
    native->layouts = layouts;
    native->attributes = attributes;
    return true;
}

static uint32_t blend_factor(uint32_t admitted)
{
    /* Shared policy emits Vulkan's named factor representation; Metal uses a
     * separate namespace. This table is conversion only, not another policy. */
    static const uint32_t factors[19] = {0,1,2,3,6,7,4,5,8,9,11,12,0,0,10,15,16,17,18};
    return factors[admitted];
}
static doe_mtl_stencil_face stencil(doe_vk_stencil_face face)
{
    return (doe_mtl_stencil_face){face.compare_operation, face.fail_operation,
        face.depth_fail_operation, face.pass_operation};
}

WGPURenderPipeline native_metal_create_render_pipeline(native_device *device,
    const WGPURenderPipelineDescriptor *descriptor, const native_stage *stages, size_t stage_count,
    native_pipeline_error *error)
{
    *error = (native_pipeline_error){WGPUErrorType_Validation, "Metal render descriptor, reflection or feature unsupported"};
    if (!descriptor || descriptor->nextInChain || descriptor->vertex.nextInChain) return NULL;
    const WGPUFragmentState *fragment = descriptor->fragment;
    if (fragment && (fragment->nextInChain ||
        fragment->targetCount > DOE_MTL_COLOR_ATTACHMENTS || (fragment->targetCount && !fragment->targets))) return NULL;
    doe_vk_render_pipeline_desc fixed = {0};
    doe_resource_render_pipeline_desc resource = {.samples=descriptor->multisample.count,
        .topology=descriptor->primitive.topology, .strip_index_format=descriptor->primitive.stripIndexFormat,
        .color_count=fragment ? (uint32_t)fragment->targetCount : 0};
    native_graphics_limits interfaces = {60,60,true};
    if (!native_render_fixed(device->features, descriptor, &fixed) ||
        !native_render_depth(device->features, descriptor->depthStencil, &fixed, &resource) ||
        !native_render_interfaces(stages, stage_count, &descriptor->vertex, fragment,
            descriptor->multisample.alphaToCoverageEnabled, fixed.topology == DOE_VK_POINTS,
            &interfaces, &device->backend.limits) ||
        !native_render_fragment_depth(stages, stage_count, descriptor->depthStencil)) return NULL;
    doe_mtl_render_pipeline_desc native = {.sample_count=fixed.sample_count,
        .alpha_to_coverage=fixed.alpha_to_coverage,
        .override_sample_mask=!(fragment && stage_count == 2 && !stages[1].imported &&
            stages[1].view.metal_sample_mask == fixed.sample_mask),
        .sample_mask=fixed.sample_mask,
        .color_count=resource.color_count};
    for (uint32_t i = 0; i < resource.color_count; ++i) {
        resource.color_formats[i] = fragment->targets[i].format;
        if (!resource.color_formats[i]) continue;
        doe_mtl_color_target *target = &native.colors[i];
        const doe_vk_color_target *source = &fixed.color_targets[i];
        if (!native_metal_format(device->features, resource.color_formats[i], &target->format)) return NULL;
        target->write_mask = ((source->write_mask & 1) ? 8 : 0) | ((source->write_mask & 2) ? 4 : 0) |
            ((source->write_mask & 4) ? 2 : 0) | ((source->write_mask & 8) ? 1 : 0);
        target->blend = source->blend_enabled;
        target->color = (doe_mtl_blend_component){source->color_operation,
            blend_factor(source->color_source_factor), blend_factor(source->color_destination_factor)};
        target->alpha = (doe_mtl_blend_component){source->alpha_operation,
            blend_factor(source->alpha_source_factor), blend_factor(source->alpha_destination_factor)};
    }
    if (descriptor->depthStencil) {
        doe_mtl_format format;
        if (!native_metal_format(device->features, descriptor->depthStencil->format, &format)) return NULL;
        native.depth_stencil = (doe_mtl_depth_stencil){.depth_format=fixed.depth_test ? format : 0,
            .stencil_format=fixed.stencil_test ? format : 0, .depth_compare=fixed.depth_compare,
            .depth_write=fixed.depth_write, .front=stencil(fixed.stencil_front), .back=stencil(fixed.stencil_back),
            .stencil_read_mask=fixed.stencil_read_mask, .stencil_write_mask=fixed.stencil_write_mask};
    }
    native_pipeline *payload = calloc(1, sizeof(*payload));
    if (payload) payload->metal_graphics = calloc(1, sizeof(*payload->metal_graphics));
    if (!payload || !payload->metal_graphics) {
        free(payload);
        *error = (native_pipeline_error){WGPUErrorType_OutOfMemory, "Metal graphics metadata allocation failed"};
        return NULL;
    }
    payload->device = device;
    payload->metal_graphics->topology = fixed.topology == DOE_VK_POINTS ? 0 : fixed.topology == DOE_VK_LINES ? 1 :
        fixed.topology == DOE_VK_LINE_STRIP ? 2 : fixed.topology == DOE_VK_TRIANGLES ? 3 : 4;
    payload->metal_graphics->winding = fixed.clockwise_front ? 0 : 1;
    payload->metal_graphics->cull = fixed.cull_mask;
    payload->metal_graphics->depth_bias = (float)fixed.depth_bias;
    payload->metal_graphics->depth_slope = fixed.depth_bias_slope_scale;
    payload->metal_graphics->depth_clamp = fixed.depth_bias_clamp;
    payload->metal_graphics->unclipped_depth = fixed.unclipped_depth;
    doe_resource_pipeline_layout *layout = (doe_resource_pipeline_layout *)descriptor->layout;
    bool automatic = !layout;
    doe_resource_render_pipeline *pipeline = NULL;
    doe_mtl_vertex_layout layouts[DOE_RESOURCE_MAX_VERTEX_BUFFERS];
    doe_mtl_vertex_attribute attributes[16];
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    doe_resource_status status = automatic ? native_automatic_layout(device, stages, stage_count, &layout) : DOE_RESOURCE_OK;
    if (status == DOE_RESOURCE_OK && (!layout || doe_resource_pipeline_layout_device(layout) != device ||
        !native_pipeline_bindings(payload, stages, stage_count, layout) ||
        !native_metal_stage_map_build(payload, &stages[0], DOE_COMPILER_STAGE_VERTEX,
            &payload->metal_graphics->stages[0], &native.vertex) ||
        (fragment && !native_metal_stage_map_build(payload, &stages[1], DOE_COMPILER_STAGE_FRAGMENT,
            &payload->metal_graphics->stages[1], &native.fragment)) ||
        !vertices(payload, &descriptor->vertex, native.vertex.sizes_slot, &resource, &native, layouts, attributes)))
        status = DOE_RESOURCE_UNSUPPORTED;
    resource.immediate_required_bytes = payload->immediate_required_bytes;
    doe_resource_binding_minimum minimums[NATIVE_MAX_PIPELINE_BINDINGS];
    resource.minimums = minimums;
    resource.minimum_count = native_pipeline_minimums(payload, minimums);
    if (status == DOE_RESOURCE_OK && failure.status == NATIVE_OK) {
        doe_mtl_error detail = {0};
        doe_mtl_status created = doe_mtl_render_pipeline_create(device->backend.value.metal.context,
            &native, &payload->handle.metal_render, &detail);
        failure = native_failure_metal(created, &detail);
        if (failure.status == NATIVE_OK) status = doe_resource_render_pipeline_create(&device->resources, layout,
            &resource, (doe_resource_native){payload,NULL,release_graphics}, &pipeline);
    }
    if (automatic) doe_resource_pipeline_layout_release(layout);
    native_unlock(device);
    if (!pipeline) {
        release_graphics(NULL, payload);
        error->type = failure.status == NATIVE_OK ? native_resource_error(status) : native_result_error(failure.status);
        if (failure.status == NATIVE_OK) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        native_failure_format(&failure, "Metal render pipeline creation or layout admission failed", error->message);
        return NULL;
    }
    native_label(device, pipeline, descriptor->label);
    return (WGPURenderPipeline)pipeline;
}
