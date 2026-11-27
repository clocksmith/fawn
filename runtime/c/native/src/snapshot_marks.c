/* SPDX-License-Identifier: MIT */
#include "snapshot.h"
#include <stdlib.h>
#include <string.h>

/* Lowering hooks of the snapshot owner (snapshot.h). Every function here runs
 * under the device mutex inside one queue submission or pipeline creation. */

static bool writable(const DoeCompilerResourceV4 *resource)
{
    return (resource->kind == DOE_COMPILER_STORAGE_BUFFER && resource->access == DOE_COMPILER_READ_WRITE) ||
        (resource->kind == DOE_COMPILER_STORAGE_TEXTURE &&
         (resource->access == DOE_COMPILER_WRITE || resource->access == DOE_COMPILER_READ_WRITE));
}

/* The artifact itself decides: OpDecorate DescriptorSet/Binding of the hidden
 * mark variable (compiler/src/spirv_marks.c). */
static bool hidden_mark_binding(const doe_shader_view *view)
{
    if (view->target != DOE_SHADER_SPIRV || !view->words) return false;
    for (size_t at = 5; at + 3 < view->counts.words;) {
        uint32_t length = view->words[at] >> 16;
        if (!length || length > view->counts.words - at) return false;
        if ((view->words[at] & 65535u) == 71u && length == 4 && view->words[at + 2] == 33u &&
            view->words[at + 3] == DOE_COMPILER_WRITE_MARK_BINDING) {
            const uint32_t target = view->words[at + 1];
            for (size_t set = 5; set + 3 < view->counts.words;) {
                uint32_t width = view->words[set] >> 16;
                if (!width || width > view->counts.words - set) return false;
                if ((view->words[set] & 65535u) == 71u && width == 4 && view->words[set + 1] == target &&
                    view->words[set + 2] == 34u) return view->words[set + 3] == DOE_COMPILER_WRITE_MARK_GROUP;
                set += width;
            }
            return false;
        }
        at += length;
    }
    return false;
}

bool native_marks_pipeline(native_pipeline *pipeline, const doe_shader_view *view)
{
    uint32_t count = 0;
    for (size_t i = 0; i < view->counts.resources; ++i) {
        const DoeCompilerResourceV4 *resource = &view->resources[i];
        if (!writable(resource)) continue;
        if (count < DOE_COMPILER_WRITE_MARK_MAX_SLOTS)
            pipeline->mark_slots[count] = (native_binding){resource->group, resource->binding,
                resource->kind == DOE_COMPILER_STORAGE_BUFFER ? DOE_BIND_BUFFER : DOE_BIND_STORAGE_TEXTURE,
                WGPUBufferBindingType_Storage};
        ++count;
    }
    pipeline->marked = count != 0 && hidden_mark_binding(view);
    pipeline->unmarked_writes = count != 0 && !pipeline->marked;
    pipeline->mark_slot_count = pipeline->marked ? count : 0;
    /* The compiler refuses more slots; the hidden binding needs one entry. */
    return !pipeline->marked || (count <= DOE_COMPILER_WRITE_MARK_MAX_SLOTS &&
        pipeline->binding_count < NATIVE_MAX_PIPELINE_BINDINGS && !pipeline->immediate_bytes);
}

static native_snapshot *open_branch(const native_device *device)
{
    native_snapshot *snapshot = device->snapshot;
    return snapshot && snapshot->active ? snapshot : NULL;
}

