/* SPDX-License-Identifier: MIT
 * Owned fault/call-boundary control. No shader interpretation or GPU simulation. */
#include "mock_native.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { MOCK_KIND_DEVICE, MOCK_KIND_QUEUE, MOCK_KIND_BUFFER, MOCK_KIND_PIPELINE,
    MOCK_KIND_COMMAND, MOCK_KIND_TEXTURE, MOCK_KIND_VIEW, MOCK_KIND_SAMPLER, MOCK_KIND_RENDER_PIPELINE, MOCK_KIND_DEPTH_STATE, MOCK_KIND_TIMESTAMPS, MOCK_KIND_EVENT };
struct mock_object {
    unsigned kind;
    size_t length;
    unsigned char *bytes;
    size_t status;
    int64_t code;
};

static doe_mtl_status failure(doe_mtl_error *error) {
    doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "injected native failure", error);
    if (error != NULL) {
        error->native_code = -71;
        strcpy(error->message, "owned fault boundary");
    }
    return DOE_MTL_NATIVE_FAILURE;
}

static mock_object *object_create(mock_native *mock, unsigned kind) {
    mock_object *object = calloc(1, sizeof(*object));
    assert(object != NULL);
    object->kind = kind;
    mock->live++;
    mock->created++;
    return object;
}

static void close_driver(void *userdata) {
    mock_native *mock = userdata;
    assert(mock->live == 0);
    mock->closes++;
}

static void release_object(void *userdata, void *handle) {
    mock_native *mock = userdata;
    mock_object *object = handle;
    assert(object != NULL && mock->live > 0);
    if (object->kind == MOCK_KIND_COMMAND)
        assert(object->status != 2 && object->status != 3);
    mock->released++;
    mock->releases_by_kind[object->kind]++;
    mock->live--;
    free(object->bytes);
    free(object);
}

static doe_mtl_status device_create(void *userdata, void **out,
    doe_mtl_caps *caps, doe_mtl_error *error) {
    mock_native *mock = userdata;
    doe_mtl_caps result = {0};
    if (mock->fail == MOCK_DEVICE) return failure(error);
    result.max_buffer_length = mock->buffer_limit ? mock->buffer_limit : 4096;
    result.max_threadgroup_memory_length = 32768;
    result.max_threads_per_workgroup = 1024;
    result.max_threads_per_dimension[0] = 1024;
    result.max_threads_per_dimension[1] = 1024;
    result.max_threads_per_dimension[2] = 64;
    result.max_texture_1d = 16384;
    result.max_texture_2d = 16384;
    result.max_texture_3d = 2048;
    result.max_texture_layers = 2048;
    result.bc_texture_compression = true;
    result.sample_count_mask = 31;
    result.depth24_stencil8 = true;
    result.separate_depth_stencil = true;
    result.depth_resolve = true;
    result.stencil_resolve = true;
    result.store_and_resolve = true;
    result.multisample_array = true;
    result.max_visibility_offset = 65528;
    result.counting_visibility = true;
    result.timestamp_stage = true;
    result.timestamp_period_ns = 1.0f;
    result.timestamp_blit = true;
    strcpy(result.device_name, "test call ledger, not a GPU");
    *out = object_create(mock, MOCK_KIND_DEVICE);
    *caps = result;
    return doe_mtl_fail(DOE_MTL_OK, "mock device", error);
}

static doe_mtl_status queue_create(void *userdata, void *device, void **out,
    doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(device != NULL);
    if (mock->fail == MOCK_QUEUE) return failure(error);
    *out = object_create(mock, MOCK_KIND_QUEUE);
    return doe_mtl_fail(DOE_MTL_OK, "mock queue", error);
}

static doe_mtl_status buffer_create(void *userdata, void *device, size_t length,
    doe_mtl_memory memory, void **out, doe_mtl_error *error) {
    mock_native *mock = userdata;
    mock_object *buffer;
    assert(device != NULL && length > 0);
    if (mock->fail == MOCK_BUFFER) return failure(error);
    buffer = object_create(mock, MOCK_KIND_BUFFER);
    buffer->length = length;
    if (memory == DOE_MTL_SHARED) {
        buffer->bytes = malloc(length);
        assert(buffer->bytes != NULL);
        memset(buffer->bytes, 0xa5, length);
    }
    *out = buffer;
    return doe_mtl_fail(DOE_MTL_OK, "mock buffer", error);
}

