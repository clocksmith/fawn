/* SPDX-License-Identifier: MIT */
#include "snapshot.h"
#include <stdlib.h>
#include <string.h>

/* Public snapshot owner (include/doe_native_snapshot.h). Entry points hold no
 * device mutex on entry and call the public WebGPU procedures, which take it.
 * State that lowering reads (active, resources, views, marks) changes only
 * under the mutex. */

uint32_t doeNativeSnapshotEnableWriteMarks(WGPUDevice handle)
{
    native_device *device = (native_device *)handle;
    if (!device || device->backend.kind != WGPUBackendType_Vulkan) return 0;
    native_lock(device);
    bool enabled = device->write_marks, refused = device->shader_created;
    native_unlock(device);
    if (enabled) return 1;
    if (refused) {
        native_error(device, WGPUErrorType_Validation, "write marks must be enabled before the first shader module");
        return 0;
    }
    /* Dispatches outside a branch mark into this junk buffer. */
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = 256;
    descriptor.usage = WGPUBufferUsage_Storage;
    WGPUBuffer sink = wgpuDeviceCreateBuffer(handle, &descriptor);
    if (!sink) return 0;
    native_lock(device);
    refused = device->shader_created || device->write_marks;
    if (!refused) {
        device->mark_sink = (doe_resource_buffer *)sink;
        device->write_marks = true;
    }
    enabled = device->write_marks;
    native_unlock(device);
    if (refused) wgpuBufferRelease(sink);
    return enabled ? 1 : 0;
}

static void release_resources(native_snapshot *snapshot)
{
    for (size_t i = 0; i < snapshot->count; ++i) {
        native_snapshot_resource *resource = &snapshot->resources[i];
        if (resource->slot_buffer) wgpuBufferRelease(resource->slot_buffer);
        if (resource->slot_texture) wgpuTextureRelease(resource->slot_texture);
        doe_resource_buffer_release(resource->buffer);
        doe_resource_texture_release(resource->texture);
    }
    /* Slot views live for one lowering (snapshot_marks.c): none remain. */
    if (snapshot->marks) wgpuBufferRelease(snapshot->marks);
    free(snapshot->views);
    free(snapshot->resources);
    free(snapshot->host_marks);
    free(snapshot->last_marks);
}

static bool fail(native_device *device, const char *message)
{
    native_error(device, WGPUErrorType_Validation, message);
    return false;
}

static bool track_buffer(native_device *device, WGPUBuffer buffer, native_snapshot_resource *resource)
{
    const WGPUBufferUsage usage = wgpuBufferGetUsage(buffer);
    resource->size = wgpuBufferGetSize(buffer);
    if (!(usage & WGPUBufferUsage_CopySrc) || (usage & (WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite)))
        return fail(device, "snapshot buffers need CopySrc and no map usage");
    if (!resource->size || resource->size > UINT32_MAX)
        return fail(device, "snapshot buffers must be nonempty and at most 4 GiB");
    /* Unmapping a buffer mapped at creation would write the frozen base. */
    if (wgpuBufferGetMapState(buffer) != WGPUBufferMapState_Unmapped)
        return fail(device, "snapshot buffers must be unmapped");
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = resource->size;
    descriptor.usage = usage | WGPUBufferUsage_CopyDst;
    resource->slot_buffer = wgpuDeviceCreateBuffer((WGPUDevice)device, &descriptor);
    if (!resource->slot_buffer) return false;
    resource->buffer = (doe_resource_buffer *)buffer;
    doe_resource_buffer_retain(resource->buffer);
    resource->units = (uint32_t)((resource->size + NATIVE_SNAPSHOT_PAGE_BYTES - 1) / NATIVE_SNAPSHOT_PAGE_BYTES);
    return true;
}

