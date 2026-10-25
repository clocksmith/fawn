/* SPDX-License-Identifier: MIT
 * Real delegate calls plus independent triangle/nearest-sampling pixel oracle. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned pass_pipelines, pass_groups, bundle_pipelines, bundle_groups, bundles;
static bool reject_bundle;
static unsigned texture_copies;
void __real_wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder, const WGPUTexelCopyTextureInfo *,
    const WGPUTexelCopyTextureInfo *, const WGPUExtent3D *);
void __wrap_wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder encoder, const WGPUTexelCopyTextureInfo *from,
    const WGPUTexelCopyTextureInfo *to, const WGPUExtent3D *extent)
{
    texture_copies++;
    __real_wgpuCommandEncoderCopyTextureToTexture(encoder, from, to, extent);
}
void __real_wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder, WGPURenderPipeline);
void __wrap_wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
{
    pass_pipelines++;
    __real_wgpuRenderPassEncoderSetPipeline(pass, pipeline);
}
void __real_wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder, uint32_t, WGPUBindGroup, size_t, const uint32_t *);
void __wrap_wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder pass, uint32_t index, WGPUBindGroup group,
    size_t count, const uint32_t *offsets)
{
    pass_groups++;
    __real_wgpuRenderPassEncoderSetBindGroup(pass, index, group, count, offsets);
}
void __real_wgpuRenderBundleEncoderSetPipeline(WGPURenderBundleEncoder, WGPURenderPipeline);
void __wrap_wgpuRenderBundleEncoderSetPipeline(WGPURenderBundleEncoder encoder, WGPURenderPipeline pipeline)
{
    bundle_pipelines++;
    __real_wgpuRenderBundleEncoderSetPipeline(encoder, pipeline);
}
void __real_wgpuRenderBundleEncoderSetBindGroup(WGPURenderBundleEncoder, uint32_t, WGPUBindGroup, size_t, const uint32_t *);
void __wrap_wgpuRenderBundleEncoderSetBindGroup(WGPURenderBundleEncoder encoder, uint32_t index, WGPUBindGroup group,
    size_t count, const uint32_t *offsets)
{
    bundle_groups++;
    __real_wgpuRenderBundleEncoderSetBindGroup(encoder, index, group, count, offsets);
}
void __real_wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder, size_t, const WGPURenderBundle *);
void __wrap_wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder pass, size_t count, const WGPURenderBundle *items)
{
    bundles += (unsigned)count;
    __real_wgpuRenderPassEncoderExecuteBundles(pass, count, items);
}
WGPURenderBundle __real_wgpuRenderBundleEncoderFinish(WGPURenderBundleEncoder, const WGPURenderBundleDescriptor *);
WGPURenderBundle __wrap_wgpuRenderBundleEncoderFinish(WGPURenderBundleEncoder encoder, const WGPURenderBundleDescriptor *desc)
{
    return reject_bundle ? NULL : __real_wgpuRenderBundleEncoderFinish(encoder, desc);
}

static void check_pixels(doe_runtime *runtime, bool red_tint)
{
    texture_entry *target = runtime_find_texture(runtime, 700);
    assert(target && target->declared.width == 32 && target->declared.height == 32);
    doe_command copy = {.kind = DOE_COMMAND_COPY, .label = "copy_buffer_to_texture"};
    copy.as.copy.direction = DOE_COPY_TEXTURE_TO_BUFFER;
    copy.as.copy.bytes = 8192;
    copy.as.copy.src.handle = 700;
    copy.as.copy.src.texture = target->declared;
    unsigned channels = target->declared.format == DOE_FORMAT_R8_UNORM ? 1 :
        target->declared.format == DOE_FORMAT_RG8_UNORM ? 2 : 4;
    copy.as.copy.dst.handle = 710 + channels;
    copy.as.copy.dst.bytes_per_row = 256;
    copy.as.copy.dst.rows_per_image = 32;
    doe_trace_execution result;
    assert(doe_runtime_execute(runtime, &copy, &result));
    uint8_t *data = NULL;
    size_t size = 0;
    assert(doe_runtime_read_buffer(runtime, 710 + channels, &data, &size, &result) && size == 8192);
    const uint8_t sampled[4][4] = {{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    for (unsigned y = 0; y < 32; y++) {
        for (unsigned x = 0; x < 32; x++) {
            /* Screen triangle vertices (16,8),(8,24),(24,24). No pixel center
             * lies on an edge, so every texel has an unambiguous host oracle. */
            double px = x + .5, py = y + .5;
            bool inside = py > 8 && py < 24 && px > 16 - (py - 8) / 2 && px < 16 + (py - 8) / 2;
            uint8_t expected[4] = {0,0,0,255};
            if (inside) {
                memcpy(expected, sampled[(y >= 16 ? 2 : 0) + (x >= 16)], 4);
                if (red_tint) expected[0] = 255;
            }
            if (memcmp(data + y * 256 + x * channels, expected, channels)) {
                fprintf(stderr, "pixel %u,%u actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n", x,y,
                    data[y*256+x*4],data[y*256+x*4+1],data[y*256+x*4+2],data[y*256+x*4+3],
                    expected[0],expected[1],expected[2],expected[3]);
                abort();
            }
        }
        for (unsigned x = 32 * channels; x < 256; x++) assert(data[y * 256 + x] == 0);
    }
    free(data);
}

