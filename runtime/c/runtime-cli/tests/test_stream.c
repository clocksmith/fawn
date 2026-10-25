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
    parse_file(argv[1], "p0_resource_lifecycle_commands.json", &stream);
    assert(stream.count == 5 && stream.commands[3].kind == DOE_COMMAND_BARRIER);
    assert(stream.commands[3].as.barrier.dependency_count == 64);
    assert(stream.commands[0].as.upload.bytes == 1024 && stream.commands[0].as.upload.align_bytes == 4);
    doe_stream_free(&stream);
    parse_file(argv[1], "workgroup_atomic_commands.json", &stream); /* output_oracle is accepted, not verified */
    assert(stream.count == 1 && stream.commands[0].as.dispatch.repeat == 100);
    doe_stream_free(&stream);

    /* Aliases keep their label; kinds compare case-insensitively like the Zig parser. */
    const char *alias = "{\"commands\":[{\"command\":\"Write_Buffer\",\"handle\":1,\"data\":[7]}]}";
    assert(doe_stream_parse(alias, strlen(alias), &stream, &(doe_stream_error){0}));
    assert(stream.commands[0].kind == DOE_COMMAND_BUFFER_WRITE && !strcmp(stream.commands[0].label, "Write_Buffer"));
    assert(!strcmp(doe_command_tag(stream.commands[0].kind), "buffer_write"));
    doe_stream_free(&stream);

    rejects("{}", SIZE_MAX, "array");
    rejects("[1", SIZE_MAX, "syntax");
    rejects("[{\"kind\":\"render_draw\"}]", 0, "not implemented");
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
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"semanticOpId\":\"op\"}]", 0, "semanticOpId");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"kind\":\"texture\","
        "\"resource_handle\":1}]}]", 0, "buffers only");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"resource_handle\":1}]}]",
        0, "buffer_type");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"resource_handle\":1,"
        "\"buffer_type\":\"storage\"},{\"binding\":0,\"resource_handle\":2,\"buffer_type\":\"storage\"}]}]", 0, "repeat group");
    rejects("[{\"kind\":\"kernel_dispatch\",\"kernel\":\"k\",\"bindings\":[{\"binding\":0,\"resource_handle\":1,"
        "\"buffer_type\":\"storage\",\"visibility\":\"fragment\"}]}]", 0, "compute");
    puts("runtime-cli stream: three example streams, aliases and 19 visible rejections (host only)");
    return 0;
}
