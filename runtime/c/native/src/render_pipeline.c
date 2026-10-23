/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <stdlib.h>

static bool vertices(const WGPUVertexState *source, doe_vk_render_pipeline_desc *native,
    doe_resource_render_pipeline_desc *resource, const doe_vk_caps *caps, doe_vk_vertex_layout layouts[DOE_VK_MAX_VERTEX_BUFFERS],
    doe_vk_vertex_attribute attributes[DOE_VK_MAX_VERTEX_ATTRIBUTES])
{
    if (source->bufferCount > DOE_VK_MAX_VERTEX_BUFFERS || (source->bufferCount && !source->buffers)) return false;
    native->vertex_buffer_count = resource->vertex_count = source->bufferCount;
    native->vertex_buffers = layouts;
    native->attributes = attributes;
    for (size_t slot = 0; slot < source->bufferCount; ++slot) {
        const WGPUVertexBufferLayout *buffer = &source->buffers[slot];
        bool hole = !buffer->attributeCount && buffer->stepMode == WGPUVertexStepMode_Undefined;
        if (buffer->nextInChain || (!hole && buffer->arrayStride > UINT32_MAX) ||
            buffer->attributeCount > DOE_VK_MAX_VERTEX_ATTRIBUTES - native->attribute_count ||
            (buffer->attributeCount && !buffer->attributes)) return false;
        WGPUVertexStepMode step = buffer->stepMode;
        if (step == WGPUVertexStepMode_Undefined && buffer->attributeCount) step = WGPUVertexStepMode_Vertex;
        if (step != WGPUVertexStepMode_Undefined && step != WGPUVertexStepMode_Vertex &&
            step != WGPUVertexStepMode_Instance) return false;
        layouts[slot] = (doe_vk_vertex_layout){hole ? 0 : (uint32_t)buffer->arrayStride, step == WGPUVertexStepMode_Instance};
        resource->vertices[slot] = (doe_resource_vertex_layout){hole ? 0 : buffer->arrayStride, 0, step};
        for (size_t i = 0; i < buffer->attributeCount; ++i) {
            const WGPUVertexAttribute *attribute = &buffer->attributes[i];
            doe_vk_vertex_format format;
            uint32_t scalar, bytes;
            if (attribute->nextInChain || attribute->offset > UINT32_MAX ||
                attribute->shaderLocation >= DOE_VK_MAX_VERTEX_ATTRIBUTES ||
                attribute->shaderLocation >= caps->max_vertex_input_attributes ||
                !native_vertex_format(attribute->format, &format, &scalar, &bytes) ||
                attribute->offset > UINT32_MAX - bytes) return false;
            for (size_t j = 0; j < native->attribute_count; ++j)
                if (attributes[j].location == attribute->shaderLocation) return false;
            uint64_t end = attribute->offset + bytes;
            if (resource->vertices[slot].last_attribute_end < end) resource->vertices[slot].last_attribute_end = end;
            attributes[native->attribute_count++] = (doe_vk_vertex_attribute){attribute->shaderLocation,
                (uint32_t)slot, (uint32_t)attribute->offset, format};
        }
    }
    return true;
}

