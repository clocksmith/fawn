/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

doe_mtl_status doe_mtl_objc_command(void *userdata, void *queue, void **out,
    doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *command;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    command = doe_mtl_objc_object(runtime, queue, runtime->new_command);
    if (command != NULL) command = doe_mtl_objc_object(runtime, command, runtime->retain);
    doe_mtl_objc_drain(runtime, pool);
    if (command == NULL)
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "commandBufferWithUnretainedReferences", error);
    *out = command;
    return doe_mtl_fail(DOE_MTL_OK, "commandBufferWithUnretainedReferences", error);
}

doe_mtl_status doe_mtl_objc_copy(void *userdata, void *command, void *source,
    size_t source_offset, void *destination, size_t destination_offset, size_t length,
    doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *encoder;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    encoder = doe_mtl_objc_object(runtime, command, runtime->blit_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blitCommandEncoder", error);
    }
    runtime->copy_call(encoder, runtime->copy_buffer,
        source, source_offset, destination, destination_offset, length);
    doe_mtl_objc_void(runtime, encoder, runtime->end_encoding);
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(DOE_MTL_OK, "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:", error);
}

doe_mtl_status doe_mtl_objc_fill(void *userdata, void *command, void *buffer,
    size_t offset, size_t length, uint8_t value, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *encoder;
    const doe_mtl_range_abi range = {offset, length};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    encoder = doe_mtl_objc_object(runtime, command, runtime->blit_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blitCommandEncoder", error);
    }
    runtime->fill_call(encoder, runtime->fill_buffer, buffer, range, value);
    doe_mtl_objc_void(runtime, encoder, runtime->end_encoding);
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(DOE_MTL_OK, "fillBuffer:range:value:", error);
}

static void bind_compute(doe_mtl_objc *runtime, void *encoder, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot) {
    uint32_t sizes[DOE_MTL_BUFFER_SLOTS] = {0};
    uint32_t i;
    runtime->set_object_call(encoder, runtime->set_pipeline, pipeline);
    for (i = 0; i < DOE_MTL_BUFFER_SLOTS; ++i) {
        runtime->bind_buffer_call(encoder, runtime->set_buffer,
            resources->buffers[i].buffer, resources->buffers[i].offset, i);
        sizes[i] = resources->buffers[i].size;
    }
    if (sizes_slot != UINT32_MAX) {
        runtime->bind_bytes_call(encoder, runtime->set_bytes,
            sizes, ((size_t)sizes_slot + 1) * sizeof(uint32_t), sizes_slot);
    }
    for (i = 0; i < resources->texture_count; ++i)
        runtime->bind_image_call(encoder, runtime->set_texture, resources->textures[i], i);
    for (i = 0; i < resources->sampler_count; ++i)
        runtime->bind_image_call(encoder, runtime->set_sampler, resources->samplers[i], i);
}

doe_mtl_status doe_mtl_objc_dispatch(void *userdata, void *command, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot,
    doe_mtl_dimensions groups, doe_mtl_dimensions workgroup, uint32_t repeat,
    const doe_mtl_native_timestamps *timestamps, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *encoder;
    uint32_t i;
    const doe_mtl_size grid = {groups.x, groups.y, groups.z};
    const doe_mtl_size group = {workgroup.x, workgroup.y, workgroup.z};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    encoder = timestamps != NULL ? doe_mtl_objc_timed_compute(runtime, command, timestamps) :
        doe_mtl_objc_object(runtime, command, runtime->compute_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, timestamps != NULL ?
            "computeCommandEncoderWithDescriptor:" : "computeCommandEncoder", error);
    }
    bind_compute(runtime, encoder, pipeline, resources, sizes_slot);
    for (i = 0; i < repeat; ++i)
        runtime->dispatch_call(encoder, runtime->dispatch, grid, group);
    doe_mtl_objc_void(runtime, encoder, runtime->end_encoding);
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(DOE_MTL_OK, "dispatchThreadgroups:threadsPerThreadgroup:", error);
}

doe_mtl_status doe_mtl_objc_dispatch_indirect(void *userdata, void *command, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot,
    void *arguments, size_t offset, doe_mtl_dimensions workgroup, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    void *encoder = doe_mtl_objc_object(runtime, command, runtime->compute_encoder);
    if (!encoder) {
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "computeCommandEncoder", error);
    }
    bind_compute(runtime, encoder, pipeline, resources, sizes_slot);
    const doe_mtl_size group = {workgroup.x, workgroup.y, workgroup.z};
    runtime->dispatch_indirect_call(encoder, runtime->dispatch_indirect, arguments, offset, group);
    doe_mtl_objc_void(runtime, encoder, runtime->end_encoding);
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(DOE_MTL_OK,
        "dispatchThreadgroupsWithIndirectBuffer:indirectBufferOffset:threadsPerThreadgroup:", error);
}

void doe_mtl_objc_commit(void *userdata, void *command) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    doe_mtl_objc_void(runtime, command, runtime->commit);
    doe_mtl_objc_drain(runtime, pool);
}

size_t doe_mtl_objc_observe(void *userdata, void *command, bool wait, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    size_t status;
    doe_mtl_fail(DOE_MTL_OK, "command completion", error);
    if (pool == NULL) return SIZE_MAX;
    status = doe_mtl_objc_size(runtime, command, runtime->status);
    /* Matches retained wait_result: never wait an uncommitted buffer. */
    if (wait && (status == 2 || status == 3)) {
        doe_mtl_objc_void(runtime, command, runtime->wait);
        status = doe_mtl_objc_size(runtime, command, runtime->status);
    }
    if (status == 5) {
        void *native_error = doe_mtl_objc_object(runtime, command, runtime->error);
        (void)doe_mtl_objc_error(runtime, native_error, "MTLCommandBuffer.error", error);
        if (error != NULL) error->status = DOE_MTL_EXECUTION_FAILED;
    }
    doe_mtl_objc_drain(runtime, pool);
    return status;
}
