/* SPDX-License-Identifier: MIT
 * render_draw / render_pass and samplers over the public WebGPU C ABI, with
 * the semantics of doe-zig-runtime's native Vulkan lane (vk_render.zig): the
 * default proxy pipeline (bench kernels draw_call_proxy_vertex/fragment.wgsl,
 * no bind groups) draws `draw_count` times inline in one render pass that
 * clears the color target to (0, 0, 0, 1) and stores it, then one submission
 * is waited to completion. The target is a scratch texture for the command
 * unless the command names an existing texture. As in that lane, the
 * pipeline, bind-group and encode modes and dynamic offsets are validated but
 * do not change the recorded work. draw_indirect and draw_indexed_indirect
 * write the command's draw arguments `draw_count` times into one Indirect
 * buffer, as doe-zig-runtime's WebGPU path does for multi-draw, and record one
 * indirect draw per record; doe-zig-runtime's native lane draws those
 * commands directly. */
#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

static WGPUShaderModule shader_module(doe_runtime *runtime, const char *name, doe_trace_execution *execution)
{
    char *source = NULL;
    size_t size = 0;
    char *path = runtime_resolve_kernel(runtime, name, &source, &size);
    if (!path) {
        runtime_fail(execution, "render_shader_missing", "render shader %s was not found under the kernel root", name);
        return NULL;
    }
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, size};
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(runtime->device, &descriptor);
    free(source);
    free(path);
    if (!module) runtime_fail(execution, "render_shader_failed", "render shader %s was not created", name);
    return module;
}

static WGPURenderPipeline render_pipeline(doe_runtime *runtime, doe_texture_format format, doe_trace_execution *execution)
{
    for (size_t i = 0; i < runtime->render_pipeline_count; i++)
        if (runtime->render_pipelines[i].format == format) return runtime->render_pipelines[i].pipeline;
    if (runtime->render_pipeline_count == runtime->render_pipeline_capacity) {
        size_t capacity = runtime->render_pipeline_capacity ? runtime->render_pipeline_capacity * 2 : 4;
        render_pipeline_entry *grown = realloc(runtime->render_pipelines, capacity * sizeof(*grown));
        if (!grown) { runtime_fail(execution, "out_of_memory", "out of memory"); return NULL; }
        runtime->render_pipelines = grown;
        runtime->render_pipeline_capacity = capacity;
    }
    WGPUShaderModule vertex = shader_module(runtime, "draw_call_proxy_vertex.wgsl", execution);
    WGPUShaderModule fragment = vertex ? shader_module(runtime, "draw_call_proxy_fragment.wgsl", execution) : NULL;
    WGPURenderPipeline pipeline = NULL;
    if (vertex && fragment) {
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = runtime_webgpu_format(format);
        WGPUFragmentState fragment_state = WGPU_FRAGMENT_STATE_INIT;
        fragment_state.module = fragment;
        fragment_state.entryPoint = (WGPUStringView){"main", WGPU_STRLEN};
        fragment_state.targetCount = 1;
        fragment_state.targets = &target;
        WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        descriptor.vertex.module = vertex;
        descriptor.vertex.entryPoint = (WGPUStringView){"main", WGPU_STRLEN};
        descriptor.fragment = &fragment_state;
        pipeline = wgpuDeviceCreateRenderPipeline(runtime->device, &descriptor);
        if (!pipeline) runtime_fail(execution, "render_pipeline_failed", "the proxy render pipeline was not created");
    }
    if (fragment) wgpuShaderModuleRelease(fragment);
    if (vertex) wgpuShaderModuleRelease(vertex);
    if (pipeline) runtime->render_pipelines[runtime->render_pipeline_count++] = (render_pipeline_entry){format, pipeline};
    return pipeline;
}