static void *buffer_contents(void *userdata, void *handle) {
    mock_native *mock = userdata;
    mock_object *buffer = handle;
    assert(buffer->kind == MOCK_KIND_BUFFER);
    ++mock->content_queries;
    if (mock->fail == MOCK_CONTENTS) return NULL;
    return buffer->bytes;
}

static doe_mtl_status pipeline_create(void *userdata, void *device,
    const doe_mtl_pipeline_desc *desc, void **out, size_t *max_threads,
    doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(device != NULL && desc != NULL);
    ++mock->pipeline_calls;
    if (mock->fail == MOCK_PIPELINE || mock->pipeline_calls == mock->fail_pipeline_at) {
        doe_mtl_status status = failure(error);
        if (mock->pipeline_failure != DOE_MTL_OK) {
            status = mock->pipeline_failure;
            if (error) error->status = status;
        }
        return status;
    }
    assert(strlen(desc->entrypoint) < sizeof(mock->entrypoint));
    strcpy(mock->entrypoint, desc->entrypoint);
    *out = object_create(mock, MOCK_KIND_PIPELINE);
    *max_threads = mock->pipeline_max_threads;
    return doe_mtl_fail(DOE_MTL_OK, "mock pipeline", error);
}

static doe_mtl_status command_create(void *userdata, void *queue, void **out,
    doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(queue != NULL);
    if (mock->fail == MOCK_COMMAND) return failure(error);
    mock->last_command = object_create(mock, MOCK_KIND_COMMAND);
    *out = mock->last_command;
    return doe_mtl_fail(DOE_MTL_OK, "mock command", error);
}

static doe_mtl_status copy_buffer(void *userdata, void *command, void *source,
    size_t source_offset, void *destination, size_t destination_offset,
    size_t length, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && source != NULL && destination != NULL);
    if (mock->fail == MOCK_COPY) return failure(error);
    mock->copies++;
    mock->copy_sequence = ++mock->operation_sequence;
    mock->last_source = source;
    mock->last_destination = destination;
    mock->last_source_offset = source_offset;
    mock->last_destination_offset = destination_offset;
    mock->last_length = length;
    return doe_mtl_fail(DOE_MTL_OK, "mock copy", error);
}

static doe_mtl_status fill_buffer(void *userdata, void *command, void *buffer,
    size_t offset, size_t length, uint8_t value, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && buffer != NULL);
    if (mock->fail == MOCK_FILL) return failure(error);
    mock->fills++;
    mock->last_destination = buffer;
    mock->last_destination_offset = offset;
    mock->last_length = length;
    mock->last_fill = value;
    return doe_mtl_fail(DOE_MTL_OK, "mock fill", error);
}

static doe_mtl_status dispatch(void *userdata, void *command, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot,
    doe_mtl_dimensions groups, doe_mtl_dimensions workgroup, uint32_t repeat,
    const doe_mtl_native_timestamps *timestamps, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && pipeline != NULL && resources != NULL);
    if (mock->fail == MOCK_DISPATCH) return failure(error);
    mock->dispatches++;
    mock->dispatch_sequence = ++mock->operation_sequence;
    mock->direct_resources = *resources;
    if (timestamps != NULL) mock->timestamp_pair = *timestamps;
    memcpy(mock->bindings, resources->buffers, sizeof(mock->bindings));
    mock->resources = *resources;
    mock->sizes_slot = sizes_slot;
    mock->groups = groups;
    mock->workgroup = workgroup;
    mock->repeat = repeat;
    return doe_mtl_fail(DOE_MTL_OK, "mock dispatch", error);
}

static doe_mtl_status dispatch_indirect(void *userdata, void *command, void *pipeline,
    const doe_mtl_native_resources *resources, uint32_t sizes_slot,
    void *arguments, size_t offset, doe_mtl_dimensions workgroup, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command && pipeline && resources && arguments);
    if (mock->fail == MOCK_INDIRECT) return failure(error);
    ++mock->indirects;
    mock->indirect_sequence = ++mock->operation_sequence;
    mock->indirect_arguments = arguments;
    mock->indirect_offset = offset;
    mock->indirect_pipeline = pipeline;
    memcpy(mock->bindings, resources->buffers, sizeof(mock->bindings));
    mock->resources = *resources;
    mock->sizes_slot = sizes_slot;
    mock->workgroup = workgroup;
    return doe_mtl_fail(DOE_MTL_OK, "mock indirect dispatch", error);
}