WGPURenderPipeline native_create_render_pipeline(native_device *device,
    const WGPURenderPipelineDescriptor *descriptor, native_pipeline_error *error)
{
    *error = (native_pipeline_error){WGPUErrorType_Validation, "render descriptor, stage interface or capability unsupported"};
    if (!device || device->backend.kind != WGPUBackendType_Vulkan || !descriptor || descriptor->nextInChain || descriptor->vertex.nextInChain ||
        descriptor->vertex.constantCount) return NULL;
    const WGPUFragmentState *fragment = descriptor->fragment;
    if (fragment && (fragment->nextInChain || fragment->constantCount ||
        (fragment->targetCount && !fragment->targets))) return NULL;
    if (fragment && fragment->targetCount > DOE_RESOURCE_MAX_COLOR_ATTACHMENTS) {
        *error = (native_pipeline_error){WGPUErrorType_Validation, "render pipeline exceeds the color attachment limit"};
        return NULL;
    }
    bool colored = false;
    for (size_t i = 0; fragment && i < fragment->targetCount; ++i)
        colored |= fragment->targets[i].format != WGPUTextureFormat_Undefined;
    if (!colored && !descriptor->depthStencil) return NULL;
    native_stage stages[2] = {0};
    size_t stage_count = fragment ? 2 : 1;
    if (!native_stage_load(device, descriptor->vertex.module, descriptor->vertex.entryPoint,
            DOE_COMPILER_STAGE_VERTEX, &stages[0]) ||
        (fragment && !native_stage_load(device, fragment->module, fragment->entryPoint,
            DOE_COMPILER_STAGE_FRAGMENT, &stages[1]))) return NULL;
    doe_vk_render_pipeline_desc native = {
        .vertex={stages[0].view.words,stages[0].view.counts.words,stages[0].name}
    };
    if (fragment) native.fragment = (doe_vk_shader){stages[1].view.words,stages[1].view.counts.words,stages[1].name};
    doe_resource_render_pipeline_desc resource = {.color_count=fragment ? (uint32_t)fragment->targetCount : 0,
        .samples=descriptor->multisample.count,
        .topology=descriptor->primitive.topology,.strip_index_format=descriptor->primitive.stripIndexFormat};
    for (uint32_t i = 0; i < resource.color_count; ++i) resource.color_formats[i] = fragment->targets[i].format;
    doe_vk_vertex_layout layouts[DOE_VK_MAX_VERTEX_BUFFERS];
    doe_vk_vertex_attribute attributes[DOE_VK_MAX_VERTEX_ATTRIBUTES];
    if (!native_render_fixed(descriptor, &native) || !native_render_depth(descriptor->depthStencil, &native, &resource) ||
        !vertices(&descriptor->vertex, &native, &resource, &device->backend.value.vulkan.caps, layouts, attributes) ||
        !native_render_interfaces(stages, stage_count, &descriptor->vertex, fragment, descriptor->multisample.alphaToCoverageEnabled, &device->backend.value.vulkan.caps)) return NULL;
    native_pipeline *payload = calloc(1, sizeof(*payload));
    if (!payload) { *error = (native_pipeline_error){WGPUErrorType_OutOfMemory,"render pipeline payload allocation failed"}; return NULL; }
    payload->device = device;
    payload->vertex_count = native.vertex_buffer_count;
    doe_resource_render_pipeline *pipeline = NULL;
    doe_resource_pipeline_layout *layout = (doe_resource_pipeline_layout *)descriptor->layout;
    bool automatic = !layout;
    native_lock(device);
    doe_vk_status backend = native_device_status(device);
    doe_resource_status status = automatic ? native_automatic_layout(device, stages, stage_count, &layout) : DOE_RESOURCE_OK;
    if (status == DOE_RESOURCE_OK && (!layout || doe_resource_pipeline_layout_device(layout) != device ||
        !native_pipeline_bindings(payload, stages, stage_count, layout))) status = DOE_RESOURCE_LAYOUT;
    if (status == DOE_RESOURCE_OK && backend == DOE_VK_OK) {
        doe_vk_binding_layout native_bindings[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
        if (!native_vulkan_binding_layout(payload,native_bindings)) abort();
        native.bindings = native_bindings;
        native.binding_count = payload->binding_count;
        backend = doe_vk_render_pipeline_create(device->backend.value.vulkan.context, &native, &payload->handle.vulkan, NULL);
        if (backend == DOE_VK_OK) status = doe_resource_render_pipeline_create(&device->resources, layout, &resource,
            (doe_resource_native){payload,NULL,native_pipeline_release}, &pipeline);
    }
    if (automatic) doe_resource_pipeline_layout_release(layout);
    native_unlock(device);
    if (!pipeline) {
        native_pipeline_release(NULL, payload);
        *error = (native_pipeline_error){backend != DOE_VK_OK ? native_backend_error(backend) : native_resource_error(status),
            "render pipeline creation or layout admission failed"};
        return NULL;
    }
    size_t size;
    doe_label_status label = native_string_size(descriptor->label, &size) ?
        doe_label_store_set(device->labels, pipeline, (const unsigned char *)descriptor->label.data, size) :
        DOE_LABEL_INVALID_ARGUMENT;
    if (label != DOE_LABEL_OK) {
        doe_resource_render_pipeline_release(pipeline);
        *error = (native_pipeline_error){native_label_error(label),"render pipeline label storage failed"};
        return NULL;
    }
    return (WGPURenderPipeline)pipeline;
}
WGPURenderPipeline wgpuDeviceCreateRenderPipeline(WGPUDevice handle, const WGPURenderPipelineDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    native_pipeline_error error;
    WGPURenderPipeline pipeline = native_create_render_pipeline(device, descriptor, &error);
    if (!pipeline) native_error(device, error.type, error.message);
    return pipeline;
}
void wgpuRenderPipelineAddRef(WGPURenderPipeline pipeline) { doe_resource_render_pipeline_retain((doe_resource_render_pipeline *)pipeline); }
void wgpuRenderPipelineRelease(WGPURenderPipeline pipeline) { doe_resource_render_pipeline_release((doe_resource_render_pipeline *)pipeline); }
void wgpuRenderPipelineSetLabel(WGPURenderPipeline pipeline, WGPUStringView label)
{
    native_label(doe_resource_render_pipeline_device((doe_resource_render_pipeline *)pipeline), pipeline, label);
}
WGPUBindGroupLayout wgpuRenderPipelineGetBindGroupLayout(WGPURenderPipeline handle, uint32_t index)
{
    doe_resource_render_pipeline *pipeline = (doe_resource_render_pipeline *)handle;
    native_device *device = doe_resource_render_pipeline_device(pipeline);
    if (!device) return NULL;
    doe_resource_layout *layout = NULL;
    native_lock(device);
    doe_resource_status status = index < device->backend.limits.maxBindGroups ?
        doe_resource_pipeline_layout_get_group(doe_resource_render_pipeline_get_layout(pipeline), index, &layout) : DOE_RESOURCE_RANGE;
    if (status == DOE_RESOURCE_OK) doe_resource_layout_retain(layout);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "render bind group layout unavailable");
    return (WGPUBindGroupLayout)layout;
}
