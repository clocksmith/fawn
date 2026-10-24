/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *open_caps(const doe_dx_caps *caps) {
    mock.override_caps = true;
    mock.reported_caps = *caps;
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    return context;
}
static void finish(doe_dx_context *context) {
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void mixed_descriptor_limits(void) {
    unsigned char code = 1;
    doe_dx_binding_layout buffers[20];
    doe_dx_texture_binding_layout textures[20];
    doe_dx_static_sampler samplers[16];
    for (uint32_t index = 0; index < 20; ++index) {
        buffers[index] = (doe_dx_binding_layout){index,0,index < 12 ? 0u : 4u,
            index < 12 ? DOE_DX_READ_ONLY : DOE_DX_READ_WRITE,
            index < 12 ? DOE_DX_UNIFORM : DOE_DX_STRUCTURED};
        textures[index] = (doe_dx_texture_binding_layout){index,1,DOE_DX_R32_UINT,
            index < 16 ? DOE_DX_READ_ONLY : DOE_DX_READ_WRITE};
    }
    for (uint32_t index = 0; index < 16; ++index)
        samplers[index] = (doe_dx_static_sampler){index,0,DOE_DX_NEAREST,DOE_DX_CLAMP,DOE_DX_CLAMP};
    const doe_dx_resource_pipeline_desc desc = {{&code,1,buffers,20},textures,20,samplers,16};
    for (unsigned bound = 0; bound < 6; ++bound) {
        doe_dx_caps caps = mock_default_caps();
        if (bound == 1) caps.limits.max_descriptors = 39;
        if (bound == 2) caps.limits.max_cbvs = 11;
        if (bound == 3) caps.limits.max_srvs = 15;
        if (bound == 4) caps.limits.max_uavs = 11;
        if (bound == 5) caps.limits.max_static_samplers = 15;
        doe_dx_context *context = open_caps(&caps);
        doe_dx_pipeline *pipeline = NULL;
        size_t allocations = mock.allocation_attempts;
        doe_dx_status status = doe_dx_pipeline_create_resources(context, &desc, &pipeline, NULL);
        if (bound == 0) {
            assert(status == DOE_DX_OK && pipeline);
            assert(pipeline->binding_count == 20 && pipeline->texture_count == 20);
            assert(mock.pipeline_desc.sampler_count == 16);
            doe_dx_pipeline_release(pipeline);
        } else {
            assert(status == DOE_DX_RANGE && !pipeline && !mock.pipelines);
            assert(mock.allocation_attempts == allocations);
        }
        finish(context);
    }
}

static void retire(doe_dx_context *context, doe_dx_commands *commands) {
    assert(doe_dx_commands_submit(commands, NULL) == DOE_DX_OK);
    doe_dx_commands_release(commands);
    mock.completed = mock.last_signal;
    assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
}

static void full_descriptor_lifetime(void) {
    doe_dx_caps caps = mock_default_caps();
    doe_dx_context *context = open_caps(&caps);
    doe_dx_buffer *upload = NULL;
    assert(doe_dx_buffer_create(context,4,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    doe_dx_buffer *buffers[DOE_DX_MAX_BINDINGS];
    doe_dx_binding_layout layouts[DOE_DX_MAX_BINDINGS];
    doe_dx_buffer_binding bindings[DOE_DX_MAX_BINDINGS];
    for (uint32_t index = 0; index < DOE_DX_MAX_BINDINGS; ++index) {
        assert(doe_dx_buffer_create(context,4,DOE_DX_DEFAULT,&buffers[index],NULL) == DOE_DX_OK);
        layouts[index] = (doe_dx_binding_layout){index,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
        bindings[index] = (doe_dx_buffer_binding){buffers[index],0,4};
    }
    /* The transfer source also consumes a lease; initialize in two real owner
     * command batches instead of inflating the existing64-resource budget. */
    doe_dx_commands *commands = NULL;
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    for (size_t index = 0; index < DOE_DX_MAX_BINDINGS - 1; ++index)
        assert(doe_dx_commands_copy(commands,upload,0,buffers[index],0,4,NULL) == DOE_DX_OK);
    assert(commands->buffer_count == DOE_DX_MAX_COMMAND_RESOURCES);
    retire(context,commands);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,upload,0,buffers[DOE_DX_MAX_BINDINGS-1],0,4,NULL) == DOE_DX_OK);
    retire(context,commands);
    doe_dx_buffer_release(upload);
    unsigned char code = 1;
    const doe_dx_pipeline_desc desc = {&code,1,layouts,DOE_DX_MAX_BINDINGS};
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create(context,&desc,&pipeline,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_dispatch(commands,pipeline,bindings,DOE_DX_MAX_BINDINGS,1,1,1,NULL) == DOE_DX_OK);
    assert(mock.binding_count == DOE_DX_MAX_BINDINGS && commands->buffer_count == DOE_DX_MAX_COMMAND_RESOURCES);
    for (size_t index = 0; index < DOE_DX_MAX_BINDINGS; ++index) {
        assert(mock.bindings[index].buffer == buffers[index]->native);
        doe_dx_buffer_release(buffers[index]);
    }
    doe_dx_pipeline_release(pipeline);
    assert(mock.buffers == DOE_DX_MAX_BINDINGS && mock.pipelines == 1);
    retire(context,commands);
    assert(!mock.buffers && !mock.pipelines && !mock.commands);
    finish(context);
}

static void allocation_and_status(void) {
    doe_dx_caps caps = mock_default_caps();
    caps.limits.max_buffer_size = 1024;
    doe_dx_context *context = open_caps(&caps);
    doe_dx_buffer *buffer = NULL;
    size_t allocations = mock.allocation_attempts;
    assert(doe_dx_buffer_create(context,1025,DOE_DX_DEFAULT,&buffer,NULL) == DOE_DX_RANGE);
    assert(!buffer && !mock.buffers && mock.allocation_attempts == allocations);
    assert(doe_dx_buffer_create(context,1024,DOE_DX_DEFAULT,&buffer,NULL) == DOE_DX_OK);
    doe_dx_buffer_release(buffer);
    assert(doe_dx_context_status(NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_context_status(context) == DOE_DX_OK);
    doe_dx_commands *commands = NULL;
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_OK);
    doe_dx_commands_release(commands);
    mock.lost = true;
    /* An observation reads the existing state; it must not poll native state. */
    assert(doe_dx_context_status(context) == DOE_DX_OK && !mock.waits);
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
    assert(doe_dx_context_status(context) == DOE_DX_DEVICE_LOST && !mock.waits);
    finish(context);
}

static void dispatch_bound(void) {
    doe_dx_caps caps = mock_default_caps();
    caps.limits.max_dispatch_groups[1] = 3;
    doe_dx_context *context = open_caps(&caps);
    unsigned char code = 1;
    const doe_dx_pipeline_desc desc = {&code,1,NULL,0};
    doe_dx_pipeline *pipeline = NULL;
    doe_dx_commands *commands = NULL;
    assert(doe_dx_pipeline_create(context,&desc,&pipeline,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_dispatch(commands,pipeline,NULL,0,1,4,1,NULL) == DOE_DX_RANGE);
    assert(!mock.dispatches && !commands->pipeline_count);
    assert(doe_dx_commands_dispatch(commands,pipeline,NULL,0,1,3,1,NULL) == DOE_DX_OK);
    doe_dx_pipeline_release(pipeline);
    doe_dx_commands_release(commands);
    finish(context);
}

int main(void) {
    mixed_descriptor_limits();
    full_descriptor_lifetime();
    allocation_and_status();
    dispatch_bound();
    puts("D3D12 queried-cap consumption: 4 host groups passed; programmed facts, no SDK/device execution");
    return 0;
}
