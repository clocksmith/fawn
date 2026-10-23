/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

/* No shader interpreter or buffer-copy execution is present. These substitutes
 * only observe actual owner calls and enforce native object lifetime edges. */
struct doe_dx_native_context { unsigned tag; };
struct doe_dx_native_buffer { unsigned recorded; };
struct doe_dx_native_pipeline { unsigned recorded; };
struct doe_dx_native_texture { unsigned recorded; };
struct doe_dx_native_timestamps { unsigned recorded; };
struct doe_dx_native_commands {
    uint64_t execution;
    doe_dx_native_buffer *buffers[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t buffer_count;
    doe_dx_native_pipeline *pipelines[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t pipeline_count;
    doe_dx_native_texture *textures[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t texture_count;
    doe_dx_native_timestamps *timestamps[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t timestamp_count;
};
mock_state mock;

void mock_reset(void) {
    assert(!mock.contexts && !mock.buffers && !mock.pipelines && !mock.commands && !mock.textures && !mock.timestamps);
    assert(!mock.allocations);
    memset(&mock, 0, sizeof(mock));
    mock.frequency = UINT64_C(1000000);
}

void *doe_dx_test_calloc(size_t count, size_t size) {
    mock.allocation_attempts++;
    if (mock.allocation_attempts == mock.allocation_failure) return NULL;
    void *pointer = calloc(count, size);
    if (pointer) mock.allocations++;
    return pointer;
}

void doe_dx_test_free(void *pointer) {
    if (pointer) {
        assert(mock.allocations);
        mock.allocations--;
        free(pointer);
    }
}

static doe_dx_status outcome(mock_failure point, doe_dx_error *error) {
    if (mock.lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, -200, "mock.device_removed", error);
    if (mock.failure == point)
        return doe_dx_fail(DOE_DX_NATIVE_FAILURE, -100 - (int32_t)point, "mock.injected", error);
    return doe_dx_fail(DOE_DX_OK, 0, "mock", error);
}

doe_dx_status doe_dx_native_open(const doe_dx_options *options,
    doe_dx_native_context **out, doe_dx_caps *caps, doe_dx_error *error) {
    doe_dx_status status = outcome(MOCK_OPEN, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_native_context *context = calloc(1, sizeof(*context));
    assert(context);
    *caps = (doe_dx_caps){0};
    caps->adapter_ordinal = options->adapter_ordinal;
    strcpy(caps->name, "host call-control, not an adapter");
    *out = context;
    mock.contexts++;
    return DOE_DX_OK;
}

void doe_dx_native_close(doe_dx_native_context *context) {
    assert(context && mock.contexts);
    mock.contexts--;
    free(context);
}

doe_dx_status doe_dx_native_buffer_create(doe_dx_native_context *context,
    uint64_t size, doe_dx_memory memory, doe_dx_native_buffer **out, doe_dx_error *error) {
    assert(context && size);
    (void)memory;
    doe_dx_status status = outcome(MOCK_BUFFER, error);
    if (status != DOE_DX_OK) return status;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    mock.buffers++;
    return DOE_DX_OK;
}

void doe_dx_native_buffer_destroy(doe_dx_native_buffer *buffer) {
    assert(buffer && !buffer->recorded && mock.buffers);
    mock.buffers--;
    free(buffer);
}

doe_dx_status doe_dx_native_write(doe_dx_native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error) {
    assert(buffer && bytes && size && !buffer->recorded);
    (void)offset;
    return outcome(MOCK_WRITE, error);
}

doe_dx_status doe_dx_native_read(doe_dx_native_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error) {
    assert(buffer && bytes && size && !buffer->recorded);
    (void)offset;
    doe_dx_status status = outcome(MOCK_READ, error);
    if (status == DOE_DX_OK) memset(bytes, 0x5a, size);
    return status;
}

doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error) {
    assert(context && desc->buffers.bytecode && desc->buffers.bytecode_size);
    doe_dx_status status = outcome(MOCK_PIPELINE, error);
    if (status != DOE_DX_OK) return status;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    mock.pipeline_desc = *desc;
    for (size_t index = 0; index < desc->texture_count; index++)
        mock.texture_layouts[index] = desc->textures[index];
    for (size_t index = 0; index < desc->sampler_count; index++)
        mock.samplers[index] = desc->samplers[index];
    mock.pipelines++;
    return DOE_DX_OK;
}

void doe_dx_native_pipeline_destroy(doe_dx_native_pipeline *pipeline) {
    assert(pipeline && !pipeline->recorded && mock.pipelines);
    mock.pipelines--;
    free(pipeline);
}

doe_dx_status doe_dx_native_commands_create(doe_dx_native_context *context,
    doe_dx_native_commands **out, doe_dx_error *error) {
    assert(context);
    doe_dx_status status = outcome(MOCK_COMMANDS, error);
    if (status != DOE_DX_OK) return status;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    mock.commands++;
    return DOE_DX_OK;
}

void doe_dx_native_commands_destroy(doe_dx_native_commands *commands) {
    if (!commands) return;
    assert(!commands->execution || mock.completed >= commands->execution || mock.lost);
    for (size_t index = 0; index < commands->buffer_count; index++)
        commands->buffers[index]->recorded--;
    for (size_t index = 0; index < commands->pipeline_count; index++)
        commands->pipelines[index]->recorded--;
    for (size_t index = 0; index < commands->texture_count; index++)
        commands->textures[index]->recorded--;
    for (size_t index = 0; index < commands->timestamp_count; index++)
        commands->timestamps[index]->recorded--;
    assert(mock.commands);
    mock.commands--;
    free(commands);
}

static void record_buffer(doe_dx_native_commands *commands, doe_dx_native_buffer *buffer) {
    for (size_t index = 0; index < commands->buffer_count; index++)
        if (commands->buffers[index] == buffer) return;
    assert(commands->buffer_count < DOE_DX_MAX_COMMAND_RESOURCES);
    commands->buffers[commands->buffer_count++] = buffer;
    buffer->recorded++;
}

void doe_dx_native_copy(doe_dx_native_commands *commands, doe_dx_native_buffer *source,
    uint64_t source_offset, doe_dx_native_buffer *destination,
    uint64_t destination_offset, uint64_t size) {
    record_buffer(commands, source);
    record_buffer(commands, destination);
    mock.copy_source = source;
    mock.copy_destination = destination;
    mock.source_offset = source_offset;
    mock.destination_offset = destination_offset;
    mock.copy_size = size;
    mock.copies++;
}

static void record_texture(doe_dx_native_commands *commands, doe_dx_native_texture *texture) {
    for (size_t index = 0; index < commands->texture_count; index++)
        if (commands->textures[index] == texture) return;
    assert(commands->texture_count < DOE_DX_MAX_COMMAND_RESOURCES);
    commands->textures[commands->texture_count++] = texture;
    texture->recorded++;
}

static void record_resources(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources) {
    bool found = false;
    for (size_t index = 0; index < commands->pipeline_count; index++)
        if (commands->pipelines[index] == pipeline) found = true;
    if (!found) {
        assert(commands->pipeline_count < DOE_DX_MAX_COMMAND_RESOURCES);
        commands->pipelines[commands->pipeline_count++] = pipeline;
        pipeline->recorded++;
    }
    for (size_t index = 0; index < resources->buffer_count; index++) {
        record_buffer(commands, resources->buffers[index].buffer);
        mock.bindings[index] = resources->buffers[index];
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        record_texture(commands, resources->textures[index]);
        mock.texture_bindings[index] = resources->textures[index];
    }
    mock.binding_count = resources->buffer_count;
    mock.texture_count = resources->texture_count;
}

doe_dx_status doe_dx_native_dispatch(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    doe_dx_status status = outcome(MOCK_DISPATCH, error);
    if (status != DOE_DX_OK) return status;
    record_resources(commands, pipeline, resources);
    mock.x = x;
    mock.y = y;
    mock.z = z;
    mock.dispatches++;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_finish(doe_dx_native_commands *commands, doe_dx_error *error) {
    assert(commands && !commands->execution);
    mock.finishes++;
    return outcome(MOCK_FINISH, error);
}

void doe_dx_native_execute(doe_dx_native_context *context, doe_dx_native_commands *commands) {
    assert(context && !commands->execution);
    mock.executes++;
    commands->execution = mock.executes;
}

doe_dx_status doe_dx_native_signal(doe_dx_native_context *context, uint64_t value,
    doe_dx_error *error) {
    assert(context && value && value < UINT64_MAX);
    mock.signals++;
    mock.last_signal = value;
    return outcome(MOCK_SIGNAL, error);
}

doe_dx_status doe_dx_native_completed(doe_dx_native_context *context, uint64_t *out,
    doe_dx_error *error) {
    assert(context);
    doe_dx_status status = outcome(MOCK_COMPLETED, error);
    if (status == DOE_DX_OK) *out = mock.completed;
    return status;
}

doe_dx_status doe_dx_native_wait(doe_dx_native_context *context, uint64_t value,
    uint32_t wait_ms, doe_dx_error *error) {
    assert(context && wait_ms && value);
    mock.waits++;
    doe_dx_status status = outcome(MOCK_WAIT, error);
    if (status != DOE_DX_OK) return status;
    return mock.completed >= value ? DOE_DX_OK : DOE_DX_PENDING;
}

doe_dx_status doe_dx_native_texture_create(doe_dx_native_context *context,
    const doe_dx_texture_desc *desc, doe_dx_native_texture **out, doe_dx_error *error) {
    assert(context && desc->width && desc->height);
    doe_dx_status status = outcome(MOCK_TEXTURE, error);
    if (status != DOE_DX_OK) return status;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    mock.textures++;
    return DOE_DX_OK;
}

void doe_dx_native_texture_destroy(doe_dx_native_texture *texture) {
    assert(texture && !texture->recorded && mock.textures);
    mock.textures--;
    free(texture);
}

void doe_dx_native_texture_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, bool upload) {
    record_buffer(commands, buffer);
    record_texture(commands, texture);
    mock.transfer_texture = texture;
    mock.transfer_buffer = buffer;
    mock.footprint = *footprint;
    mock.upload = upload;
    mock.texture_transfers++;
}

void doe_dx_native_texture_copy(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_native_texture *destination) {
    record_texture(commands, source);
    record_texture(commands, destination);
    mock.texture_source = source;
    mock.texture_destination = destination;
    mock.texture_copies++;
}

doe_dx_status doe_dx_native_graphics_create(doe_dx_native_context *context,
    const doe_dx_graphics_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error) {
    assert(desc->vertex_bytecode && desc->vertex_bytecode_size && desc->color_format == DOE_DX_RGBA8_UNORM);
    doe_dx_status status = outcome(MOCK_GRAPHICS, error);
    if (status != DOE_DX_OK) return status;
    return doe_dx_native_pipeline_create(context, &desc->fragment, out, error);
}

doe_dx_status doe_dx_native_draw(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_texture *color, const doe_dx_draw_desc *draw, doe_dx_error *error) {
    doe_dx_status status = outcome(MOCK_DRAW, error);
    if (status != DOE_DX_OK) return status;
    record_resources(commands, pipeline, resources);
    record_texture(commands, color);
    mock.draw_color = color;
    mock.draw = *draw;
    mock.draws++;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_timestamps_create(doe_dx_native_context *context, uint32_t count,
    doe_dx_native_timestamps **out, doe_dx_error *error) {
    assert(context && count && count <= 64);
    doe_dx_status status = outcome(MOCK_TIMESTAMPS, error);
    if (status != DOE_DX_OK) return status;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    mock.timestamps++;
    return DOE_DX_OK;
}

void doe_dx_native_timestamps_destroy(doe_dx_native_timestamps *timestamps) {
    assert(timestamps && !timestamps->recorded && mock.timestamps);
    mock.timestamps--;
    free(timestamps);
}

doe_dx_status doe_dx_native_timestamp_frequency(doe_dx_native_context *context,
    uint64_t *out, doe_dx_error *error) {
    assert(context);
    doe_dx_status status = outcome(MOCK_FREQUENCY, error);
    if (status != DOE_DX_OK) return status;
    *out = mock.frequency;
    return DOE_DX_OK;
}

static void record_timestamps(doe_dx_native_commands *commands, doe_dx_native_timestamps *timestamps) {
    for (size_t index = 0; index < commands->timestamp_count; index++)
        if (commands->timestamps[index] == timestamps) return;
    assert(commands->timestamp_count < DOE_DX_MAX_COMMAND_RESOURCES);
    commands->timestamps[commands->timestamp_count++] = timestamps;
    timestamps->recorded++;
}

void doe_dx_native_timestamp(doe_dx_native_commands *commands,
    doe_dx_native_timestamps *timestamps, uint32_t index) {
    record_timestamps(commands, timestamps);
    mock.timestamp_heap = timestamps;
    mock.timestamp_index = index;
    mock.timestamp_writes++;
}

void doe_dx_native_resolve_timestamps(doe_dx_native_commands *commands,
    doe_dx_native_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_dx_native_buffer *destination, uint64_t offset) {
    record_timestamps(commands, timestamps);
    record_buffer(commands, destination);
    mock.timestamp_heap = timestamps;
    mock.resolve_first = first;
    mock.resolve_count = count;
    mock.resolve_offset = offset;
    mock.resolve_buffer = destination;
    mock.timestamp_resolves++;
}