int main(void)
{
    char message[256];
    doe_runtime_options options = {.adapter = DOE_ADAPTER_AUTO, .upload_submit_every = 1,
        .public_webgpu_renderer = true};
    doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
    if (!runtime) fprintf(stderr, "%s\n", message);
    assert(runtime);
    assert(!strcmp(runtime->info.adapter_type, "cpu"));
    doe_command draw = {.kind = DOE_COMMAND_RENDER_DRAW, .label = "render_draw"};
    draw.as.render = (doe_render_draw){.draw_count = 3, .vertex_count = 3, .instance_count = 1,
        .target_handle = 700, .target_width = 32, .target_height = 32, .target_format = DOE_FORMAT_RGBA8_UNORM,
        .viewport_max_depth = 1};
    uint32_t offset = 0;
    draw.as.render.dynamic_offsets = &offset;
    draw.as.render.dynamic_offset_count = 1;
    for (unsigned mode = 0; mode < 8; mode++) {
        draw.as.render.render_pass = (mode & 1) != 0;
        draw.as.render.redundant_pipeline = (mode & 2) != 0;
        draw.as.render.redundant_bind_group = (mode & 4) != 0;
        pass_pipelines = pass_groups = bundle_pipelines = bundle_groups = bundles = 0;
        doe_trace_execution result;
        bool ok = doe_runtime_execute(runtime, &draw, &result);
        if (!ok) fprintf(stderr, "%s: %s\n", result.status_code, result.message);
        assert(ok && result.dispatch_count == 3 && result.submit_count == 1);
        unsigned pipelines = draw.as.render.redundant_pipeline ? 3 : 1;
        unsigned groups = draw.as.render.redundant_bind_group ? 3 : 1;
        assert(pass_pipelines == (draw.as.render.render_pass ? pipelines : 0));
        assert(pass_groups == (draw.as.render.render_pass ? groups : 0));
        assert(bundle_pipelines == (draw.as.render.render_pass ? 0 : pipelines));
        assert(bundle_groups == (draw.as.render.render_pass ? 0 : groups));
        assert(bundles == !draw.as.render.render_pass);
        check_pixels(runtime, false);
    }
    /* A different dynamic slot changes real shader output, not just call counts. */
    uint32_t words[3];
    const float color[3] = {5000, 0, 0};
    memcpy(words, color, sizeof(words));
    doe_command write = {.kind = DOE_COMMAND_BUFFER_WRITE, .label = "buffer_write",
        .as.buffer_write = {.handle = UINT64_C(0x8C9F260000000000), .offset = 256,
            .buffer_size = 16384, .words = words, .word_count = 3}};
    doe_trace_execution result;
    assert(doe_runtime_execute(runtime, &write, &result));
    offset = 256;
    assert(doe_runtime_execute(runtime, &draw, &result));
    check_pixels(runtime, true);
    uint32_t indices[3] = {0,1,2};
    draw.as.render.indices = indices;
    draw.as.render.index_data_count = draw.as.render.index_count = 3;
    for (unsigned mode = 0; mode < 4; mode++) {
        draw.as.render.indexed = (mode & 1) != 0;
        draw.as.render.render_pass = (mode & 2) != 0;
        draw.kind = draw.as.render.indexed ? DOE_COMMAND_DRAW_INDEXED_INDIRECT : DOE_COMMAND_DRAW_INDIRECT;
        assert(doe_runtime_execute(runtime, &draw, &result));
        check_pixels(runtime, true);
    }
    for (unsigned bad = 0; bad < 3; bad++) {
        offset = bad == 0 ? 1 : bad == 1 ? 16384 : 0;
        draw.as.render.dynamic_offset_count = bad == 2 ? 2 : 1;
        assert(!doe_runtime_execute(runtime, &draw, &result));
        assert(!strcmp(result.status_code, "render_dynamic_offsets"));
    }
    draw.as.render.dynamic_offset_count = 1;
    draw.as.render.render_pass = false;
    reject_bundle = true;
    assert(!doe_runtime_execute(runtime, &draw, &result) && !strcmp(result.status_code, "render_bundle_failed"));
    reject_bundle = false;
    assert(doe_runtime_execute(runtime, &draw, &result));
    check_pixels(runtime, false);
    draw.kind = DOE_COMMAND_RENDER_DRAW;
    draw.as.render.indexed = false;
    draw.as.render.temporary_render_texture = true;
    for (unsigned format = 0; format < 3; format++) {
        draw.as.render.target_format = format == 0 ? DOE_FORMAT_R8_UNORM :
            format == 1 ? DOE_FORMAT_RG8_UNORM : DOE_FORMAT_RGBA8_UNORM;
        for (unsigned minimum = 0; minimum < 3; minimum++) {
            draw.as.render.temporary_render_min_mip = minimum;
            texture_copies = 0;
            assert(doe_runtime_execute(runtime, &draw, &result));
            assert(texture_copies == (format < 2 && minimum == 0 ? 1u : 0u));
            check_pixels(runtime, false);
        }
    }
    printf("PASS public renderer: 8 state modes, bundle/pass call counts, all triangle texels, dynamic uniform pixels, "
        "4 indirect modes, temporary R8/RG8 copy-back texels and guards, offset refusals and bundle-failure recovery: %s (%s, %s)\n",
        runtime->info.device, runtime->info.adapter_type, runtime->info.backend);
    doe_runtime_close(runtime);
    return 0;
}