static doe_mtl_status texture_create(void *userdata, void *device,
    const doe_mtl_texture_desc *desc, void **out, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(device != NULL);
    if (mock->fail == MOCK_TEXTURE) return failure(error);
    mock->texture_desc = *desc;
    *out = object_create(mock, MOCK_KIND_TEXTURE);
    return doe_mtl_fail(DOE_MTL_OK, "mock texture", error);
}
static doe_mtl_status view_create(void *userdata, void *texture,
    const doe_mtl_texture_view_desc *desc, void **out, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(texture != NULL);
    ++mock->views;
    if (mock->fail == MOCK_VIEW || mock->views == mock->fail_view_at) return failure(error);
    mock->view_desc = *desc;
    *out = object_create(mock, MOCK_KIND_VIEW);
    return doe_mtl_fail(DOE_MTL_OK, "mock view", error);
}
static doe_mtl_status sampler_create(void *userdata, void *device,
    const doe_mtl_sampler_desc *desc, void **out, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(device != NULL);
    if (mock->fail == MOCK_SAMPLER) return failure(error);
    mock->sampler_desc = *desc;
    *out = object_create(mock, MOCK_KIND_SAMPLER);
    return doe_mtl_fail(DOE_MTL_OK, "mock sampler", error);
}
static doe_mtl_status upload_image(void *userdata, void *command, void *buffer,
    void *texture, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && buffer != NULL && texture != NULL);
    if (mock->fail == MOCK_UPLOAD_IMAGE) return failure(error);
    mock->uploads++;
    mock->image_to = *copy;
    return doe_mtl_fail(DOE_MTL_OK, "mock image upload", error);
}
static doe_mtl_status read_image(void *userdata, void *command, void *texture,
    void *buffer, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && buffer != NULL && texture != NULL);
    if (mock->fail == MOCK_READ_IMAGE) return failure(error);
    mock->reads++;
    mock->image_from = *copy;
    return doe_mtl_fail(DOE_MTL_OK, "mock image read", error);
}
static doe_mtl_status copy_image(void *userdata, void *command, void *source,
    const doe_mtl_native_texture_copy *from, void *destination,
    const doe_mtl_native_texture_copy *to, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && source != NULL && destination != NULL);
    if (mock->fail == MOCK_COPY_IMAGE) return failure(error);
    mock->image_copies++;
    mock->image_from = *from;
    mock->image_to = *to;
    return doe_mtl_fail(DOE_MTL_OK, "mock image copy", error);
}

static doe_mtl_status render_pipeline_create(void *userdata, void *device,
    const doe_mtl_render_pipeline_desc *desc, void **out, void **out_depth, doe_mtl_error *error) {
    mock_native *mock = userdata;
    mock_object *pipeline, *depth = NULL;
    assert(device != NULL && desc->vertex.msl != NULL);
    if (mock->fail == MOCK_RENDER_PIPELINE) return failure(error);
    pipeline = object_create(mock, MOCK_KIND_RENDER_PIPELINE);
    if (desc->depth_stencil.depth_format != 0 || desc->depth_stencil.stencil_format != 0) {
        if (mock->fail == MOCK_DEPTH_STATE) { release_object(mock, pipeline); return failure(error); }
        depth = object_create(mock, MOCK_KIND_DEPTH_STATE);
    }
    mock->render_pipeline_desc = *desc;
    /* The call's descriptor arrays are borrowed. A later ledger assertion must
     * inspect an owned snapshot, never a pointer into the constructor stack. */
    assert(desc->layout_count <= DOE_MTL_BUFFER_SLOTS);
    assert(desc->attribute_count <= DOE_MTL_VERTEX_ATTRIBUTES);
    if (desc->layout_count) memcpy(mock->render_layouts, desc->layouts,
        desc->layout_count * sizeof(*desc->layouts));
    if (desc->attribute_count) memcpy(mock->render_attributes, desc->attributes,
        desc->attribute_count * sizeof(*desc->attributes));
    mock->render_pipeline_desc.layouts = desc->layout_count ? mock->render_layouts : NULL;
    mock->render_pipeline_desc.attributes = desc->attribute_count ? mock->render_attributes : NULL;
    *out = pipeline;
    *out_depth = depth;
    return doe_mtl_fail(DOE_MTL_OK, "mock render pipeline", error);
}
static doe_mtl_status render(void *userdata, void *command,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draws,
    size_t count, doe_mtl_error *error) {
    mock_native *mock = userdata;
    size_t i;
    assert(command != NULL && pass->width != 0);
    if (mock->fail == MOCK_RENDER) return failure(error);
    mock->renders++;
    mock->render_sequence = ++mock->operation_sequence;
    mock->render_pass = *pass;
    mock->render_draw_count = count;
    for (i = 0; i < count && i < 2; ++i) mock->render_draws[i] = draws[i];
    return doe_mtl_fail(DOE_MTL_OK, "mock render", error);
}