void runtime_release_render(doe_runtime *runtime)
{
    runtime_release_public_render(runtime);
    for (size_t i = 0; i < runtime->render_pipeline_count; i++) wgpuRenderPipelineRelease(runtime->render_pipelines[i].pipeline);
    free(runtime->render_pipelines);
    for (size_t i = 0; i < runtime->sampler_count; i++) wgpuSamplerRelease(runtime->samplers[i].sampler);
    free(runtime->samplers);
    runtime->render_pipelines = NULL;
    runtime->samplers = NULL;
    runtime->render_pipeline_count = runtime->render_pipeline_capacity = 0;
    runtime->sampler_count = runtime->sampler_capacity = 0;
}

/* The index buffer of an indexed draw: the command's data in its format. */
WGPUBuffer runtime_render_indices(doe_runtime *runtime, const doe_render_draw *draw, uint64_t *size)
{
    size_t element = draw->index_uint32 ? 4 : 2;
    *size = ((uint64_t)draw->index_data_count * element + 3) & ~UINT64_C(3);
    uint8_t *bytes = calloc(1, (size_t)*size);
    if (!bytes) return NULL;
    for (size_t i = 0; i < draw->index_data_count; i++) {
        if (element == 4) memcpy(bytes + 4 * i, &draw->indices[i], 4);
        else {
            uint16_t value = (uint16_t)draw->indices[i];
            memcpy(bytes + 2 * i, &value, 2);
        }
    }
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = *size;
    descriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (buffer) wgpuQueueWriteBuffer(runtime->queue, buffer, 0, bytes, (size_t)*size);
    free(bytes);
    return buffer;
}

enum { INDIRECT_ARGUMENTS_MAX = 256u << 20 }; /* WebGPU's default maxBufferSize */

/* The `draw_count` argument records of an indirect draw: VkDrawIndirectCommand
 * or VkDrawIndexedIndirectCommand words, as WebGPU defines them. */
WGPUBuffer runtime_render_arguments(doe_runtime *runtime, const doe_render_draw *draw, uint32_t *stride,
    doe_trace_execution *execution)
{
    uint32_t record[5];
    uint32_t words = 4;
    if (draw->indexed) {
        record[0] = draw->index_count;
        record[1] = draw->instance_count;
        record[2] = draw->first_index;
        memcpy(&record[3], &draw->base_vertex, 4);
        record[4] = draw->first_instance;
        words = 5;
    } else {
        record[0] = draw->vertex_count;
        record[1] = draw->instance_count;
        record[2] = draw->first_vertex;
        record[3] = draw->first_instance;
    }
    *stride = words * 4;
    /* Without indirect-first-instance, which this runtime does not request,
     * WebGPU skips such a draw instead of failing it. */
    if (draw->first_instance) {
        runtime_fail(execution, "render_indirect_first_instance", "an indirect draw's first_instance must be 0");
        return NULL;
    }
    uint64_t size = (uint64_t)draw->draw_count * *stride;
    if (size > INDIRECT_ARGUMENTS_MAX) {
        runtime_fail(execution, "render_indirect_arguments_too_large", "%u indirect draw records exceed %u bytes",
            draw->draw_count, (unsigned)INDIRECT_ARGUMENTS_MAX);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes) { runtime_fail(execution, "out_of_memory", "out of memory"); return NULL; }
    for (uint32_t i = 0; i < draw->draw_count; i++) memcpy(bytes + (size_t)i * *stride, record, *stride);
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size;
    descriptor.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (buffer) wgpuQueueWriteBuffer(runtime->queue, buffer, 0, bytes, (size_t)size);
    else runtime_fail(execution, "render_indirect_buffer_failed", "the indirect argument buffer was not created");
    free(bytes);
    return buffer;
}

void runtime_render_copy_back(WGPUCommandEncoder encoder, WGPUTexture source, WGPUTexture destination,
    const doe_render_draw *draw)
{
    WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUTexelCopyTextureInfo to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    from.texture = source;
    to.texture = destination;
    WGPUExtent3D extent = {draw->target_width, draw->target_height, 1};
    wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &extent);
}

