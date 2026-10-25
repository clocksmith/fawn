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
    doe_dx_native_buffer **buffers;
    size_t buffer_count, buffer_capacity;
    doe_dx_native_pipeline **pipelines;
    size_t pipeline_count, pipeline_capacity;
    doe_dx_native_texture **textures;
    size_t texture_count, texture_capacity;
    doe_dx_native_timestamps **timestamps;
    size_t timestamp_count, timestamp_capacity;
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
    if (mock.failure == MOCK_NO_HARDWARE && point == MOCK_OPEN)
        return doe_dx_fail(DOE_DX_UNAVAILABLE, -1000, "mock.no_hardware", error);
    if (mock.failure == point)
        return doe_dx_fail(DOE_DX_NATIVE_FAILURE, -100 - (int32_t)point, "mock.injected", error);
    return doe_dx_fail(DOE_DX_OK, 0, "mock", error);
}

doe_dx_caps mock_default_caps(void) {
    /* Programmed call-seam facts only: not an actual SDK query or adapter. */
    doe_dx_caps caps = {0};
    caps.feature_level = 0xb100;
    caps.resource_binding_tier = 2;
    caps.resource_heap_tier = 1;
    caps.gpu_va_bits_per_resource = caps.gpu_va_bits_per_process = 40;
    caps.shader_model = 0x60;
    caps.limits = (doe_dx_limits){
        .max_descriptors=DOE_DX_MAX_BINDINGS, .max_static_samplers=DOE_DX_MAX_STATIC_SAMPLERS,
        .max_cbvs=14, .max_srvs=64, .max_uavs=64, .max_compute_threads=1024,
        .max_compute_size={1024,1024,64}, .max_dispatch_groups={65535,65535,65535},
        .max_compute_shared_bytes=32768, .uniform_offset_alignment=256, .max_uniform_bytes=65536,
        .max_buffer_size=UINT64_C(268435456), .max_buffer_allocation_bytes=UINT64_C(268435456),
        .buffer_allocation_alignment=65536};
    return caps;
}

