/* SPDX-License-Identifier: MIT
 * Shared helpers of the snapshot consumers (snapshot_ledger.c, snapshot_canary.c):
 * a marks-enabled device, host readback of tracked resources and the CPU diff
 * that the compiler and submission marks must cover with zero misses. */
#ifndef DOE_NATIVE_TEST_SNAPSHOT_FIXTURE_H
#define DOE_NATIVE_TEST_SNAPSHOT_FIXTURE_H
#include "consumer.h"
#include "doe_native_snapshot.h"
#include <stdint.h>
#include <stdlib.h>

enum { PAGE = 65536, TILE = 256 };

/* Vulkan's per-stage maxima here, so one pipeline can carry the sixteen
 * writable storage resources write marks admit. */
static inline void snapshot_device(state *s)
{
    consumer_line_output();
    WGPULimits limits = WGPU_LIMITS_INIT;
    limits.maxStorageBuffersPerShaderStage = 10;
    limits.maxStorageTexturesPerShaderStage = 8;
    consumer_init_device(s, NULL, 0, &limits);
    assert(doeNativeSnapshotEnableWriteMarks(s->device) == 1);
    assert(doeNativeSnapshotEnableWriteMarks(s->device) == 1);
}

static inline WGPUShaderModule snapshot_module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(module);
    return module;
}

/* Bytes of a buffer through a fresh map-read copy; caller frees. The copy is
 * recorded by the caller's encoder so it observes the same branch. */
typedef struct readback { WGPUBuffer buffer; uint64_t size; } readback;
static inline readback readback_buffer(state *s, WGPUCommandEncoder encoder, WGPUBuffer source, uint64_t size)
{
    readback result = {buffer(s, size, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst), size};
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, result.buffer, 0, size);
    return result;
}
static inline readback readback_texture(state *s, WGPUCommandEncoder encoder, WGPUTexture source,
    uint32_t width, uint32_t height)
{
    const uint32_t pitch = (width * 4 + 255) / 256 * 256;
    readback result = {buffer(s, (uint64_t)pitch * height, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst),
        (uint64_t)pitch * height};
    WGPUTexelCopyTextureInfo from = {.texture=source};
    WGPUTexelCopyBufferInfo to = {.buffer=result.buffer,.layout={.bytesPerRow=pitch,.rowsPerImage=height}};
    WGPUExtent3D extent = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &extent);
    return result;
}
/* Maps after the caller waited for the work; returns an owned copy. */
static inline uint8_t *readback_take(state *s, readback *r)
{
    uint8_t *bytes = malloc(r->size);
    assert(bytes);
    memcpy(bytes, map(s, r->buffer, r->size), r->size);
    wgpuBufferUnmap(r->buffer);
    wgpuBufferRelease(r->buffer);
    r->buffer = NULL;
    return bytes;
}
/* Tightly packed RGBA8 rows from a 256-byte-pitch readback. */
static inline void unpitch(uint8_t *bytes, uint32_t width, uint32_t height)
{
    const uint32_t pitch = (width * 4 + 255) / 256 * 256;
    for (uint32_t y = 0; y < height; ++y) memmove(bytes + (size_t)y * width * 4, bytes + (size_t)y * pitch, (size_t)width * 4);
}

/* Units (64 KiB pages of a buffer, 256x256 tiles of an RGBA8 texture) whose
 * bytes differ between before and after. width == 0 selects pages. */
static inline size_t changed_units(const uint8_t *before, const uint8_t *after, uint64_t size,
    uint32_t width, uint32_t height, uint8_t *out, size_t capacity)
{
    size_t count = 0;
    if (!width) {
        size_t units = (size_t)((size + PAGE - 1) / PAGE);
        assert(units <= capacity);
        for (size_t u = 0; u < units; ++u) {
            uint64_t start = (uint64_t)u * PAGE, length = size - start < PAGE ? size - start : PAGE;
            out[u] = memcmp(before + start, after + start, (size_t)length) != 0;
            count += out[u];
        }
        return count;
    }
    const uint32_t tiles_x = (width + TILE - 1) / TILE, tiles_y = (height + TILE - 1) / TILE;
    assert((size_t)tiles_x * tiles_y <= capacity);
    memset(out, 0, (size_t)tiles_x * tiles_y);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            size_t at = ((size_t)y * width + x) * 4;
            if (memcmp(before + at, after + at, 4)) out[(y / TILE) * tiles_x + x / TILE] = 1;
        }
    for (size_t i = 0; i < (size_t)tiles_x * tiles_y; ++i) count += out[i];
    return count;
}

/* Zero misses: every changed unit is marked. Returns the marked count. */
static inline size_t require_covered(void *snapshot, size_t resource, const uint8_t *changed, size_t units,
    const char *name)
{
    uint8_t *marks = malloc(units);
    assert(marks);
    assert(doeNativeSnapshotGetMarks(snapshot, resource, marks, units) == units);
    size_t marked = 0;
    for (size_t u = 0; u < units; ++u) {
        if (changed[u] && !marks[u]) {
            fprintf(stderr, "FAIL %s: unit %zu changed but was not marked\n", name, u);
            abort();
        }
        marked += marks[u];
    }
    free(marks);
    return marked;
}

static inline double mib(uint64_t bytes) { return (double)bytes / (1024.0 * 1024.0); }
#endif
