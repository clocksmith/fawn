/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *open_context(void) {
    doe_dx_context *owner = NULL;
    assert(doe_dx_context_create(NULL,&owner,NULL) == DOE_DX_OK);
    return owner;
}
static doe_dx_commands *recording(doe_dx_context *owner) {
    doe_dx_commands *commands = NULL;
    assert(doe_dx_commands_create(owner,&commands,NULL) == DOE_DX_OK);
    return commands;
}
static void finish(doe_dx_context *owner) {
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}
static void buffer_growth(size_t count) {
    doe_dx_context *owner = open_context();
    doe_dx_buffer *buffers[129];
    for (size_t index = 0; index < count; ++index) {
        buffers[index] = NULL;
        assert(doe_dx_buffer_create(owner,4,index ? DOE_DX_DEFAULT : DOE_DX_UPLOAD,&buffers[index],NULL) == DOE_DX_OK);
    }
    doe_dx_commands *commands = recording(owner);
    for (size_t index = 1; index < count; ++index)
        assert(doe_dx_commands_copy(commands,buffers[0],0,buffers[index],0,4,NULL) == DOE_DX_OK);
    assert(commands->buffer_count == count);
    for (size_t index = 0; index < count; ++index) {
        assert(buffers[index]->owner_index == index);
        assert(doe_dx_find_buffer(commands,buffers[index]) == &commands->buffers[index]);
    }
    assert(!buffers[1]->initialized && doe_dx_find_buffer(commands,buffers[1])->initialized);
    assert(doe_dx_commands_copy(commands,buffers[1],0,buffers[count-1],0,4,NULL) == DOE_DX_OK);
    for (size_t index = 0; index < count; ++index) doe_dx_buffer_release(buffers[index]);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(commands);
    assert(mock.buffers == count && doe_dx_context_destroy(owner) == DOE_DX_BUSY);
    mock.failure = MOCK_NONE;
    mock.completed = owner->next_fence;
    assert(doe_dx_context_poll(owner,0,NULL) == DOE_DX_OK);
    assert(!mock.buffers && !mock.commands);
    finish(owner);
}