doe_vk_status native_marks_dispatch(native_device *device, const native_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, doe_vk_binding_resource *mark, uint32_t *words)
{
    native_snapshot *snapshot = open_branch(device);
    doe_resource_buffer *marks = snapshot ? (doe_resource_buffer *)snapshot->marks : device->mark_sink;
    if (!marks) return DOE_VK_INVALID_ARGUMENT;
    native_buffer *payload = doe_resource_buffer_native(marks);
    *mark = (doe_vk_binding_resource){.kind=DOE_VK_BINDING_STORAGE,
        .value.buffer={payload->handle.vulkan,0,doe_resource_buffer_description(marks)->size}};
    /* Outside a branch every mark lands in the sink: last word of the buffer. */
    const uint32_t sink = snapshot ? snapshot->mark_words - 1 : UINT32_MAX;
    for (uint32_t k = 0; k < pipeline->mark_slot_count; ++k) {
        words[2 * k] = sink;
        words[2 * k + 1] = 0;
        if (!snapshot) continue;
        const native_binding slot = pipeline->mark_slots[k];
        size_t bound = pipeline->binding_count;
        for (size_t i = 0; i < pipeline->binding_count; ++i)
            if (pipeline->bindings[i].group == slot.group && pipeline->bindings[i].binding == slot.binding) bound = i;
        if (bound == pipeline->binding_count) continue;
        if (slot.kind == DOE_BIND_BUFFER) {
            for (size_t r = 0; r < snapshot->buffer_count; ++r) {
                const native_snapshot_resource *resource = &snapshot->resources[r];
                native_buffer *slot_payload = doe_resource_buffer_native((doe_resource_buffer *)resource->slot_buffer);
                if (bindings[bound].value.buffer.buffer != slot_payload->handle.vulkan) continue;
                /* A binding offset beyond 32 bits cannot be marked: refuse it. */
                if (bindings[bound].value.buffer.offset > UINT32_MAX) return DOE_VK_UNSUPPORTED;
                words[2 * k] = resource->first;
                words[2 * k + 1] = (uint32_t)bindings[bound].value.buffer.offset;
            }
        } else {
            for (size_t v = 0; v < snapshot->view_count; ++v) {
                const native_snapshot_view *view = &snapshot->views[v];
                if (bindings[bound].value.view != view->slot) continue;
                words[2 * k] = snapshot->resources[view->resource].first;
                words[2 * k + 1] = snapshot->resources[view->resource].tiles_x;
            }
        }
    }
    return DOE_VK_OK;
}

static size_t tracked_buffer(const native_snapshot *snapshot, const doe_resource_buffer *buffer)
{
    for (size_t i = 0; i < snapshot->buffer_count; ++i)
        if (snapshot->resources[i].buffer == buffer) return i;
    return SIZE_MAX;
}
static size_t tracked_texture(const native_snapshot *snapshot, const doe_resource_texture *texture)
{
    for (size_t i = snapshot->buffer_count; i < snapshot->count; ++i)
        if (snapshot->resources[i].texture == texture) return i;
    return SIZE_MAX;
}

bool native_snapshot_tracks_buffer(const native_device *device, const doe_resource_buffer *buffer)
{
    const native_snapshot *snapshot = device->snapshot;
    return snapshot && tracked_buffer(snapshot, buffer) != SIZE_MAX;
}
bool native_snapshot_tracks_texture(const native_device *device, const doe_resource_texture *texture)
{
    const native_snapshot *snapshot = device->snapshot;
    return snapshot && tracked_texture(snapshot, texture) != SIZE_MAX;
}

static void mark_bytes(native_snapshot *snapshot, const doe_resource_buffer *buffer, uint64_t offset, uint64_t size)
{
    size_t index = tracked_buffer(snapshot, buffer);
    if (index == SIZE_MAX || !size) return;
    const native_snapshot_resource *resource = &snapshot->resources[index];
    if (offset >= resource->size) return;
    if (size > resource->size - offset) size = resource->size - offset;
    for (uint64_t page = offset / NATIVE_SNAPSHOT_PAGE_BYTES; page <= (offset + size - 1) / NATIVE_SNAPSHOT_PAGE_BYTES; ++page)
        snapshot->host_marks[resource->first + page] = 1;
}

static void mark_tiles(native_snapshot *snapshot, size_t index, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (index == SIZE_MAX || !width || !height) return;
    const native_snapshot_resource *resource = &snapshot->resources[index];
    if (x >= resource->width || y >= resource->height) return;
    uint32_t right = width > resource->width - x ? resource->width : x + width;
    uint32_t bottom = height > resource->height - y ? resource->height : y + height;
    for (uint32_t row = y / NATIVE_SNAPSHOT_TILE_TEXELS; row <= (bottom - 1) / NATIVE_SNAPSHOT_TILE_TEXELS; ++row)
        for (uint32_t column = x / NATIVE_SNAPSHOT_TILE_TEXELS; column <= (right - 1) / NATIVE_SNAPSHOT_TILE_TEXELS; ++column)
            snapshot->host_marks[resource->first + row * resource->tiles_x + column] = 1;
}

