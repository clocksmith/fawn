/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *context(void) {
    doe_dx_context *value = NULL;
    assert(doe_dx_context_create(NULL,&value,NULL) == DOE_DX_OK);
    return value;
}
static doe_dx_commands *commands(doe_dx_context *owner) {
    doe_dx_commands *value = NULL;
    assert(doe_dx_commands_create(owner,&value,NULL) == DOE_DX_OK);
    return value;
}
static void retire(doe_dx_context *owner, doe_dx_commands *work) {
    assert(doe_dx_commands_submit(work,NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    mock.completed = mock.last_signal;
    assert(doe_dx_context_poll(owner,0,NULL) == DOE_DX_OK);
}
static doe_dx_buffer *initialized(doe_dx_context *owner, uint64_t size) {
    doe_dx_buffer *upload = NULL, *value = NULL;
    assert(doe_dx_buffer_create(owner,size,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(owner,size,DOE_DX_DEFAULT,&value,NULL) == DOE_DX_OK);
    doe_dx_commands *work = commands(owner);
    assert(doe_dx_commands_copy(work,upload,0,value,0,size,NULL) == DOE_DX_OK);
    retire(owner,work);
    doe_dx_buffer_release(upload);
    return value;
}
static doe_dx_pipeline *pipeline(doe_dx_context *owner, const doe_dx_binding_layout *layouts, size_t count) {
    const unsigned char code = 1;
    const doe_dx_pipeline_desc desc = {&code,1,layouts,count};
    doe_dx_pipeline *value = NULL;
    assert(doe_dx_pipeline_create(owner,&desc,&value,NULL) == DOE_DX_OK);
    return value;
}
static void finish(doe_dx_context *owner) {
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}

static void read_alias_lifetime(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *buffer = initialized(owner,512);
    const doe_dx_binding_layout layouts[] = {
        {0,0,0,DOE_DX_READ_ONLY,DOE_DX_UNIFORM},
        {0,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED},
        {1,0,0,DOE_DX_READ_ONLY,DOE_DX_UNIFORM}
    };
    const doe_dx_buffer_binding bindings[] = {{buffer,0,256},{buffer,64,256},{buffer,256,256}};
    doe_dx_pipeline *shader = pipeline(owner,layouts,3);
    doe_dx_commands *work = commands(owner);
    assert(buffer->references == 1);
    assert(doe_dx_commands_dispatch(work,shader,bindings,3,1,1,1,NULL) == DOE_DX_OK);
    assert(work->buffer_count == 1 && buffer->references == 2 && mock.binding_count == 3);
    for (size_t index = 0; index < 3; ++index) {
        assert(mock.bindings[index].buffer == buffer->native);
        assert(mock.bindings[index].offset == bindings[index].offset);
        assert(mock.bindings[index].size == bindings[index].size);
    }
    doe_dx_commands *other = commands(owner);
    assert(doe_dx_commands_dispatch(other,shader,bindings,3,1,1,1,NULL) == DOE_DX_BUSY);
    assert(!other->buffer_count && buffer->references == 2);
    doe_dx_commands_release(other);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(work,NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_buffer_release(buffer);
    doe_dx_pipeline_release(shader);
    doe_dx_commands_release(work);
    assert(mock.buffers == 1 && mock.pipelines == 1 && mock.commands == 1);
    assert(doe_dx_context_destroy(owner) == DOE_DX_BUSY);
    mock.failure = MOCK_NONE;
    mock.completed = owner->next_fence;
    assert(doe_dx_context_poll(owner,0,NULL) == DOE_DX_OK);
    assert(!mock.buffers && !mock.pipelines && !mock.commands);
    finish(owner);
}

static void writable_alias_rules(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *buffer = initialized(owner,128);
    const doe_dx_binding_layout layouts[] = {
        {0,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED},
        {1,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED}
    };
    doe_dx_buffer_binding bindings[] = {{buffer,0,64},{buffer,64,64}};
    doe_dx_pipeline *shader = pipeline(owner,layouts,2);
    doe_dx_commands *work = commands(owner);
    size_t dispatches = mock.dispatches;
    bindings[1].offset = 60;
    assert(doe_dx_commands_dispatch(work,shader,bindings,2,1,1,1,NULL) == DOE_DX_UNSUPPORTED);
    assert(mock.dispatches == dispatches && !work->buffer_count && buffer->references == 1);
    bindings[1].offset = UINT64_MAX;
    assert(doe_dx_commands_dispatch(work,shader,bindings,2,1,1,1,NULL) == DOE_DX_RANGE);
    bindings[1].offset = 128;
    assert(doe_dx_commands_dispatch(work,shader,bindings,2,1,1,1,NULL) == DOE_DX_RANGE);
    bindings[1].offset = 64;
    assert(doe_dx_commands_dispatch(work,shader,bindings,2,1,1,1,NULL) == DOE_DX_OK);
    assert(work->buffer_count == 1 && buffer->references == 2);
    /* Opposite descriptor order preserves the same nonoverlap decision. */
    bindings[0].offset = 64;
    bindings[1].offset = 0;
    assert(doe_dx_commands_dispatch(work,shader,bindings,2,1,1,1,NULL) == DOE_DX_OK);
    assert(work->buffer_count == 1 && buffer->references == 2);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(shader);
    const doe_dx_binding_layout mixed[] = {
        {0,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED},
        {0,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED}
    };
    shader = pipeline(owner,mixed,2);
    work = commands(owner);
    dispatches = mock.dispatches;
    assert(doe_dx_commands_dispatch(work,shader,bindings,2,1,1,1,NULL) == DOE_DX_UNSUPPORTED);
    assert(mock.dispatches == dispatches && !work->buffer_count && buffer->references == 1);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(shader);
    doe_dx_buffer_release(buffer);
    finish(owner);
}

static void unique_budget(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *buffers[DOE_DX_MAX_COMMAND_RESOURCES];
    for (size_t index = 0; index < DOE_DX_MAX_COMMAND_RESOURCES; ++index)
        buffers[index] = initialized(owner,4);
    doe_dx_commands *work = commands(owner);
    for (size_t index = 1; index < DOE_DX_MAX_COMMAND_RESOURCES - 1; ++index)
        assert(doe_dx_commands_copy(work,buffers[0],0,buffers[index],0,4,NULL) == DOE_DX_OK);
    assert(work->buffer_count == 63);
    doe_dx_binding_layout layouts[DOE_DX_MAX_BINDINGS];
    doe_dx_buffer_binding bindings[DOE_DX_MAX_BINDINGS];
    for (uint32_t index = 0; index < DOE_DX_MAX_BINDINGS; ++index) {
        layouts[index] = (doe_dx_binding_layout){index,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
        bindings[index] = (doe_dx_buffer_binding){buffers[63],0,4};
    }
    doe_dx_pipeline *shader = pipeline(owner,layouts,DOE_DX_MAX_BINDINGS);
    assert(doe_dx_commands_dispatch(work,shader,bindings,DOE_DX_MAX_BINDINGS,1,1,1,NULL) == DOE_DX_OK);
    assert(work->buffer_count == 64 && buffers[63]->references == 2);
    assert(mock.binding_count == DOE_DX_MAX_BINDINGS);
    for (size_t index = 0; index < DOE_DX_MAX_COMMAND_RESOURCES; ++index)
        doe_dx_buffer_release(buffers[index]);
    doe_dx_pipeline_release(shader);
    retire(owner,work);
    assert(!mock.buffers && !mock.pipelines && !mock.commands);
    finish(owner);
}

int main(void) {
    read_alias_lifetime();
    writable_alias_rules();
    unique_budget();
    puts("D3D12 buffer alias owner:3 host groups passed; no SDK or shader execution");
    return 0;
}