bool runtime_execute_render(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    if (runtime->options.public_webgpu_renderer) return runtime_execute_public_render(runtime, command, execution);
    const doe_render_draw *draw = &command->as.render;
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    WGPURenderPipeline pipeline = render_pipeline(runtime, draw->target_format, execution);
    bool ok = pipeline != NULL;
    WGPUTexture scratch = NULL, target = NULL;
    if (ok && draw->target_handle && draw->target_handle != DOE_RENDER_SCRATCH_HANDLE) {
        texture_entry *entry = runtime_find_texture(runtime, draw->target_handle);
        if (!entry) ok = runtime_fail(execution, "render_target_missing", "render target texture %llu does not exist",
            (unsigned long long)draw->target_handle);
        else target = entry->texture;
    } else if (ok) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
        descriptor.size = (WGPUExtent3D){draw->target_width, draw->target_height, 1};
        descriptor.format = runtime_webgpu_format(draw->target_format);
        target = scratch = wgpuDeviceCreateTexture(runtime->device, &descriptor);
        if (!scratch) ok = runtime_fail(execution, "render_target_failed", "the scratch render target was not created");
    }
    WGPUTexture temporary = NULL;
#ifdef __APPLE__
    /* Retained Metal native renderer stages every flagged draw, independent of
     * format/minimum mip. Vulkan/D3D12 native paths do not consume this flag. */
    if (ok && draw->temporary_render_texture) {
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        descriptor.size = (WGPUExtent3D){draw->target_width, draw->target_height, 1};
        descriptor.format = runtime_webgpu_format(draw->target_format);
        temporary = wgpuDeviceCreateTexture(runtime->device, &descriptor);
        if (!temporary) ok = runtime_fail(execution, "render_temporary_failed", "temporary Metal target creation failed");
    }