static void mark_view(native_snapshot *snapshot, const doe_resource_view *view)
{
    if (!view) return;
    size_t index = tracked_texture(snapshot, doe_resource_view_texture(view));
    if (index != SIZE_MAX) mark_tiles(snapshot, index, 0, 0, UINT32_MAX, UINT32_MAX);
}

/* The slot view over the same description, created once per base view for
 * one lowering. The submission's commands keep the base view alive until
 * lower_end, and the recording retains the slot view it binds. */
static native_failure cache_view(native_snapshot *snapshot, doe_resource_view *view)
{
    if (!view) return native_failure_status(NATIVE_OK);
    size_t index = tracked_texture(snapshot, doe_resource_view_texture(view));
    if (index == SIZE_MAX) return native_failure_status(NATIVE_OK);
    for (size_t i = 0; i < snapshot->view_count; ++i)
        if (snapshot->views[i].view == view) return native_failure_status(NATIVE_OK);
    if (snapshot->view_count == snapshot->view_capacity) {
        size_t capacity = snapshot->view_capacity ? snapshot->view_capacity * 2 : 8;
        native_snapshot_view *grown = realloc(snapshot->views, capacity * sizeof(*grown));
        if (!grown) return native_failure_status(NATIVE_OUT_OF_MEMORY);
        snapshot->views = grown;
        snapshot->view_capacity = capacity;
    }
    const doe_resource_view_desc *desc = doe_resource_view_description(view);
    doe_vk_texture_view_desc native = {.base_mip=desc->base_mip,.mip_count=desc->mip_count,
        .base_layer=desc->base_layer,.layer_count=desc->layer_count,.dimension=DOE_VK_VIEW_2D};
    /* Stage 1 tracks 2D single-layer textures: their views are plain 2D. */
    if (desc->swizzled || desc->dimension != WGPUTextureViewDimension_2D ||
        !native_texture_aspect(desc->aspect, &native.aspect) ||
        !native_texture_format(snapshot->device->features, desc->format, &native.format))
        return native_failure_status(NATIVE_UNSUPPORTED);
    for (uint32_t i = 0; i < 4; ++i) native.swizzle[i] = DOE_VK_SWIZZLE_IDENTITY;
    native_texture *slot = doe_resource_texture_native((doe_resource_texture *)snapshot->resources[index].slot_texture);
    doe_vk_error error = {0};
    doe_vk_texture_view *created = NULL;
    doe_vk_status status = doe_vk_texture_view_create(slot->texture, &native, &created, &error);
    if (status != DOE_VK_OK) return native_failure_vulkan(status, &error);
    snapshot->views[snapshot->view_count++] = (native_snapshot_view){view, created, NULL, index};
    return native_failure_status(NATIVE_OK);
}

static native_failure cache_bound_views(native_snapshot *snapshot, const native_pipeline *pipeline,
    doe_resource_pipeline_layout *layout, doe_resource_bind_group *const *groups,
    const doe_resource_dispatch_offsets *offsets)
{
    if (pipeline->unmarked_writes) return native_failure_status(NATIVE_UNSUPPORTED);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    if (native_resolve_resources(pipeline, layout, groups, offsets, resolved) != DOE_RESOURCE_OK)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        if (resolved[i].kind != DOE_BIND_TEXTURE && resolved[i].kind != DOE_BIND_STORAGE_TEXTURE) continue;
        native_failure failure = cache_view(snapshot, resolved[i].resource.view);
        if (failure.status != NATIVE_OK) return failure;
    }
    return native_failure_status(NATIVE_OK);
}

/* The views a render pass writes: colors, resolves and a writable depth. */
enum { RENDER_WRITTEN_VIEWS = 3 + 2 * DOE_RESOURCE_MAX_COLOR_ATTACHMENTS };
static size_t written_views(const doe_resource_render_targets *targets, doe_resource_view **views)
{
    size_t count = 0;
    views[count++] = targets->color;
    views[count++] = targets->resolve_target;
    views[count++] = !targets->depth_read_only || !targets->stencil_read_only ? targets->depth_stencil : NULL;
    for (uint32_t i = 0; i < targets->color_count && i < DOE_RESOURCE_MAX_COLOR_ATTACHMENTS; ++i) {
        views[count++] = targets->colors[i].view;
        views[count++] = targets->colors[i].resolve_target;
    }
    return count;
}

