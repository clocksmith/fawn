/* SPDX-License-Identifier: MIT
 * Command-stream parsing: the repository's example streams for the executed
 * kinds, and visible rejection of every kind, field and value not implemented.
 * argv[1] is the repository examples directory. */
#include "stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    char *text = malloc((size_t)length);
    assert(text && fread(text, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return text;
}

static void parse_file(const char *root, const char *name, doe_stream *stream)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    size_t size;
    char *text = slurp(path, &size);
    doe_stream_error error;
    bool ok = doe_stream_parse(text, size, stream, &error);
    if (!ok) fprintf(stderr, "%s: command %zu: %s\n", name, error.command_index, error.message);
    assert(ok);
    free(text);
}

static void rejects(const char *text, size_t index, const char *needle)
{
    doe_stream stream;
    doe_stream_error error;
    assert(!doe_stream_parse(text, strlen(text), &stream, &error));
    assert(stream.commands == NULL && stream.count == 0);
    if (error.command_index != index || !strstr(error.message, needle))
        fprintf(stderr, "%s -> [%zu] %s\n", text, error.command_index, error.message);
    assert(error.command_index == index && strstr(error.message, needle));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    doe_stream stream;
    parse_file(argv[1], "rmsnorm_tree_commands.json", &stream);
    assert(stream.count == 2);
    assert(stream.commands[0].kind == DOE_COMMAND_BUFFER_WRITE);
    assert(stream.commands[0].as.buffer_write.handle == 6001 && stream.commands[0].as.buffer_write.buffer_size == 16);
    assert(stream.commands[0].as.buffer_write.word_count == 4 && stream.commands[0].as.buffer_write.words[1] == 1065353216u);
    const doe_command *dispatch = &stream.commands[1];
    assert(dispatch->kind == DOE_COMMAND_KERNEL_DISPATCH && !strcmp(dispatch->as.dispatch.kernel, "rmsnorm.wgsl"));
    assert(!strcmp(dispatch->as.dispatch.entry_point, "main"));
    assert(dispatch->as.dispatch.x == 32 && dispatch->as.dispatch.y == 1 && dispatch->as.dispatch.repeat == 100);
    assert(dispatch->as.dispatch.warmup == 5 && dispatch->as.dispatch.initialize_buffers);
    assert(dispatch->as.dispatch.binding_count == 4);
    assert(dispatch->as.dispatch.bindings[0].buffer_type == DOE_BINDING_UNIFORM);
    assert(dispatch->as.dispatch.bindings[1].buffer_type == DOE_BINDING_READ_ONLY_STORAGE);
    assert(dispatch->as.dispatch.bindings[3].buffer_type == DOE_BINDING_STORAGE);
    assert(dispatch->as.dispatch.bindings[3].handle == 6004 && dispatch->as.dispatch.bindings[3].buffer_size == 524288);
    doe_stream_free(&stream);
    /* Semantic identity in both spellings, as doe-zig-runtime's command_stream.zig reads it. */
    parse_file(argv[1], "numeric-stability-decode-greedy.commands.json", &stream);
    assert(stream.count == 6);
    const doe_semantic *logits = &stream.commands[3].semantic;
    assert(!strcmp(logits->op_id, "decode.final_logits") && !strcmp(logits->stage, "decode_demo"));
    assert(!strcmp(logits->phase, "final_logits") && logits->has_token_index && logits->token_index == 0);
    assert(!logits->has_layer_index && !logits->execution_plan_hash && !logits->has_capture);
    assert(doe_semantic_present(logits) && !doe_semantic_present(&stream.commands[0].semantic));
    doe_stream_free(&stream);
    const char *snake = "[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semantic_op_id\":\"op\","
        "\"semantic_layer_index\":7,\"semantic_execution_plan_hash\":\"p\",\"capture_buffer_handle\":9,"
        "\"capture_offset\":4,\"capture_size\":8}]";
    doe_stream_error semantic_error;
    assert(doe_stream_parse(snake, strlen(snake), &stream, &semantic_error));
    const doe_semantic *snake_semantic = &stream.commands[0].semantic;
    assert(!strcmp(snake_semantic->op_id, "op") && !snake_semantic->stage && !snake_semantic->has_token_index);
    assert(snake_semantic->has_layer_index && snake_semantic->layer_index == 7);
    assert(!strcmp(snake_semantic->execution_plan_hash, "p") && snake_semantic->has_capture);
    assert(snake_semantic->capture_handle == 9 && snake_semantic->capture_offset == 4 && snake_semantic->capture_size == 8);
    doe_stream_free(&stream);
    parse_file(argv[1], "p0_resource_lifecycle_commands.json", &stream);
    assert(stream.count == 5 && stream.commands[3].kind == DOE_COMMAND_BARRIER);
    assert(stream.commands[3].as.barrier.dependency_count == 64);
    assert(stream.commands[0].as.upload.bytes == 1024 && stream.commands[0].as.upload.align_bytes == 4);
    doe_stream_free(&stream);
    parse_file(argv[1], "workgroup_atomic_commands.json", &stream); /* output_oracle is accepted, not verified */
    assert(stream.count == 1 && stream.commands[0].as.dispatch.repeat == 100);
    doe_stream_free(&stream);

    /* Copy directions come from the kind name, layouts and defaults as doe-zig-runtime parses them. */
    parse_file(argv[1], "copy_texture_to_buffer_padded_commands.json", &stream);
    const doe_command *copy = &stream.commands[0];
    assert(copy->kind == DOE_COMMAND_COPY && copy->as.copy.direction == DOE_COPY_TEXTURE_TO_BUFFER);
    assert(!strcmp(copy->label, "copy_buffer_to_texture") && copy->as.copy.bytes == 4096);
    assert(copy->as.copy.src.texture.width == 32 && copy->as.copy.src.texture.format == DOE_FORMAT_RGBA8_UNORM);
    assert(copy->as.copy.src.texture.usage == 6 && copy->as.copy.dst.bytes_per_row == 256 && copy->as.copy.src.bytes_per_row == 128);
    doe_stream_free(&stream);
    parse_file(argv[1], "copy_buffer_to_buffer_overlap_commands.json", &stream);
    assert(stream.commands[0].as.copy.direction == DOE_COPY_BUFFER_TO_BUFFER);
    assert(stream.commands[0].as.copy.src.offset == 1024 && stream.commands[0].as.copy.dst.offset == 2048);
    doe_stream_free(&stream);
    const char *textures = "[{\"kind\":\"texture_copy\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2},"
        "{\"kind\":\"copy_buffer_to_buffer\",\"direction\":\"texture_to_texture\",\"bytes\":4,\"src_handle\":3,\"dst_handle\":4},"
        "{\"kind\":\"write_texture\",\"handle\":5,\"data\":[1,2,3,255]},"
        "{\"kind\":\"query_texture\",\"textureHandle\":5,\"viewDimension\":\"2d-array\",\"usage\":6},"
        "{\"kind\":\"destroy_texture\",\"targetHandle\":5}]";
    assert(doe_stream_parse(textures, strlen(textures), &stream, &(doe_stream_error){0}));
    assert(stream.commands[0].as.copy.direction == DOE_COPY_BUFFER_TO_BUFFER); /* texture_copy is buffer_to_buffer */
    assert(stream.commands[1].as.copy.direction == DOE_COPY_TEXTURE_TO_TEXTURE);
    assert(stream.commands[1].as.copy.src.texture.format == DOE_FORMAT_R8_UNORM && stream.commands[1].as.copy.src.texture.width == 1);
    const doe_command *write = &stream.commands[2];
    assert(write->kind == DOE_COMMAND_TEXTURE_WRITE && !strcmp(write->label, "texture_write"));
    assert(write->as.texture_write.texture.format == DOE_FORMAT_RGBA8_UNORM && write->as.texture_write.texture.usage == 6);
    assert(write->as.texture_write.data_size == 4 && write->as.texture_write.data[3] == 255);
    const doe_texture_query *query = &stream.commands[3].as.texture_query;
    assert(query->handle == 5 && query->has_view_dimension && query->view_dimension == 1 && query->has_usage && !query->has_width);
    assert(stream.commands[4].kind == DOE_COMMAND_TEXTURE_DESTROY && stream.commands[4].as.texture_destroy.handle == 5);
    doe_stream_free(&stream);

    /* Render and sampler commands with doe-zig-runtime defaults. */
    parse_file(argv[1], "draw_call_indexed_proxy_commands.json", &stream);
    const doe_render_draw *indexed = &stream.commands[0].as.render;
    assert(stream.commands[0].kind == DOE_COMMAND_RENDER_DRAW && !strcmp(stream.commands[0].label, "render_draw"));
    assert(indexed->indexed && !indexed->index_uint32 && indexed->index_count == 3 && indexed->draw_count == 2000);
    assert(indexed->target_handle == 0 && indexed->target_width == 64 && indexed->target_format == DOE_FORMAT_RGBA8_UNORM);
    doe_stream_free(&stream);
    parse_file(argv[1], "draw_call_state_bindings_commands.json", &stream);
    const doe_render_draw *state = &stream.commands[0].as.render;
    assert(!state->indexed && state->has_scissor_width && state->scissor_width == 64 && state->has_stencil_reference);
    assert(state->has_viewport_width && state->viewport_max_depth == 1.0f && state->has_blend_constant);
    doe_stream_free(&stream);
    parse_file(argv[1], "texture_sampler_write_query_destroy_commands.json", &stream);
    const doe_sampler_desc *sampler = &stream.commands[0].as.sampler;
    assert(stream.commands[0].kind == DOE_COMMAND_SAMPLER_CREATE && sampler->handle == 9100 && sampler->mag_filter == 2);
    assert(sampler->address_mode[0] == 1 && sampler->lod_max_clamp == 32.0f && sampler->max_anisotropy == 1);
    assert(stream.commands[3].kind == DOE_COMMAND_RENDER_DRAW && stream.commands[5].kind == DOE_COMMAND_SAMPLER_DESTROY);
    doe_stream_free(&stream);

    /* Aliases label rows with the normalized kind; kinds compare case-insensitively like the Zig parser. */
    const char *alias = "{\"commands\":[{\"command\":\"Write_Buffer\",\"handle\":1,\"data\":[7]}]}";
    assert(doe_stream_parse(alias, strlen(alias), &stream, &(doe_stream_error){0}));
    assert(stream.commands[0].kind == DOE_COMMAND_BUFFER_WRITE && !strcmp(stream.commands[0].label, "buffer_write"));
    assert(!strcmp(doe_command_tag(stream.commands[0].kind), "buffer_write"));
    doe_stream_free(&stream);

    rejects("{}", SIZE_MAX, "array");
    rejects("[1", SIZE_MAX, "syntax");
    rejects("[{\"kind\":\"draw_indirect\"}]", 0, "not implemented");
    rejects("[{\"kind\":\"upload\",\"bytes\":4},{\"kind\":\"map_async\",\"bytes\":4}]", 1, "map_async");
    rejects("[{\"kind\":\"upload\",\"bytes\":4,\"extra\":1}]", 0, "\"extra\"");
    rejects("[{\"kind\":\"upload\"}]", 0, "\"bytes\" is required");
    rejects("[{\"kind\":\"upload\",\"bytes\":4,\"alignBytes\":3}]", 0, "multiple of 4");
    rejects("[{\"kind\":\"upload\",\"command\":\"upload\",\"bytes\":4}]", 0, "more than once");
    rejects("[{\"kind\":\"buffer_write\",\"handle\":1,\"data\":[4294967296]}]", 0, "data");
    rejects("[{\"kind\":\"buffer_write\",\"handle\":1,\"resourceHandle\":2,\"data\":[1]}]", 0, "one integer");
    rejects("[{\"kind\":\"buffer_write\",\"handle\":1,\"offset\":2,\"data\":[1]}]", 0, "offset");
    rejects("[{\"kind\":\"kernel_dispatch\",\"x\":1}]", 0, "kernel");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"x\":0}]", 0, "nonzero");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"repeatSynchronization\":\"independent\"}]", 0, "independent");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semanticOpId\":\"a\",\"semantic_op_id\":\"b\"}]", 0, "both");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semanticTokenIndex\":4294967296}]", 0, "semantic_token_index");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semanticOpId\":\"op\",\"captureBufferHandle\":1}]", 0, "both");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semanticOpId\":\"op\",\"captureBufferHandle\":1,"
        "\"captureSize\":0}]", 0, "nonzero");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"captureBufferHandle\":1,\"captureSize\":4}]", 0,
        "semantic operation");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semanticOpId\":\"op\",\"numericStability\":{}}]", 0,
        "numericStability");
    rejects("[{\"kind\":\"buffer_write\",\"handle\":1,\"data\":[1],\"semanticOpId\":\"op\"}]", 0, "semanticOpId");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"kind\":\"texture\","
        "\"resource_handle\":1}]}]", 0, "buffers only");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"resource_handle\":1}]}]",
        0, "buffer_type");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"resource_handle\":1,"
        "\"buffer_type\":\"storage\"},{\"binding\":0,\"resource_handle\":2,\"buffer_type\":\"storage\"}]}]", 0, "repeat group");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"resource_handle\":1,"
        "\"buffer_type\":\"storage\",\"visibility\":\"fragment\"}]}]", 0, "compute");
    rejects("[{\"kind\":\"copy_buffer_to_buffer\",\"src_handle\":1,\"dst_handle\":2}]", 0, "\"bytes\" is required");
    rejects("[{\"kind\":\"copy_buffer_to_buffer\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2,\"direction\":\"up\"}]", 0, "direction");
    rejects("[{\"kind\":\"copy_buffer_to_buffer\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2,\"src_width\":4}]", 0, "buffer side");
    rejects("[{\"kind\":\"copy_texture_to_texture\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2,\"dst_mip_level\":1}]", 0, "mip_level");
    rejects("[{\"kind\":\"copy_texture_to_texture\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2,\"src_format\":\"bc1-rgba-unorm\"}]", 0, "format");
    rejects("[{\"kind\":\"copy_texture_to_buffer\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2,\"src_kind\":\"buffer\"}]", 0, "contradicts");
    rejects("[{\"kind\":\"copy_buffer_to_texture\",\"bytes\":4,\"src_handle\":1,\"dst_handle\":2,\"dst_offset\":4}]", 0, "texture side");
    rejects("[{\"kind\":\"texture_write\",\"handle\":1,\"bytes_per_row\":8}]", 0, "\"bytes_per_row\"");
    rejects("[{\"kind\":\"texture_write\",\"handle\":1,\"dimension\":\"3d\"}]", 0, "dimension");
    rejects("[{\"kind\":\"texture_write\",\"handle\":1,\"data\":[256]}]", 0, "[0, 255]");
    rejects("[{\"kind\":\"texture_write\",\"handle\":0}]", 0, "nonzero integer texture handle");
    rejects("[{\"kind\":\"texture_query\",\"handle\":1,\"textureHandle\":1}]", 0, "more than once");
    rejects("[{\"kind\":\"render_draw\"}]", 0, "\"draw_count\" is required");
    rejects("[{\"kind\":\"render_draw\",\"drawCount\":1,\"pipelineMode\":\"dynamic\"}]", 0, "pipeline_mode");
    rejects("[{\"kind\":\"render_pass\",\"drawCount\":1,\"viewportMinDepth\":0.5,\"viewportMaxDepth\":0.25}]", 0, "depths");
    rejects("[{\"kind\":\"draw_indexed\",\"drawCount\":1,\"indexData\":[0,1],\"indexCount\":3}]", 0, "exceeds");
    rejects("[{\"kind\":\"draw_indexed\",\"drawCount\":1}]", 0, "index_data");
    rejects("[{\"kind\":\"render_draw\",\"drawCount\":1,\"indirectBufferHandle\":5}]", 0, "\"indirectBufferHandle\"");
    rejects("[{\"kind\":\"render_draw\",\"drawCount\":1,\"targetFormat\":\"depth24plus\"}]", 0, "target format");
    rejects("[{\"kind\":\"sampler_create\",\"handle\":1,\"magFilter\":\"cubic\"}]", 0, "mag_filter");
    rejects("[{\"kind\":\"sampler_create\",\"handle\":1,\"maxAnisotropy\":0}]", 0, "max_anisotropy");
    puts("runtime-cli stream: nine example streams, semantic fields, aliases, copy, texture, render and sampler kinds and 46 visible "
        "rejections (host only)");
    return 0;
}
