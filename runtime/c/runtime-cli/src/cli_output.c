/* SPDX-License-Identifier: MIT
 * Retained runtime/trace/command_output.zig normalized output contract. */
#include "cli_output.h"
#include "runtime_internal.h"
#include "trace.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool write_words(FILE *out, const uint32_t *words, size_t count)
{
    if (fputc('[', out) == EOF) return false;
    for (size_t i = 0; i < count; i++)
        if (fprintf(out, "%s%" PRIu32, i ? "," : "", words[i]) < 0) return false;
    return fputc(']', out) != EOF;
}

/* Retained Zig prints the shortest decimal that round-trips to the source f32. */
static bool write_float(FILE *out, const char *field, float value)
{
    char text[64];
    for (int precision = 1; precision <= 9; precision++) {
        int length = snprintf(text, sizeof(text), "%.*g", precision, (double)value);
        if (length < 0 || (size_t)length >= sizeof(text)) return false;
        float parsed = strtof(text, NULL);
        if (!memcmp(&parsed, &value, sizeof(value))) break;
    }
    return fprintf(out, ",\"%s\":%s", field, text) >= 0;
}

static bool write_render(FILE *out, const doe_render_draw *draw)
{
    if (fprintf(out, ",\"drawCount\":%u,\"vertexCount\":%u,\"instanceCount\":%u,\"firstVertex\":%u,\"firstInstance\":%u",
            draw->draw_count, draw->vertex_count, draw->instance_count, draw->first_vertex, draw->first_instance) < 0)
        return false;
    if (draw->indexed) {
        if (fprintf(out, ",\"indexCount\":%u,\"firstIndex\":%u,\"baseVertex\":%d,\"indexFormat\":\"%s\",\"indexData\":",
                draw->index_count, draw->first_index, draw->base_vertex, draw->index_uint32 ? "uint32" : "uint16") < 0 ||
            !write_words(out, draw->indices, draw->index_data_count)) return false;
    }
    if (fprintf(out, ",\"targetHandle\":%" PRIu64 ",\"targetWidth\":%u,\"targetHeight\":%u,\"targetFormat\":%u,"
            "\"pipelineMode\":\"%s\",\"bindGroupMode\":\"%s\",\"encodeMode\":\"%s\"",
            draw->target_handle, draw->target_width, draw->target_height, (unsigned)runtime_webgpu_format(draw->target_format),
            draw->redundant_pipeline ? "redundant" : "static", draw->redundant_bind_group ? "redundant" : "no_change",
            draw->render_pass ? "render_pass" : "render_bundle") < 0)
        return false;
    if (!write_float(out, "viewportX", draw->viewport_x) || !write_float(out, "viewportY", draw->viewport_y) ||
        (draw->has_viewport_width && !write_float(out, "viewportWidth", draw->viewport_width)) ||
        (draw->has_viewport_height && !write_float(out, "viewportHeight", draw->viewport_height)) ||
        !write_float(out, "viewportMinDepth", draw->viewport_min_depth) ||
        !write_float(out, "viewportMaxDepth", draw->viewport_max_depth) ||
        fprintf(out, ",\"scissorX\":%u,\"scissorY\":%u", draw->scissor_x, draw->scissor_y) < 0) return false;
    if (draw->has_scissor_width && fprintf(out, ",\"scissorWidth\":%u", draw->scissor_width) < 0) return false;
    if (draw->has_scissor_height && fprintf(out, ",\"scissorHeight\":%u", draw->scissor_height) < 0) return false;
    if (fprintf(out, ",\"blend\":[%.17g,%.17g,%.17g,%.17g],\"stencilReference\":%u", draw->blend[0], draw->blend[1],
            draw->blend[2], draw->blend[3], draw->stencil_reference) < 0) return false;
    return !draw->has_dynamic_offsets || (fputs(",\"bindGroupDynamicOffsets\":", out) >= 0 &&
        write_words(out, draw->dynamic_offsets, draw->dynamic_offset_count));
}