static native_failure render_record(native_snapshot *snapshot, const doe_resource_render_record *record)
{
    const doe_resource_render_packet *pass = doe_resource_render_record_borrow(record);
    const doe_resource_render_targets *targets = &pass->targets;
    doe_resource_view *views[RENDER_WRITTEN_VIEWS];
    const size_t count = written_views(targets, views);
    for (size_t i = 0; i < count; ++i) {
        mark_view(snapshot, views[i]);
        native_failure failure = cache_view(snapshot, views[i]);
        if (failure.status != NATIVE_OK) return failure;
    }
    /* A read-only depth attachment is read from the slot as well. */
    native_failure failure = cache_view(snapshot, targets->depth_stencil);
    if (failure.status != NATIVE_OK) return failure;
    size_t draws = doe_resource_render_record_draw_count(record);
    for (size_t i = 0; i < draws; ++i) {
        const doe_resource_render_packet *bindings = NULL;
        doe_resource_render_draw_args args;
        if (!doe_resource_render_record_draw_borrow(record, i, &bindings, &args))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (!bindings->pipeline) continue;
        failure = cache_bound_views(snapshot, doe_resource_render_pipeline_native(bindings->pipeline),
            doe_resource_render_pipeline_get_layout(bindings->pipeline), bindings->groups, &bindings->offsets);
        if (failure.status != NATIVE_OK) return failure;
    }
    return native_failure_status(NATIVE_OK);
}

static native_failure scan_record(native_snapshot *snapshot, const doe_resource_command *record)
{
    switch (record->kind) {
    case DOE_COMMAND_COPY_BUFFER:
        mark_bytes(snapshot, record->value.copy.destination, record->value.copy.destination_offset, record->value.copy.size);
        break;
    case DOE_COMMAND_CLEAR_BUFFER:
        mark_bytes(snapshot, record->value.clear.buffer, record->value.clear.offset, record->value.clear.size);
        break;
    case DOE_COMMAND_BUFFER_TEXTURE: {
        const doe_resource_texture_copy *copy = &record->value.texture.copy;
        if (record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE)
            mark_tiles(snapshot, tracked_texture(snapshot, record->value.texture.texture),
                copy->origin[0], copy->origin[1], copy->width, copy->height);
        else mark_bytes(snapshot, record->value.texture.buffer, copy->offset, record->value.texture.region.required_bytes);
        break;
    }
    case DOE_COMMAND_COPY_TEXTURE: {
        const doe_resource_texture_blit *blit = &record->value.blit;
        mark_tiles(snapshot, tracked_texture(snapshot, blit->destination.texture),
            blit->destination.origin[0], blit->destination.origin[1], blit->width, blit->height);
        break;
    }
    case DOE_COMMAND_RESOLVE_QUERY:
        mark_bytes(snapshot, record->value.resolve.destination, record->value.resolve.offset,
            (uint64_t)record->value.resolve.count * 8u);
        break;
    case DOE_COMMAND_RENDER:
        return render_record(snapshot, record->value.render);
    case DOE_COMMAND_DISPATCH:
    case DOE_COMMAND_DISPATCH_INDIRECT: {
        doe_resource_pipeline *pipeline = record->value.dispatch.pipeline;
        return cache_bound_views(snapshot, doe_resource_pipeline_native(pipeline),
            doe_resource_pipeline_get_layout(pipeline), record->value.dispatch.groups,
            record->value.dispatch.dynamic_offsets);
    }
    case DOE_COMMAND_WRITE_TIMESTAMP:
    case DOE_COMMAND_BEGIN_OCCLUSION:
    case DOE_COMMAND_END_OCCLUSION:
        break;
    }
    return native_failure_status(NATIVE_OK);
}

/* Outside a branch the base is frozen: the next restore copies back only the
 * previous branch's units, so a base write elsewhere would leave the slot
 * stale. True when a pipeline that writes storage binds a tracked resource
 * writably (a storage texture of such a pipeline counts as written). */