static doe_mtl_status timestamps_create(void *userdata, void *device, uint32_t count,
    void **out, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(device != NULL && count != 0);
    if (mock->fail == MOCK_TIMESTAMPS) return failure(error);
    *out = object_create(mock, MOCK_KIND_TIMESTAMPS);
    return doe_mtl_fail(DOE_MTL_OK, "mock timestamps", error);
}
static doe_mtl_status timestamp(void *userdata, void *command, void *buffer,
    uint32_t index, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && buffer != NULL);
    if (mock->fail == MOCK_TIMESTAMP) return failure(error);
    mock->timestamps++;
    mock->timestamp_pair = (doe_mtl_native_timestamps){buffer, index, index};
    return doe_mtl_fail(DOE_MTL_OK, "mock timestamp", error);
}
static doe_mtl_status timestamp_boundary(void *userdata, void *command, void *buffer,
    uint32_t index, bool stage, void *scratch, void **event, uint64_t value, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert((stage == (scratch != NULL)) && value != 0);
    if (*event == NULL) *event = object_create(mock, MOCK_KIND_EVENT);
    return timestamp(userdata, command, buffer, index, error);
}
static doe_mtl_status resolve_timestamps(void *userdata, void *command, void *buffer,
    uint32_t first, uint32_t count, void *destination, size_t offset,
    void **event, uint64_t value, doe_mtl_error *error) {
    mock_native *mock = userdata;
    assert(command != NULL && buffer != NULL && destination != NULL);
    assert(value != 0);
    if (*event == NULL) *event = object_create(mock, MOCK_KIND_EVENT);
    if (mock->fail == MOCK_RESOLVE_TIMESTAMPS) return failure(error);
    mock->resolves++;
    mock->last_source = buffer;
    mock->last_source_offset = first;
    mock->last_length = count;
    mock->last_destination = destination;
    mock->last_destination_offset = offset;
    return doe_mtl_fail(DOE_MTL_OK, "mock timestamp resolve", error);
}

static void commit(void *userdata, void *handle) {
    mock_native *mock = userdata;
    mock_object *command = handle;
    assert(command->kind == MOCK_KIND_COMMAND && command->status == 0);
    mock->commits++;
    command->status = mock->unknown_commit ? 0 : 2;
}

static size_t observe(void *userdata, void *handle, bool wait, doe_mtl_error *error) {
    mock_native *mock = userdata;
    mock_object *command = handle;
    assert(command->kind == MOCK_KIND_COMMAND);
    mock->polls++;
    if (wait) mock->waits++;
    doe_mtl_fail(DOE_MTL_OK, "command completion", error);
    if (command->status == 5 && error != NULL) {
        doe_mtl_fail(DOE_MTL_EXECUTION_FAILED, "MTLCommandBuffer.error", error);
        error->native_code = command->code;
        memcpy(error->message, mock->completion_message, sizeof(error->message));
        error->message[sizeof(error->message) - 1] = '\0';
    }
    return command->status;
}

doe_mtl_native mock_driver(mock_native *mock) {
    doe_mtl_native native = {0};
    memset(mock, 0, sizeof(*mock));
    mock->pipeline_max_threads = 256;
    native.userdata = mock;
    native.close = close_driver;
    native.release = release_object;
    native.device = device_create;
    native.queue = queue_create;
    native.buffer = buffer_create;
    native.contents = buffer_contents;
    native.pipeline = pipeline_create;
    native.command = command_create;
    native.copy = copy_buffer;
    native.fill = fill_buffer;
    native.dispatch = dispatch;
    native.dispatch_indirect = dispatch_indirect;
    native.texture = texture_create;
    native.view = view_create;
    native.sampler = sampler_create;
    native.upload_texture = upload_image;
    native.read_texture = read_image;
    native.copy_texture = copy_image;
    native.render_pipeline = render_pipeline_create;
    native.render = render;
    native.timestamps = timestamps_create;
    native.timestamp = timestamp;
    native.timestamp_boundary = timestamp_boundary;
    native.resolve_timestamps = resolve_timestamps;
    native.commit = commit;
    native.observe = observe;
    return native;
}

void mock_result(mock_native *mock, size_t status, int64_t code) {
    assert(mock->last_command != NULL);
    mock->last_command->status = status;
    mock->last_command->code = code;
}

void mock_assert_closed(const mock_native *mock) {
    assert(mock->closes == 1);
    assert(mock->live == 0);
    assert(mock->created == mock->released);
}