static bool track_texture(native_device *device, WGPUTexture texture, native_snapshot_resource *resource)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    resource->texel_bytes = native_snapshot_texel_bytes(desc->format);
    if (desc->dimension != WGPUTextureDimension_2D || desc->depth_or_layers != 1 || desc->mip_levels != 1 ||
        desc->samples != 1 || !(desc->usage & WGPUTextureUsage_CopySrc) || !resource->texel_bytes)
        return fail(device, "snapshot textures are single-sample, single-mip, single-layer 2D with CopySrc and a tiled format");
    resource->width = desc->width;
    resource->height = desc->height;
    resource->size = (uint64_t)desc->width * desc->height * resource->texel_bytes;
    resource->tiles_x = (desc->width + NATIVE_SNAPSHOT_TILE_TEXELS - 1) / NATIVE_SNAPSHOT_TILE_TEXELS;
    resource->tiles_y = (desc->height + NATIVE_SNAPSHOT_TILE_TEXELS - 1) / NATIVE_SNAPSHOT_TILE_TEXELS;
    resource->units = resource->tiles_x * resource->tiles_y;
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = desc->usage | WGPUTextureUsage_CopyDst;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = (WGPUExtent3D){desc->width, desc->height, 1};
    descriptor.format = desc->format;
    descriptor.viewFormatCount = desc->view_format_count;
    descriptor.viewFormats = desc->view_formats;
    resource->slot_texture = wgpuDeviceCreateTexture((WGPUDevice)device, &descriptor);
    if (!resource->slot_texture) return false;
    resource->texture = (doe_resource_texture *)texture;
    doe_resource_texture_retain(resource->texture);
    return true;
}

void *doeNativeSnapshotCreate(WGPUDevice handle, const DoeNativeSnapshotDesc *desc)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (!desc || (desc->bufferCount && !desc->buffers) || (desc->textureCount && !desc->textures) ||
        !desc->bufferCount + !desc->textureCount == 2 || desc->bufferCount > SIZE_MAX / 2 - desc->textureCount) {
        native_error(device, WGPUErrorType_Validation, "invalid snapshot descriptor");
        return NULL;
    }
    native_lock(device);
    bool admitted = device->write_marks && !device->snapshot;
    native_unlock(device);
    if (!admitted) {
        native_error(device, WGPUErrorType_Validation, "snapshots need write marks and one snapshot per device");
        return NULL;
    }
    native_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    size_t count = desc->bufferCount + desc->textureCount;
    if (snapshot) snapshot->resources = calloc(count, sizeof(*snapshot->resources));
    if (!snapshot || !snapshot->resources) {
        free(snapshot);
        native_error(device, WGPUErrorType_OutOfMemory, "snapshot allocation failed");
        return NULL;
    }
    native_device_retain(device);
    snapshot->device = device;
    snapshot->buffer_count = desc->bufferCount;
    uint64_t units = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        native_snapshot_resource *resource = &snapshot->resources[i];
        for (size_t j = 0; ok && j < i; ++j)
            ok = i < desc->bufferCount ? desc->buffers[i] != desc->buffers[j] :
                j < desc->bufferCount || desc->textures[i - desc->bufferCount] != desc->textures[j - desc->bufferCount];
        if (!ok) fail(device, "snapshot resources must be distinct");
        else ok = i < desc->bufferCount ? track_buffer(device, desc->buffers[i], resource) :
            track_texture(device, desc->textures[i - desc->bufferCount], resource);
        snapshot->count = i + 1;
        resource->first = (uint32_t)units;
        units += resource->units;
        snapshot->ledger.declaredBytes += resource->size;
        if (ok && units >= UINT32_MAX / 4) ok = fail(device, "snapshot mark range exceeds 32 bits");
    }
    if (ok) {
        snapshot->mark_words = (uint32_t)units + 1;
        snapshot->host_marks = calloc(snapshot->mark_words, 1);
        snapshot->last_marks = calloc(snapshot->mark_words, 1);
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = (uint64_t)snapshot->mark_words * 4u;
        descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        snapshot->marks = wgpuDeviceCreateBuffer(handle, &descriptor);
        ok = snapshot->host_marks && snapshot->last_marks && snapshot->marks;
    }
    if (ok) {
        native_lock(device);
        ok = !device->snapshot;
        if (ok) device->snapshot = snapshot;
        native_unlock(device);
        if (!ok) fail(device, "a snapshot already exists on this device");
    }
    if (!ok) {
        release_resources(snapshot);
        native_device_release(device);
        free(snapshot);
        return NULL;
    }
    return snapshot;
}