static bool bound_writes(const native_snapshot *snapshot, const native_pipeline *pipeline,
    doe_resource_pipeline_layout *layout, doe_resource_bind_group *const *groups,
    const doe_resource_dispatch_offsets *offsets)
{
    if (!pipeline->marked && !pipeline->unmarked_writes) return false;
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    if (native_resolve_resources(pipeline, layout, groups, offsets, resolved) != DOE_RESOURCE_OK) return true;
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        if (resolved[i].kind == DOE_BIND_BUFFER && pipeline->bindings[i].buffer_type == WGPUBufferBindingType_Storage &&
            tracked_buffer(snapshot, resolved[i].resource.buffer.buffer) != SIZE_MAX) return true;
        if (resolved[i].kind == DOE_BIND_STORAGE_TEXTURE &&
            tracked_texture(snapshot, doe_resource_view_texture(resolved[i].resource.view)) != SIZE_MAX) return true;
    }
    return false;
}

static bool view_tracked(const native_snapshot *snapshot, const doe_resource_view *view)
{
    return view && tracked_texture(snapshot, doe_resource_view_texture(view)) != SIZE_MAX;
}

static bool writes_base(const native_snapshot *snapshot, const doe_resource_command *record)
{
    switch (record->kind) {
    case DOE_COMMAND_COPY_BUFFER: return tracked_buffer(snapshot, record->value.copy.destination) != SIZE_MAX;
    case DOE_COMMAND_CLEAR_BUFFER: return tracked_buffer(snapshot, record->value.clear.buffer) != SIZE_MAX;
    case DOE_COMMAND_BUFFER_TEXTURE:
        return record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE ?
            tracked_texture(snapshot, record->value.texture.texture) != SIZE_MAX :
            tracked_buffer(snapshot, record->value.texture.buffer) != SIZE_MAX;
    case DOE_COMMAND_COPY_TEXTURE: return tracked_texture(snapshot, record->value.blit.destination.texture) != SIZE_MAX;
    case DOE_COMMAND_RESOLVE_QUERY: return tracked_buffer(snapshot, record->value.resolve.destination) != SIZE_MAX;
    case DOE_COMMAND_RENDER: {
        const doe_resource_render_record *render = record->value.render;
        doe_resource_view *views[RENDER_WRITTEN_VIEWS];
        const size_t count = written_views(&doe_resource_render_record_borrow(render)->targets, views);
        for (size_t i = 0; i < count; ++i) if (view_tracked(snapshot, views[i])) return true;
        const size_t draws = doe_resource_render_record_draw_count(render);
        for (size_t i = 0; i < draws; ++i) {
            const doe_resource_render_packet *bindings = NULL;
            doe_resource_render_draw_args args;
            if (!doe_resource_render_record_draw_borrow(render, i, &bindings, &args)) return true;
            if (bindings->pipeline && bound_writes(snapshot, doe_resource_render_pipeline_native(bindings->pipeline),
                    doe_resource_render_pipeline_get_layout(bindings->pipeline), bindings->groups, &bindings->offsets))
                return true;
        }
        return false;
    }
    case DOE_COMMAND_DISPATCH:
    case DOE_COMMAND_DISPATCH_INDIRECT: {
        doe_resource_pipeline *pipeline = record->value.dispatch.pipeline;
        return bound_writes(snapshot, doe_resource_pipeline_native(pipeline), doe_resource_pipeline_get_layout(pipeline),
            record->value.dispatch.groups, record->value.dispatch.dynamic_offsets);
    }
    case DOE_COMMAND_WRITE_TIMESTAMP:
    case DOE_COMMAND_BEGIN_OCCLUSION:
    case DOE_COMMAND_END_OCCLUSION:
        return false;
    }
    return true;
}

native_failure native_snapshot_admit_base(const native_device *device, doe_resource_commands *const *commands,
    size_t count)
{
    const native_snapshot *snapshot = device->snapshot;
    if (!snapshot || snapshot->active) return native_failure_status(NATIVE_OK);
    for (size_t c = 0; c < count; ++c) {
        size_t records = 0;
        const doe_resource_command *record = doe_resource_commands_borrow(commands[c], &records);
        for (size_t i = 0; i < records; ++i)
            if (writes_base(snapshot, &record[i])) return native_failure_status(NATIVE_UNSUPPORTED);
    }
    return native_failure_status(NATIVE_OK);
}

