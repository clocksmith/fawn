/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_MIXED_HOST_H
#define DOE_NATIVE_TEST_METAL_MIXED_HOST_H
#include "mixed_resource_cases.h"
#include "../src/metal_binding_internal.h"

static void mixed_resource_ledger(state *s)
{
    (void)mixed_resources_pixels; /* Host native ledger is not a GPU oracle. */
    unsigned baseline = mocks[2].live;
    mixed_resources resources = {0};
    mixed_resources_create(s, &resources);
    native_pipeline *pipeline = doe_resource_pipeline_native((doe_resource_pipeline *)resources.pipeline);
    assert(pipeline && pipeline->metal_compute && pipeline->binding_count == 5);
    uint32_t slots[5] = {0};
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        const native_binding *binding = &pipeline->bindings[i];
        uint32_t slot = pipeline->metal_compute->slots[i];
        if (binding->group == 0) slots[0] = slot;
        else if (binding->group == 1) slots[1] = slot;
        else if (binding->group == 2) slots[2] = slot;
        else if (binding->binding == 998) slots[3] = slot;
        else slots[4] = slot;
        assert(binding->binding >= 998 && slot < 3);
    }
    native_buffer *output = doe_resource_buffer_native((doe_resource_buffer *)resources.output);
    native_buffer *uniform = doe_resource_buffer_native((doe_resource_buffer *)resources.uniform);
    native_view *sampled = doe_resource_view_native((doe_resource_view *)resources.sampled_view);
    native_view *stored = doe_resource_view_native((doe_resource_view *)resources.stored_view);
    native_sampler *sampler = doe_resource_sampler_native((doe_resource_sampler *)resources.sampler);
    void *output_identity = doe_mtl_buffer_native(output->handle.metal);
    void *uniform_identity = doe_mtl_buffer_native(uniform->handle.metal);
    void *sampled_identity = doe_mtl_texture_view_native(sampled->metal);
    void *stored_identity = doe_mtl_texture_view_native(stored->metal);
    void *sampler_identity = doe_mtl_sampler_native(sampler->metal);
    WGPUCommandEncoder encoder = mixed_resources_record(s, &resources, false);
    unsigned dispatches = mocks[2].dispatches;
    submit(s, encoder);
    assert(mocks[2].dispatches == dispatches + 1);
    assert(mocks[2].bindings[slots[0]].buffer == output_identity && mocks[2].bindings[slots[0]].size == 16);
    assert(mocks[2].bindings[slots[4]].buffer == uniform_identity && mocks[2].bindings[slots[4]].size == 16);
    assert(mocks[2].resources.textures[slots[1]] == sampled_identity);
    assert(mocks[2].resources.textures[slots[3]] == stored_identity);
    assert(mocks[2].resources.samplers[slots[2]] == sampler_identity);
    assert(mocks[2].groups.x == 4 && mocks[2].groups.y == 1 && mocks[2].groups.z == 1);
    /* A native failure after all five typed resources were leased retains its
     * exact detail through deferred public scope delivery, then permits reuse. */
    encoder = mixed_resources_record(s, &resources, false);
    mocks[2].fail = MOCK_DISPATCH;
    exact_message expected = {"Metal: injected native failure (native=-71): owned fault boundary", 0};
    WGPUPopErrorScopeCallbackInfo scope = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=exact_scope, .userdata1=&expected};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    submit(s, encoder);
    mocks[2].fail = MOCK_NONE;
    assert(wgpuDevicePopErrorScope(s->device, scope).id);
    overwrite_stack();
    wgpuInstanceProcessEvents(s->instance);
    assert(expected.calls == 1);
    encoder = mixed_resources_record(s, &resources, false);
    submit(s, encoder);
    mixed_resources_release_inputs(&resources);
    wgpuBufferRelease(resources.readback);
    assert(mocks[2].live == baseline && !s->errors);

    resources = (mixed_resources){0};
    mixed_resources_create(s, &resources);
    encoder = mixed_resources_record(s, &resources, false);
    mixed_resources_release_inputs(&resources);
    wgpuBufferRelease(resources.readback);
    native_device *device = (native_device *)s->device;
    native_lock(device);
    hold = true;
    native_unlock(device);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Internal);
    submit(s, encoder);
    pop(s, WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending && mocks[2].live > baseline);
    hold = false;
    native_queue_settlement settled = native_settle_pending(device);
    assert(settled.complete && settled.failure.status == NATIVE_OK && !device->pending);
    native_unlock(device);
    assert(mocks[2].live == baseline && !s->errors);
}
#endif