/* Copies of one marked run: a page range of a buffer, a tile run of a row. */
static void copy_unit_run(native_snapshot *snapshot, WGPUCommandEncoder encoder, size_t index,
    uint32_t first, uint32_t end)
{
    const native_snapshot_resource *resource = &snapshot->resources[index];
    if (resource->buffer) {
        uint64_t offset = (uint64_t)first * NATIVE_SNAPSHOT_PAGE_BYTES;
        uint64_t limit = (uint64_t)end * NATIVE_SNAPSHOT_PAGE_BYTES;
        uint64_t size = (limit < resource->size ? limit : resource->size) - offset;
        wgpuCommandEncoderCopyBufferToBuffer(encoder, (WGPUBuffer)resource->buffer, offset,
            resource->slot_buffer, offset, size);
        snapshot->ledger.lastPrepareBytes += size;
    } else {
        uint32_t row = first / resource->tiles_x, column = first % resource->tiles_x;
        uint32_t x = column * NATIVE_SNAPSHOT_TILE_TEXELS, y = row * NATIVE_SNAPSHOT_TILE_TEXELS;
        uint32_t right = (column + (end - first)) * NATIVE_SNAPSHOT_TILE_TEXELS;
        uint32_t bottom = y + NATIVE_SNAPSHOT_TILE_TEXELS;
        if (right > resource->width) right = resource->width;
        if (bottom > resource->height) bottom = resource->height;
        WGPUTexelCopyTextureInfo source = {.texture=(WGPUTexture)resource->texture,.origin={x,y,0},
            .aspect=WGPUTextureAspect_All};
        WGPUTexelCopyTextureInfo destination = source;
        destination.texture = resource->slot_texture;
        const WGPUExtent3D extent = {right - x, bottom - y, 1};
        wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &extent);
        snapshot->ledger.lastPrepareBytes += (uint64_t)extent.width * extent.height * resource->texel_bytes;
    }
    ++snapshot->ledger.lastPrepareCopies;
}

static void prepare_slot(native_snapshot *snapshot, WGPUCommandEncoder encoder)
{
    for (size_t i = 0; i < snapshot->count; ++i) {
        const native_snapshot_resource *resource = &snapshot->resources[i];
        /* A texture run never spans a tile row; a buffer run spans any pages. */
        const uint32_t row = resource->buffer ? resource->units : resource->tiles_x;
        for (uint32_t start = 0; start < resource->units; start += row) {
            uint32_t unit = start;
            while (unit < start + row) {
                if (snapshot->filled && !snapshot->last_marks[resource->first + unit]) { ++unit; continue; }
                uint32_t end = unit + 1;
                while (end < start + row && (!snapshot->filled || snapshot->last_marks[resource->first + end])) ++end;
                copy_unit_run(snapshot, encoder, i, unit, end);
                unit = end;
            }
        }
    }
}

uint32_t doeNativeSnapshotBranchBegin(void *handle)
{
    native_snapshot *snapshot = handle;
    if (!snapshot) return 0;
    native_device *device = snapshot->device;
    native_lock(device);
    bool open = snapshot->active;
    native_unlock(device);
    if (open) return fail(device, "a snapshot branch is already open");
    snapshot->ledger.lastPrepareBytes = 0;
    snapshot->ledger.lastPrepareCopies = 0;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder((WGPUDevice)device, NULL);
    if (!encoder) return 0;
    /* A new slot gets a full copy; a recycled slot gets the previous
     * branch's marked pages and tiles back from the base. */
    prepare_slot(snapshot, encoder);
    wgpuCommandEncoderClearBuffer(encoder, snapshot->marks, 0, (uint64_t)snapshot->mark_words * 4u);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    if (!commands) return 0;
    WGPUQueue queue = wgpuDeviceGetQueue((WGPUDevice)device);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuQueueRelease(queue);
    wgpuCommandBufferRelease(commands);
    snapshot->ledger.totalPrepareBytes += snapshot->ledger.lastPrepareBytes;
    ++snapshot->ledger.prepareSubmits;
    ++snapshot->ledger.branchesBegun;
    memset(snapshot->host_marks, 0, snapshot->mark_words);
    native_lock(device);
    snapshot->filled = true;
    snapshot->active = true;
    native_unlock(device);
    return 1;
}