static bool write_fields(FILE *out, const doe_command *command)
{
    switch (command->kind) {
    case DOE_COMMAND_UPLOAD:
        return fprintf(out, ",\"bytes\":%" PRIu64 ",\"alignBytes\":%" PRIu64,
            command->as.upload.bytes, command->as.upload.align_bytes) >= 0;
    case DOE_COMMAND_BUFFER_WRITE:
        return fprintf(out, ",\"handle\":%" PRIu64 ",\"offset\":%" PRIu64 ",\"bufferSize\":%" PRIu64 ",\"data\":",
            command->as.buffer_write.handle, command->as.buffer_write.offset, command->as.buffer_write.buffer_size) >= 0 &&
            write_words(out, command->as.buffer_write.words, command->as.buffer_write.word_count);
    case DOE_COMMAND_BARRIER:
        return fprintf(out, ",\"dependencyCount\":%" PRIu64, command->as.barrier.dependency_count) >= 0;
    case DOE_COMMAND_KERNEL_DISPATCH:
        return doe_trace_write_dispatch_fields(out, command);
    case DOE_COMMAND_COPY: {
        const char *direction = NULL;
        switch (command->as.copy.direction) {
        case DOE_COPY_BUFFER_TO_BUFFER: direction = "buffer_to_buffer"; break;
        case DOE_COPY_BUFFER_TO_TEXTURE: direction = "buffer_to_texture"; break;
        case DOE_COPY_TEXTURE_TO_BUFFER: direction = "texture_to_buffer"; break;
        case DOE_COPY_TEXTURE_TO_TEXTURE: direction = "texture_to_texture"; break;
        }
        return fprintf(out, ",\"direction\":\"%s\",\"srcHandle\":%" PRIu64 ",\"dstHandle\":%" PRIu64
            ",\"bytes\":%" PRIu64 ",\"usesTemporaryBuffer\":%s,\"temporaryBufferAlignment\":%u", direction,
            command->as.copy.src.handle, command->as.copy.dst.handle, command->as.copy.bytes,
            command->as.copy.temporary_buffer ? "true" : "false", command->as.copy.temporary_buffer_alignment) >= 0;
    }
    case DOE_COMMAND_TEXTURE_WRITE:
        return fprintf(out, ",\"handle\":%" PRIu64 ",\"width\":%u,\"height\":%u,\"dataBytes\":%zu",
            command->as.texture_write.texture.handle, command->as.texture_write.texture.width,
            command->as.texture_write.texture.height, command->as.texture_write.data_size) >= 0;
    case DOE_COMMAND_TEXTURE_QUERY: {
        const doe_texture_query *query = &command->as.texture_query;
        return fprintf(out, ",\"handle\":%" PRIu64, query->handle) >= 0 &&
            (!query->has_width || fprintf(out, ",\"expectedWidth\":%u", query->width) >= 0) &&
            (!query->has_height || fprintf(out, ",\"expectedHeight\":%u", query->height) >= 0) &&
            (!query->has_depth || fprintf(out, ",\"expectedDepthOrArrayLayers\":%u", query->depth) >= 0);
    }
    case DOE_COMMAND_TEXTURE_DESTROY:
        return fprintf(out, ",\"handle\":%" PRIu64, command->as.texture_destroy.handle) >= 0;
    case DOE_COMMAND_RENDER_DRAW:
    case DOE_COMMAND_RENDER_PASS:
    case DOE_COMMAND_DRAW_INDIRECT:
    case DOE_COMMAND_DRAW_INDEXED_INDIRECT:
        return write_render(out, &command->as.render);
    case DOE_COMMAND_SAMPLER_CREATE:
        return fprintf(out, ",\"handle\":%" PRIu64, command->as.sampler.handle) >= 0;
    case DOE_COMMAND_SAMPLER_DESTROY:
        return fprintf(out, ",\"handle\":%" PRIu64, command->as.sampler_destroy.handle) >= 0;
    case DOE_COMMAND_DISPATCH:
    case DOE_COMMAND_DISPATCH_INDIRECT:
        return fprintf(out, ",\"x\":%u,\"y\":%u,\"z\":%u", command->as.grid.x, command->as.grid.y, command->as.grid.z) >= 0;
    case DOE_COMMAND_MAP_ASYNC:
        return fprintf(out, ",\"mode\":\"%s\",\"bytes\":%" PRIu64, command->as.map.write ? "write" : "read",
            command->as.map.bytes) >= 0;
    case DOE_COMMAND_ASYNC_DIAGNOSTICS:
        return fprintf(out, ",\"targetFormat\":%u,\"mode\":\"%s\",\"iterations\":%u",
            (unsigned)runtime_webgpu_format(command->as.diagnostics.format),
            doe_diagnostic_mode_name(command->as.diagnostics.mode), command->as.diagnostics.iterations) >= 0;
    case DOE_COMMAND_SURFACE_CONFIGURE:
        return fprintf(out, ",\"handle\":%" PRIu64 ",\"width\":%u,\"height\":%u", command->as.surface.handle,
            command->as.surface.width, command->as.surface.height) >= 0;
    case DOE_COMMAND_SURFACE_CREATE:
    case DOE_COMMAND_SURFACE_CAPABILITIES:
    case DOE_COMMAND_SURFACE_ACQUIRE:
    case DOE_COMMAND_SURFACE_PRESENT:
    case DOE_COMMAND_SURFACE_UNCONFIGURE:
    case DOE_COMMAND_SURFACE_RELEASE:
        return fprintf(out, ",\"handle\":%" PRIu64, command->as.surface.handle) >= 0;
    }
    return false;
}

bool cli_write_normalized(FILE *out, uint64_t seq, const doe_command *command)
{
    return fprintf(out, "{\"seq\":%" PRIu64 ",\"kind\":", seq) >= 0 && doe_trace_write_string(out, command->label) &&
        write_fields(out, command) && fputs("}\n", out) >= 0;
}