native_failure native_snapshot_lower_begin(native_device *device, doe_resource_commands *const *commands, size_t count)
{
    native_snapshot *snapshot = open_branch(device);
    if (!snapshot) return native_snapshot_admit_base(device, commands, count);
    if (device->backend.kind != WGPUBackendType_Vulkan || snapshot->swapped)
        return native_failure_status(NATIVE_UNSUPPORTED);
    for (size_t c = 0; c < count; ++c) {
        size_t records = 0;
        const doe_resource_command *record = doe_resource_commands_borrow(commands[c], &records);
        for (size_t i = 0; i < records; ++i) {
            native_failure failure = scan_record(snapshot, &record[i]);
            if (failure.status != NATIVE_OK) return failure;
        }
    }
    /* The base payloads borrow the slot's native objects for this lowering
     * only; lower_end restores them before the device mutex is released. */
    for (size_t i = 0; i < snapshot->count; ++i) {
        native_snapshot_resource *resource = &snapshot->resources[i];
        if (resource->buffer) {
            native_buffer *base = doe_resource_buffer_native(resource->buffer);
            resource->saved_buffer = base->handle;
            base->handle = ((native_buffer *)doe_resource_buffer_native((doe_resource_buffer *)resource->slot_buffer))->handle;
        } else {
            native_texture *base = doe_resource_texture_native(resource->texture);
            resource->saved_texture = base->texture;
            base->texture = ((native_texture *)doe_resource_texture_native((doe_resource_texture *)resource->slot_texture))->texture;
        }
    }
    for (size_t i = 0; i < snapshot->view_count; ++i) {
        native_view *base = doe_resource_view_native(snapshot->views[i].view);
        snapshot->views[i].saved = base->view;
        base->view = snapshot->views[i].slot;
    }
    snapshot->swapped = true;
    ++snapshot->ledger.branchSubmits;
    return native_failure_status(NATIVE_OK);
}

void native_snapshot_lower_end(native_device *device)
{
    native_snapshot *snapshot = device->snapshot;
    if (!snapshot) return;
    for (size_t i = 0; snapshot->swapped && i < snapshot->count; ++i) {
        native_snapshot_resource *resource = &snapshot->resources[i];
        if (resource->buffer) ((native_buffer *)doe_resource_buffer_native(resource->buffer))->handle = resource->saved_buffer;
        else ((native_texture *)doe_resource_texture_native(resource->texture))->texture = resource->saved_texture;
    }
    /* A slot view lives for this lowering only; the recording retains what it
     * binds or renders to until its work retires. */
    for (size_t i = 0; i < snapshot->view_count; ++i) {
        if (snapshot->swapped)
            ((native_view *)doe_resource_view_native(snapshot->views[i].view))->view = snapshot->views[i].saved;
        doe_vk_texture_view_release(snapshot->views[i].slot);
    }
    snapshot->view_count = 0;
    snapshot->swapped = false;
}

uint32_t native_snapshot_texel_bytes(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_R8Unorm: case WGPUTextureFormat_R8Uint: case WGPUTextureFormat_R8Sint: return 1;
    case WGPUTextureFormat_RG8Unorm: case WGPUTextureFormat_R16Uint: case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_R16Float: return 2;
    case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_RGBA8UnormSrgb: case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb: case WGPUTextureFormat_RGBA8Uint: case WGPUTextureFormat_RGBA8Sint:
    case WGPUTextureFormat_R32Uint: case WGPUTextureFormat_R32Sint: case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_RG16Float: return 4;
    case WGPUTextureFormat_RGBA16Float: case WGPUTextureFormat_RGBA16Uint: case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_RG32Float: case WGPUTextureFormat_RG32Uint: case WGPUTextureFormat_RG32Sint: return 8;
    case WGPUTextureFormat_RGBA32Float: case WGPUTextureFormat_RGBA32Uint: case WGPUTextureFormat_RGBA32Sint: return 16;
    default: return 0;
    }
}