static void pipeline_records(void) {
    doe_dx_context *owner = open_context();
    const unsigned char bytecode = 1;
    const doe_dx_pipeline_desc desc = {&bytecode,1,NULL,0};
    doe_dx_pipeline *pipeline = NULL;
    assert(doe_dx_pipeline_create(owner,&desc,&pipeline,NULL) == DOE_DX_OK);
    doe_dx_commands *commands = recording(owner);
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_commands_dispatch(commands,pipeline,NULL,0,1,1,1,NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(!commands->pipeline_count && pipeline->references == 1 && !mock.dispatches);
    mock.allocation_failure = 0;
    for (size_t index = 0; index < 65537; ++index)
        assert(doe_dx_commands_dispatch(commands,pipeline,NULL,0,1,1,1,NULL) == DOE_DX_OK);
    assert(commands->pipeline_count == 65537 && pipeline->references == 65538 && mock.dispatches == 65537);
    doe_dx_pipeline_release(pipeline);
    doe_dx_commands_release(commands);
    assert(!mock.pipelines);
    finish(owner);
}

static void partial_reservation(void) {
    for (size_t fault = 1; fault <= 4; ++fault) {
        doe_dx_context *owner = open_context();
        doe_dx_commands *commands = recording(owner);
        mock.allocation_failure = mock.allocation_attempts + fault;
        const doe_dx_reservation all = {.buffers=1,.textures=1,.timestamps=1,.pipelines=1};
        doe_dx_error error = {0};
        assert(doe_dx_reserve_uses(commands,all,&error) == DOE_DX_OUT_OF_MEMORY);
        assert(error.status == DOE_DX_OUT_OF_MEMORY && error.operation);
        assert(!commands->buffer_count && !commands->texture_count && !commands->timestamp_count && !commands->pipeline_count);
        assert(!mock.copies && !mock.draws && !mock.dispatches && !mock.timestamp_writes);
        mock.allocation_failure = 0;
        assert(doe_dx_reserve_uses(commands,all,NULL) == DOE_DX_OK);
        doe_dx_commands_release(commands);
        finish(owner);
    }
    doe_dx_context *owner = open_context();
    doe_dx_commands *commands = recording(owner);
    size_t attempts = mock.allocation_attempts;
    assert(doe_dx_reserve_uses(commands,(doe_dx_reservation){.buffers=SIZE_MAX},NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(mock.allocation_attempts == attempts && !commands->buffers && !commands->buffer_count);
    doe_dx_commands_release(commands);
    finish(owner);
}

static void recording_reserve_failures(void) {
    for (size_t fault = 1; fault <= 3; ++fault) {
        doe_dx_context *owner = open_context();
        doe_dx_buffer *upload = NULL, *buffer = NULL;
        doe_dx_texture *texture = NULL;
        assert(doe_dx_buffer_create(owner,4,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
        assert(doe_dx_buffer_create(owner,4,DOE_DX_DEFAULT,&buffer,NULL) == DOE_DX_OK);
        const doe_dx_texture_desc image = {1,1,DOE_DX_R32_UINT,DOE_DX_TEXTURE_COPY_DST|DOE_DX_TEXTURE_SAMPLED};
        assert(doe_dx_texture_create(owner,&image,&texture,NULL) == DOE_DX_OK);
        doe_dx_commands *init = recording(owner);
        assert(doe_dx_commands_copy(init,upload,0,buffer,0,4,NULL) == DOE_DX_OK);
        assert(doe_dx_commands_upload_texture(init,upload,(doe_dx_texture_layout){0},texture,NULL) == DOE_DX_OK);
        assert(doe_dx_commands_submit(init,NULL) == DOE_DX_OK);
        doe_dx_commands_release(init);
        mock.completed = owner->next_fence;
        assert(doe_dx_context_poll(owner,0,NULL) == DOE_DX_OK);
        doe_dx_buffer_release(upload);
        const unsigned char code = 1;
        const doe_dx_binding_layout buffer_layout = {0,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
        const doe_dx_texture_binding_layout texture_layout = {0,1,DOE_DX_R32_UINT,DOE_DX_READ_ONLY, false};
        const doe_dx_resource_pipeline_desc desc = {{&code,1,&buffer_layout,1},&texture_layout,1,NULL,0};
        doe_dx_pipeline *pipeline = NULL;
        assert(doe_dx_pipeline_create_resources(owner,&desc,&pipeline,NULL) == DOE_DX_OK);
        doe_dx_commands *commands = recording(owner);
        const doe_dx_buffer_binding binding = {buffer,0,4};
        const doe_dx_compute_resources resources = {&binding,1,&texture,1};
        mock.allocation_failure = mock.allocation_attempts + fault;
        assert(doe_dx_commands_dispatch_resources(commands,pipeline,&resources,1,1,1,NULL) == DOE_DX_OUT_OF_MEMORY);
        assert(!commands->buffer_count && !commands->texture_count && !commands->pipeline_count);
        assert(!buffer->owner && !texture->owner && buffer->references == 1 && texture->references == 1);
        assert(pipeline->references == 1 && !mock.dispatches);
        mock.allocation_failure = 0;
        assert(doe_dx_commands_dispatch_resources(commands,pipeline,&resources,1,1,1,NULL) == DOE_DX_OK);
        doe_dx_buffer_release(buffer);
        doe_dx_texture_release(texture);
        doe_dx_pipeline_release(pipeline);
        doe_dx_commands_release(commands);
        finish(owner);
    }
    doe_dx_context *owner = open_context();
    doe_dx_commands *commands = recording(owner);
    doe_dx_timestamps *query = NULL;
    assert(doe_dx_timestamps_create(owner,1,&query,NULL) == DOE_DX_OK);
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_commands_timestamp(commands,query,0,NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(!commands->timestamp_count && !query->owner && !query->initialized && !mock.timestamp_writes);
    mock.allocation_failure = 0;
    assert(doe_dx_commands_timestamp(commands,query,0,NULL) == DOE_DX_OK);
    doe_dx_timestamps_release(query);
    doe_dx_commands_release(commands);
    finish(owner);
}

static void texture_query_indices(void) {
    doe_dx_context *owner = open_context();
    doe_dx_commands *commands = recording(owner);
    doe_dx_buffer *upload = NULL;
    assert(doe_dx_buffer_create(owner,4,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    doe_dx_texture *textures[129];
    doe_dx_timestamps *queries[129];
    const doe_dx_texture_desc desc = {1,1,DOE_DX_R32_UINT,DOE_DX_TEXTURE_COPY_SRC|DOE_DX_TEXTURE_COPY_DST};
    for (size_t index = 0; index < 129; ++index) {
        textures[index] = NULL;
        queries[index] = NULL;
        assert(doe_dx_texture_create(owner,&desc,&textures[index],NULL) == DOE_DX_OK);
        assert(doe_dx_timestamps_create(owner,1,&queries[index],NULL) == DOE_DX_OK);
        assert(doe_dx_commands_upload_texture(commands,upload,(doe_dx_texture_layout){0},textures[index],NULL) == DOE_DX_OK);
        assert(doe_dx_commands_timestamp(commands,queries[index],0,NULL) == DOE_DX_OK);
    }
    assert(commands->texture_count == 129 && commands->timestamp_count == 129);
    for (size_t index = 0; index < 129; ++index) {
        assert(textures[index]->owner_index == index && queries[index]->owner_index == index);
        assert(doe_dx_find_texture(commands,textures[index]) == &commands->textures[index]);
    }
    assert(doe_dx_commands_copy_texture(commands,textures[0],textures[128],NULL) == DOE_DX_OK);
    doe_dx_buffer *readback = NULL;
    assert(doe_dx_buffer_create(owner,8,DOE_DX_READBACK,&readback,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_resolve_timestamps(commands,queries[0],0,1,readback,0,NULL) == DOE_DX_OK);
    for (size_t index = 0; index < 129; ++index) {
        doe_dx_texture_release(textures[index]);
        doe_dx_timestamps_release(queries[index]);
    }
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    assert(doe_dx_commands_submit(commands,NULL) == DOE_DX_OK);
    doe_dx_commands_release(commands);
    mock.completed = owner->next_fence;
    assert(doe_dx_context_poll(owner,0,NULL) == DOE_DX_OK);
    assert(!mock.textures && !mock.timestamps && !mock.buffers && !mock.commands);
    finish(owner);
}
static void immediate_snapshots(void) {
    doe_dx_context *owner=open_context();
    const unsigned char code=1;
    doe_dx_dynamic_pipeline_desc desc={.resources={.buffers={&code,1,NULL,0}},
        .immediate_bytes=8,.immediate_required_bytes=UINT64_C(255)};
    doe_dx_pipeline *pipeline=NULL;
    desc.immediate_bytes=65;
    assert(doe_dx_pipeline_create_dynamic(owner,&desc,&pipeline,NULL)==DOE_DX_INVALID_ARGUMENT && !pipeline);
    desc.immediate_bytes=8;
    assert(doe_dx_pipeline_create_dynamic(owner,&desc,&pipeline,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=recording(owner);
    doe_dx_dynamic_resources values={.immediate_words={123,456}};
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_UNINITIALIZED);
    assert(!mock.dispatches && !commands->pipeline_count);
    values.immediate_initialized_bytes=UINT64_C(255);
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_OK);
    values.immediate_words[0]=789;
    assert(mock.immediate_words[0]==123 && mock.immediate_words[1]==456);
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_OK);
    assert(mock.immediate_words[0]==789);
    doe_dx_pipeline_release(pipeline);
    doe_dx_commands_release(commands);
    finish(owner);
}
static void raw_logical_tail(void) {
    doe_dx_context *owner=open_context();
    const unsigned char code=1;
    const doe_dx_binding_layout raw={.shader_register=3,.register_space=2,.access=DOE_DX_READ_WRITE,.view=DOE_DX_RAW};
    uint32_t index=4;
    doe_dx_dynamic_pipeline_desc desc={.resources={.buffers={&code,1,&raw,1}},
        .logical_size_count=5,.logical_size_indices=&index};
    doe_dx_pipeline *pipeline=NULL;
    index=5;
    assert(doe_dx_pipeline_create_dynamic(owner,&desc,&pipeline,NULL)==DOE_DX_INVALID_ARGUMENT && !pipeline);
    index=4;
    assert(doe_dx_pipeline_create_dynamic(owner,&desc,&pipeline,NULL)==DOE_DX_OK);
    doe_dx_buffer *upload=NULL,*buffer=NULL,*short_buffer=NULL;
    assert(doe_dx_buffer_create(owner,4,DOE_DX_UPLOAD,&upload,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(owner,4,DOE_DX_DEFAULT,&buffer,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(owner,2,DOE_DX_DEFAULT,&short_buffer,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=recording(owner);
    assert(doe_dx_commands_copy(commands,upload,0,buffer,0,4,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_copy_bytes(commands,upload,0,short_buffer,0,2,NULL)==DOE_DX_OK);
    doe_dx_buffer_binding binding={short_buffer,0,2};
    doe_dx_dynamic_resources values={.resources={.buffers=&binding,.buffer_count=1}};
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_RANGE);
    assert(!mock.dispatches && !commands->pipeline_count);
    binding.buffer=buffer;
    assert(doe_dx_commands_dispatch_dynamic(commands,pipeline,&values,1,1,1,NULL)==DOE_DX_OK);
    assert(mock.logical_size_count==5 && mock.logical_size_indices[0]==4 && mock.bindings[0].size==2);
    doe_dx_buffer_release(upload); doe_dx_buffer_release(buffer); doe_dx_buffer_release(short_buffer);
    doe_dx_pipeline_release(pipeline);
    mock.failure=MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL)==DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(commands);
    assert(mock.buffers==3 && mock.pipelines==1);
    mock.failure=MOCK_NONE; mock.completed=owner->next_fence;
    assert(doe_dx_context_poll(owner,0,NULL)==DOE_DX_OK);
    assert(!mock.buffers && !mock.pipelines);
    finish(owner);
}
int main(void) {
    raw_logical_tail();
    immediate_snapshots();
    buffer_growth(65);
    buffer_growth(129);
    pipeline_records();
    partial_reservation();
    texture_query_indices();
    recording_reserve_failures();
    puts("D3D12 dynamic command owner:8 host groups passed; no SDK or GPU execution");
    return 0;
}