#endif
    WGPUTextureView view = ok ? wgpuTextureCreateView(temporary ? temporary : target, NULL) : NULL;
    if (ok && !view) ok = runtime_fail(execution, "render_view_failed", "render target view creation failed");
    uint64_t index_size = 0;
    WGPUBuffer indices = ok && draw->indexed ? runtime_render_indices(runtime, draw, &index_size) : NULL;
    if (ok && draw->indexed && !indices) ok = runtime_fail(execution, "render_index_buffer_failed", "the index buffer was not created");
    bool indirect = command->kind == DOE_COMMAND_DRAW_INDIRECT || command->kind == DOE_COMMAND_DRAW_INDEXED_INDIRECT;
    uint32_t stride = 0;
    WGPUBuffer arguments = ok && indirect ? runtime_render_arguments(runtime, draw, &stride, execution) : NULL;
    if (indirect && !arguments) ok = false;
    uint64_t setup_end = doe_runtime_monotonic_ns();
    uint64_t encode_end = setup_end;
    if (ok) {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = (WGPUColor){0, 0, 0, 1};
        WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        pass_descriptor.colorAttachmentCount = 1;
        pass_descriptor.colorAttachments = &color;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
        float viewport_width = draw->has_viewport_width ? draw->viewport_width : (float)draw->target_width;
        float viewport_height = draw->has_viewport_height ? draw->viewport_height : (float)draw->target_height;
        wgpuRenderPassEncoderSetViewport(pass, draw->viewport_x, draw->viewport_y, viewport_width, viewport_height,
            draw->viewport_min_depth, draw->viewport_max_depth);
        wgpuRenderPassEncoderSetScissorRect(pass, draw->scissor_x, draw->scissor_y,
            draw->has_scissor_width ? draw->scissor_width : draw->target_width,
            draw->has_scissor_height ? draw->scissor_height : draw->target_height);
        if (draw->has_blend_constant) {
            WGPUColor blend = {draw->blend[0], draw->blend[1], draw->blend[2], draw->blend[3]};
            wgpuRenderPassEncoderSetBlendConstant(pass, &blend);
        }
        if (draw->has_stencil_reference) wgpuRenderPassEncoderSetStencilReference(pass, draw->stencil_reference);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        if (indices) {
            wgpuRenderPassEncoderSetIndexBuffer(pass, indices, draw->index_uint32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                0, index_size);
        }
        for (uint32_t i = 0; i < draw->draw_count; i++) {
            if (arguments) {
                uint64_t offset = (uint64_t)i * stride;
                if (indices) wgpuRenderPassEncoderDrawIndexedIndirect(pass, arguments, offset);
                else wgpuRenderPassEncoderDrawIndirect(pass, arguments, offset);
            } else if (indices) {
                wgpuRenderPassEncoderDrawIndexed(pass, draw->index_count, draw->instance_count, draw->first_index,
                    draw->base_vertex, draw->first_instance);
            } else {
                wgpuRenderPassEncoderDraw(pass, draw->vertex_count, draw->instance_count, draw->first_vertex, draw->first_instance);
            }
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        if (temporary) runtime_render_copy_back(encoder, temporary, target, draw);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        encode_end = doe_runtime_monotonic_ns();
        ok = runtime_submit(runtime, commands, execution);
        execution->submit_count = 1;
        execution->dispatch_count = draw->draw_count;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (arguments) {
        wgpuBufferDestroy(arguments);
        wgpuBufferRelease(arguments);
    }
    if (indices) wgpuBufferRelease(indices);
    if (view) wgpuTextureViewRelease(view);
    if (temporary) wgpuTextureRelease(temporary);
    if (scratch) {
        wgpuTextureDestroy(scratch);
        wgpuTextureRelease(scratch);
    }
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = setup_end - start;
    execution->encode_ns = encode_end - setup_end;
    execution->submit_wait_ns = end - encode_end;
    return ok;
}

bool runtime_execute_sampler(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    if (command->kind == DOE_COMMAND_SAMPLER_DESTROY) {
        /* A sampler that was never created is not an error, as in doe-zig-runtime. */
        for (size_t i = 0; i < runtime->sampler_count; i++) {
            if (runtime->samplers[i].handle != command->as.sampler_destroy.handle) continue;
            wgpuSamplerRelease(runtime->samplers[i].sampler);
            runtime->samplers[i] = runtime->samplers[--runtime->sampler_count];
            break;
        }
        return true;
    }
    const doe_sampler_desc *s = &command->as.sampler;
    if (runtime->sampler_count == runtime->sampler_capacity) {
        size_t capacity = runtime->sampler_capacity ? runtime->sampler_capacity * 2 : 8;
        sampler_entry *grown = realloc(runtime->samplers, capacity * sizeof(*grown));
        if (!grown) return runtime_fail(execution, "out_of_memory", "out of memory");
        runtime->samplers = grown;
        runtime->sampler_capacity = capacity;
    }
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.addressModeU = (WGPUAddressMode)s->address_mode[0];
    descriptor.addressModeV = (WGPUAddressMode)s->address_mode[1];
    descriptor.addressModeW = (WGPUAddressMode)s->address_mode[2];
    descriptor.magFilter = (WGPUFilterMode)s->mag_filter;
    descriptor.minFilter = (WGPUFilterMode)s->min_filter;
    descriptor.mipmapFilter = (WGPUMipmapFilterMode)s->mipmap_filter;
    descriptor.lodMinClamp = s->lod_min_clamp;
    descriptor.lodMaxClamp = s->lod_max_clamp;
    descriptor.compare = (WGPUCompareFunction)s->compare;
    descriptor.maxAnisotropy = s->max_anisotropy;
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    WGPUSampler sampler = wgpuDeviceCreateSampler(runtime->device, &descriptor);
    bool ok = sampler != NULL || runtime_fail(execution, "sampler_create_failed", "sampler %llu was not created",
        (unsigned long long)s->handle);
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = doe_runtime_monotonic_ns() - start;
    if (!ok) {
        if (sampler) wgpuSamplerRelease(sampler);
        return false;
    }
    /* A second create with the same handle replaces the first sampler. */
    for (size_t i = 0; i < runtime->sampler_count; i++) {
        if (runtime->samplers[i].handle != s->handle) continue;
        wgpuSamplerRelease(runtime->samplers[i].sampler);
        runtime->samplers[i].sampler = sampler;
        return true;
    }
    runtime->samplers[runtime->sampler_count++] = (sampler_entry){s->handle, sampler};
    return true;
}