doe_dx_status doe_dx_native_open(const doe_dx_options *options,
    doe_dx_native_context **out, doe_dx_caps *caps, doe_dx_error *error) {
    mock.opens++;
    mock.open_options = *options;
    doe_dx_status status = outcome(MOCK_OPEN, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_native_context *context = calloc(1, sizeof(*context));
    assert(context);
    *caps = mock_default_caps();
    caps->adapter_ordinal = options->adapter_selection == DOE_DX_SELECT_ORDINAL ||
        options->adapter_selection == DOE_DX_SELECT_HARDWARE ?
        options->adapter_ordinal : DOE_DX_NO_ADAPTER_ORDINAL;
    caps->adapter_selection = options->adapter_selection;
    caps->software_adapter = options->adapter_selection == DOE_DX_SELECT_WARP;
    if (options->adapter_selection == DOE_DX_SELECT_LUID) {
        caps->luid_low = options->adapter_identity.luid_low;
        caps->luid_high = options->adapter_identity.luid_high;
        caps->software_adapter = options->adapter_identity.software_adapter;
    }
    if (mock.override_caps) *caps = mock.reported_caps;
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

doe_dx_status doe_dx_native_pipeline_create_dynamic(doe_dx_native_context *context,
    const doe_dx_dynamic_pipeline_desc *desc,doe_dx_native_pipeline **out,doe_dx_error *error) {
    doe_dx_status status = doe_dx_native_pipeline_create(context,&desc->resources,out,error);
    if (status == DOE_DX_OK) {
        mock.dynamic_sampler_count = desc->sampler_count;
        for (size_t i = 0; i < desc->sampler_count; ++i) mock.dynamic_layouts[i] = desc->samplers[i];
    }
    return status;
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
    free(commands->buffers);
    free(commands->pipelines);
    free(commands->textures);
    free(commands->timestamps);
    free(commands);
}

/* Borrow observation per native use, not a simulated ownership registry. */
static void *observation_capacity(void *old, size_t count, size_t *capacity, size_t element) {
    if (count < *capacity) return old;
    assert(*capacity <= SIZE_MAX / 2 && (*capacity ? *capacity * 2 : 8) <= SIZE_MAX / element);
    size_t next = *capacity ? *capacity * 2 : 8;
    void *fresh = realloc(old,next * element);
    assert(fresh);
    *capacity = next;
    return fresh;
}

static void record_buffer(doe_dx_native_commands *commands, doe_dx_native_buffer *buffer) {
    commands->buffers = observation_capacity(commands->buffers,commands->buffer_count,
        &commands->buffer_capacity,sizeof(*commands->buffers));
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
    commands->textures = observation_capacity(commands->textures,commands->texture_count,
        &commands->texture_capacity,sizeof(*commands->textures));
    commands->textures[commands->texture_count++] = texture;
    texture->recorded++;
}

static void record_resources(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources) {
    commands->pipelines = observation_capacity(commands->pipelines,commands->pipeline_count,
        &commands->pipeline_capacity,sizeof(*commands->pipelines));
    commands->pipelines[commands->pipeline_count++] = pipeline;
    pipeline->recorded++;
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
    mock.dynamic_sampler_count = resources->sampler_count;
    for (size_t i = 0; i < resources->sampler_count; ++i) mock.dynamic_values[i] = resources->samplers[i];
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

doe_dx_status doe_dx_native_dispatch_indirect(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_buffer *arguments, uint64_t offset, doe_dx_error *error) {
    doe_dx_status status = outcome(MOCK_DISPATCH_INDIRECT, error);
    if (status != DOE_DX_OK) return status;
    record_resources(commands, pipeline, resources);
    record_buffer(commands, arguments);
    mock.indirect_arguments = arguments;
    mock.indirect_offset = offset;
    mock.indirect_dispatches++;
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

void doe_dx_native_texture_region_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, doe_dx_texture_region region, bool upload) {
    assert(region.width && region.height);
    doe_dx_native_texture_transfer(commands,buffer,texture,footprint,upload);
    mock.region = region;
}

void doe_dx_native_texture_blit(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_texture_region from, doe_dx_native_texture *destination,
    uint32_t destination_x, uint32_t destination_y) {
    record_texture(commands,source);
    record_texture(commands,destination);
    mock.texture_source = source;
    mock.texture_destination = destination;
    mock.region = from;
    mock.destination_x = destination_x;
    mock.destination_y = destination_y;
    mock.texture_blits++;
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
    const doe_dx_mesh_pipeline_desc *mesh, doe_dx_native_pipeline **out, doe_dx_error *error) {
    const doe_dx_graphics_pipeline_desc *desc = &mesh->graphics;
    assert(desc->vertex_bytecode && desc->vertex_bytecode_size);
    assert(desc->color_format == DOE_DX_RGBA8_UNORM || desc->color_format == DOE_DX_BGRA8_UNORM);
    doe_dx_status status = outcome(MOCK_GRAPHICS, error);
    if (status != DOE_DX_OK) return status;
    return doe_dx_native_pipeline_create(context, &desc->fragment, out, error);
}

doe_dx_status doe_dx_native_draw(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_resources *resources,
    doe_dx_native_texture *color, const doe_dx_native_geometry *geometry,
    const doe_dx_mesh_draw_desc *mesh, doe_dx_error *error) {
    doe_dx_status status = outcome(MOCK_DRAW, error);
    if (status != DOE_DX_OK) return status;
    record_resources(commands, pipeline, resources);
    record_texture(commands, color);
    mock.draw_color = color;
    mock.draw = mesh->draw;
    mock.geometry = *geometry;
    mock.vertex_count = mesh->vertex_count;
    mock.indexed = mesh->indices != NULL;
    if (mesh->indices) {
        mock.indices = *mesh->indices;
        record_buffer(commands, geometry->indices.buffer);
    }
    for (size_t index = 0; index < mesh->vertex_count; index++)
        record_buffer(commands, geometry->vertices[index].buffer);
    if (geometry->depth) record_texture(commands, geometry->depth);
    mock.clear_depth = mesh->clear_depth;
    mock.depth_clear = mesh->depth_clear;
    mock.draws++;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_timestamps_create(doe_dx_native_context *context, doe_dx_query_kind kind,uint32_t count,
    doe_dx_native_timestamps **out, doe_dx_error *error) {
    assert(context && count && count <= DOE_DX_MAX_QUERIES);
    doe_dx_status status = outcome(MOCK_TIMESTAMPS, error);
    if (status != DOE_DX_OK) return status;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    (void)kind;
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
    commands->timestamps = observation_capacity(commands->timestamps,commands->timestamp_count,
        &commands->timestamp_capacity,sizeof(*commands->timestamps));
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

doe_dx_status doe_dx_native_render_create(doe_dx_native_context *context,
    const doe_dx_render_pipeline_desc *desc,doe_dx_native_pipeline **out,doe_dx_error *error) {
    assert(context && desc);
    mock.last_render_pipeline=*desc;
    doe_dx_status status=outcome(MOCK_GRAPHICS,error);
    if (status!=DOE_DX_OK) return status;
    *out=calloc(1,sizeof(**out));
    assert(*out);
    mock.pipelines++;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_native_render(doe_dx_native_commands *commands,doe_dx_native_texture *color,
    bool clear,const float clear_color[4],bool discard,const doe_dx_native_render_depth *depth,
    const doe_dx_native_render_draw *draws,
    size_t count,doe_dx_error *error) {
    (void)clear_color;
    doe_dx_status status=outcome(MOCK_DRAW,error);
    if (status!=DOE_DX_OK) return status;
    if (color) record_texture(commands,color);
    for (size_t i=0;i<count;++i) {
        if (draws[i].query) { record_timestamps(commands,draws[i].query); continue; }
        for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage)
            record_resources(commands,draws[i].pipeline,&draws[i].stages[stage]);
        for (size_t slot=0;slot<draws[i].vertex_buffer_count;++slot)
            if (draws[i].vertices[slot].buffer) record_buffer(commands,draws[i].vertices[slot].buffer);
        if (draws[i].indirect.buffer) record_buffer(commands,draws[i].indirect.buffer);
        if (draws[i].indices.buffer) record_buffer(commands,draws[i].indices.buffer);
        mock.last_render_draw=draws[i];
    }
    if (depth) {
        assert(depth->texture);
        record_texture(commands,depth->texture);
        mock.render_depth=*depth;
    } else mock.render_depth=(doe_dx_native_render_depth){0};
    mock.render_passes++;
    mock.render_draws+=count;
    mock.render_clear=clear;
    mock.render_discard=discard;
    return DOE_DX_OK;
}
