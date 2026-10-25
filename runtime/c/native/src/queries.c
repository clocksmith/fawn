/* SPDX-License-Identifier: MIT */
#include "metal_frontend.h"
#include "d3d12_queries.h"
#include <stdlib.h>

/* Called before device publication or under its serialized queue mutex. The
 * compiler owns the generated WGSL's meaning; the backend owns normalization. */
doe_vk_status native_timestamp_initialize(native_device *device)
{
    if (device->timestamp_normalizer) return DOE_VK_OK;
    if (!native_feature_enabled(device, WGPUFeatureName_TimestampQuery) || !native_timestamp_supported(&device->backend.value.vulkan.caps)) return DOE_VK_UNSUPPORTED;
    uint32_t workgroup = native_timestamp_workgroup(&device->backend.value.vulkan.caps);
    char source[DOE_VK_TIMESTAMP_SOURCE_CAPACITY];
    size_t size = 0;
    doe_vk_status status = doe_vk_timestamp_shader_source(device->backend.value.vulkan.caps.timestamp_period_ns,
        device->backend.value.vulkan.caps.timestamp_valid_bits, workgroup, source, sizeof(source), &size);
    if (status != DOE_VK_OK) return status;
    doe_shader *shader = NULL;
    doe_shader_error error = {0};
    doe_shader_status compiled = native_compile_shader(source, size, &shader, &error);
    if (compiled != DOE_SHADER_OK)
        return compiled == DOE_SHADER_OUT_OF_MEMORY ? DOE_VK_OUT_OF_MEMORY : DOE_VK_NATIVE_FAILURE;
    doe_shader_view view;
    status = doe_shader_borrow(shader, &view) == DOE_SHADER_OK && native_shader_capabilities(&view, &device->backend.value.vulkan.caps) ?
        DOE_VK_OK : DOE_VK_NATIVE_FAILURE;
    if (status == DOE_VK_OK) {
        doe_vk_timestamp_normalizer_desc descriptor = {.spirv=view.words, .word_count=view.counts.words,
            .workgroup_size=workgroup, .capacity=DOE_RESOURCE_MAX_QUERIES,
            .period_ns=device->backend.value.vulkan.caps.timestamp_period_ns, .valid_bits=device->backend.value.vulkan.caps.timestamp_valid_bits};
        status = doe_vk_timestamp_normalizer_create(device->backend.value.vulkan.context, &descriptor,
            &device->timestamp_normalizer, NULL);
    }
    doe_shader_release(shader);
    return status;
}