static uint64_t unit_bytes(const native_snapshot_resource *resource, uint32_t unit)
{
    if (resource->buffer) {
        uint64_t offset = (uint64_t)unit * NATIVE_SNAPSHOT_PAGE_BYTES;
        uint64_t rest = resource->size - offset;
        return rest < NATIVE_SNAPSHOT_PAGE_BYTES ? rest : NATIVE_SNAPSHOT_PAGE_BYTES;
    }
    uint32_t x = unit % resource->tiles_x * NATIVE_SNAPSHOT_TILE_TEXELS;
    uint32_t y = unit / resource->tiles_x * NATIVE_SNAPSHOT_TILE_TEXELS;
    uint32_t width = resource->width - x < NATIVE_SNAPSHOT_TILE_TEXELS ? resource->width - x : NATIVE_SNAPSHOT_TILE_TEXELS;
    uint32_t height = resource->height - y < NATIVE_SNAPSHOT_TILE_TEXELS ? resource->height - y : NATIVE_SNAPSHOT_TILE_TEXELS;
    return (uint64_t)width * height * resource->texel_bytes;
}

uint32_t doeNativeSnapshotBranchEnd(void *handle)
{
    native_snapshot *snapshot = handle;
    if (!snapshot) return 0;
    native_device *device = snapshot->device;
    uint32_t *words = malloc((size_t)snapshot->mark_words * 4u);
    if (!words) return fail(device, "snapshot marks allocation failed");
    native_lock(device);
    native_failure failure = native_failure_status(snapshot->active ? NATIVE_OK : NATIVE_INVALID_ARGUMENT);
    /* Completion observed by the caller retires through routine settlement;
     * a buffer still held by unresolved work reads as BUSY and is refused. */
    if (failure.status == NATIVE_OK) failure = native_settle_routine(device).failure;
    if (failure.status == NATIVE_OK)
        failure = native_buffer_read(doe_resource_buffer_native((doe_resource_buffer *)snapshot->marks), 0,
            words, (size_t)snapshot->mark_words * 4u);
    if (failure.status == NATIVE_OK) snapshot->active = false;
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        free(words);
        return fail(device, "snapshot branch end needs an open branch whose work has completed");
    }
    snapshot->ledger.lastMarkedUnits = 0;
    snapshot->ledger.lastMarkedBytes = 0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        const native_snapshot_resource *resource = &snapshot->resources[i];
        for (uint32_t unit = 0; unit < resource->units; ++unit) {
            const uint32_t index = resource->first + unit;
            snapshot->last_marks[index] = (uint8_t)(words[index] != 0 || snapshot->host_marks[index] != 0);
            if (!snapshot->last_marks[index]) continue;
            ++snapshot->ledger.lastMarkedUnits;
            snapshot->ledger.lastMarkedBytes += unit_bytes(resource, unit);
        }
    }
    free(words);
    ++snapshot->ledger.branchesEnded;
    return 1;
}

uint32_t doeNativeSnapshotGetLedger(void *handle, DoeNativeSnapshotLedger *out)
{
    native_snapshot *snapshot = handle;
    if (!snapshot || !out) return 0;
    *out = snapshot->ledger;
    native_lock(snapshot->device);
    out->slotViews = snapshot->view_count;
    native_unlock(snapshot->device);
    return 1;
}

size_t doeNativeSnapshotGetMarks(void *handle, size_t index, uint8_t *out, size_t capacity)
{
    native_snapshot *snapshot = handle;
    if (!snapshot || index >= snapshot->count) return 0;
    const native_snapshot_resource *resource = &snapshot->resources[index];
    for (size_t i = 0; out && i < capacity && i < resource->units; ++i)
        out[i] = snapshot->last_marks[resource->first + i];
    return resource->units;
}

void doeNativeSnapshotRelease(void *handle)
{
    native_snapshot *snapshot = handle;
    if (!snapshot) return;
    native_device *device = snapshot->device;
    native_lock(device);
    if (device->snapshot == snapshot) device->snapshot = NULL;
    native_unlock(device);
    release_resources(snapshot);
    free(snapshot);
    native_device_release(device);
}