static void release_queries(void *userdata, void *handle)
{
    (void)userdata;
    native_queries *payload = handle;
    native_lock(payload->device);
    if (payload->device->backend.kind == WGPUBackendType_Metal) {
        doe_mtl_buffer_release(payload->metal_results);
        doe_mtl_timestamps_release(payload->metal_timestamps);
    }
    else if (payload->device->backend.kind==WGPUBackendType_D3D12) native_d3d12_queries_release(payload->d3d12_queries);
    else doe_vk_query_pool_release(payload->pool);
    native_unlock(payload->device);
    free(payload);
}
WGPUQuerySet wgpuDeviceCreateQuerySet(WGPUDevice handle, const WGPUQuerySetDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (native_device_dead(device)) return native_lost_query_set(device, descriptor);
    if (device->backend.kind != WGPUBackendType_Vulkan && device->backend.kind != WGPUBackendType_Metal &&
        device->backend.kind != WGPUBackendType_D3D12) {
        native_error(device, WGPUErrorType_Validation, "operation not yet composed for selected backend");
        return NULL;
    }
    doe_resource_status status = descriptor && !descriptor->nextInChain &&
        (descriptor->type == WGPUQueryType_Occlusion ||
            (descriptor->type == WGPUQueryType_Timestamp && native_feature_enabled(device, WGPUFeatureName_TimestampQuery))) ?
        doe_resource_query_set_validate(descriptor->type, descriptor->count) : DOE_RESOURCE_UNSUPPORTED;
    if (status != DOE_RESOURCE_OK) {
        native_error(device, native_resource_error(status), "query descriptor invalid or timestamp feature not enabled");
        return NULL;
    }
    native_queries *payload = calloc(1, sizeof(*payload));
    if (!payload) { native_error(device, WGPUErrorType_OutOfMemory, "query payload allocation failed"); return NULL; }
    payload->device = device;
    doe_resource_query_set *set = NULL;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK && descriptor->count) {
        if (device->backend.kind == WGPUBackendType_Metal) {
            doe_mtl_error detail = {0};
            if (descriptor->type == WGPUQueryType_Timestamp)
                failure = native_failure_metal(doe_mtl_timestamps_create(device->backend.value.metal.context,
                    descriptor->count, &payload->metal_timestamps, &detail), &detail);
            else if ((uint64_t)(descriptor->count - 1) * 8 > device->backend.value.metal.caps.max_visibility_offset)
                failure = native_failure_status(NATIVE_UNSUPPORTED);
            else failure = native_failure_metal(doe_mtl_buffer_create(device->backend.value.metal.context,
                (uint64_t)descriptor->count * 8, DOE_MTL_SHARED, &payload->metal_results, &detail), &detail);
        } else if (device->backend.kind==WGPUBackendType_D3D12) {
            failure=native_d3d12_queries_create(device,descriptor->type,descriptor->count,&payload->d3d12_queries);
        } else {
            doe_vk_status backend = descriptor->type == WGPUQueryType_Occlusion ?
                doe_vk_occlusion_pool_create(device->backend.value.vulkan.context, descriptor->count, &payload->pool, NULL) :
                doe_vk_query_pool_create(device->backend.value.vulkan.context, descriptor->count, &payload->pool, NULL);
            failure = native_failure_status(native_from_vulkan(backend));
        }
    }
    if (failure.status == NATIVE_OK)
        status = doe_resource_query_set_create(&device->resources, descriptor->type, descriptor->count,
            (doe_resource_native){payload, NULL, release_queries}, &set);
    native_unlock(device);
    if (!set) {
        release_queries(NULL, payload);
        if (failure.status != NATIVE_OK) {
            char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
            native_failure_format(&failure, "query set creation failed", message);
            native_error(device, native_result_error(failure.status), message);
        } else native_error(device, native_resource_error(status), "query set creation failed");
    } else native_label(device, set, descriptor->label);
    return (WGPUQuerySet)set;
}
void wgpuQuerySetAddRef(WGPUQuerySet set) { doe_resource_query_set_retain((doe_resource_query_set *)set); }
void wgpuQuerySetRelease(WGPUQuerySet set) { doe_resource_query_set_release((doe_resource_query_set *)set); }
void wgpuQuerySetDestroy(WGPUQuerySet handle)
{
    doe_resource_query_set *set = (doe_resource_query_set *)handle;
    native_device *device = doe_resource_query_set_device(set);
    if (!device) return;
    native_lock(device);
    doe_resource_query_set_destroy(set);
    native_unlock(device);
}
uint32_t wgpuQuerySetGetCount(WGPUQuerySet set) { return doe_resource_query_set_count((doe_resource_query_set *)set); }
WGPUQueryType wgpuQuerySetGetType(WGPUQuerySet set) { return doe_resource_query_set_type((doe_resource_query_set *)set); }
void wgpuQuerySetSetLabel(WGPUQuerySet set, WGPUStringView label)
{
    native_label(doe_resource_query_set_device((doe_resource_query_set *)set), set, label);
}
void wgpuCommandEncoderWriteTimestamp(WGPUCommandEncoder handle, WGPUQuerySet set, uint32_t index)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_encoder_ended(encoder);
    doe_resource_status status = native_feature_enabled(device, WGPUFeatureName_TimestampQuery) ? doe_resource_encoder_write_timestamp(encoder,
        (doe_resource_query_set *)set, index, DOE_TIMESTAMP_COMMAND) :
        doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_UNSUPPORTED);
    native_unlock(device);
    native_encoding_error(device, ended, status, "timestamp recording failed");
}
void wgpuCommandEncoderResolveQuerySet(WGPUCommandEncoder handle, WGPUQuerySet set,
    uint32_t first, uint32_t count, WGPUBuffer destination, uint64_t offset)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    native_lock(device);
    const bool ended = doe_resource_encoder_ended(encoder);
    doe_resource_status status = doe_resource_encoder_resolve_queries(encoder,
        (doe_resource_query_set *)set, first, count, (doe_resource_buffer *)destination, offset);
    native_unlock(device);
    native_encoding_error(device, ended, status, "query resolve recording failed");
}

doe_vk_status native_record_query(doe_vk_commands *commands, const doe_resource_command *record)
{
    if (record->kind == DOE_COMMAND_WRITE_TIMESTAMP) {
        native_queries *payload = doe_resource_query_set_native(record->value.timestamp.set);
        doe_vk_status status = doe_vk_commands_reset_queries(commands, payload->pool, record->value.timestamp.index, 1);
        if (status != DOE_VK_OK) return status;
        doe_vk_timestamp_stage position = record->value.timestamp.position == DOE_TIMESTAMP_PASS_BEGIN ?
            DOE_VK_TIMESTAMP_BEGIN : DOE_VK_TIMESTAMP_END;
        return doe_vk_commands_write_timestamp(commands, payload->pool, record->value.timestamp.index, position);
    }
    native_queries *payload = doe_resource_query_set_native(record->value.resolve.set);
    native_buffer *destination = doe_resource_buffer_native(record->value.resolve.destination);
    if (doe_resource_query_set_type(record->value.resolve.set) == WGPUQueryType_Occlusion)
        return doe_vk_commands_resolve_queries(commands, payload->pool, record->value.resolve.first,
            record->value.resolve.count, destination->handle.vulkan, record->value.resolve.offset);
    doe_vk_status status = native_timestamp_initialize(payload->device);
    if (status != DOE_VK_OK) return status;
    return doe_vk_commands_resolve_queries_ns(commands, payload->pool,
        record->value.resolve.first, record->value.resolve.count,
        payload->device->timestamp_normalizer, destination->handle.vulkan, record->value.resolve.offset);
}
