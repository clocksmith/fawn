/* SPDX-License-Identifier: MIT */
#include "stream_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct kind_name {
    const char *name;
    doe_command_kind kind;
} kind_name;

/* The doe-zig-runtime aliases of the kinds executed here (command_kind.zig). */
static const kind_name KINDS[] = {
    {"upload", DOE_COMMAND_UPLOAD},
    {"buffer_upload", DOE_COMMAND_UPLOAD},
    {"buffer_write", DOE_COMMAND_BUFFER_WRITE},
    {"write_buffer", DOE_COMMAND_BUFFER_WRITE},
    {"queue_write_buffer", DOE_COMMAND_BUFFER_WRITE},
    {"barrier", DOE_COMMAND_BARRIER},
    {"kernel_dispatch", DOE_COMMAND_KERNEL_DISPATCH},
    {"dispatch", DOE_COMMAND_DISPATCH},
    {"dispatch_workgroups", DOE_COMMAND_DISPATCH},
    {"dispatch_invocations", DOE_COMMAND_DISPATCH},
    {"dispatch_indirect", DOE_COMMAND_DISPATCH_INDIRECT},
    {"copy_buffer_to_texture", DOE_COMMAND_COPY},
    {"copy_texture", DOE_COMMAND_COPY},
    {"texture_copy", DOE_COMMAND_COPY},
    {"copy_texture_to_buffer", DOE_COMMAND_COPY},
    {"copy_buffer_to_buffer", DOE_COMMAND_COPY},
    {"buffer_copy", DOE_COMMAND_COPY},
    {"copyBufferToTexture", DOE_COMMAND_COPY},
    {"copyTextureToBuffer", DOE_COMMAND_COPY},
    {"copyBufferToBuffer", DOE_COMMAND_COPY},
    {"copy_texture_to_texture", DOE_COMMAND_COPY},
    {"texture_write", DOE_COMMAND_TEXTURE_WRITE},
    {"write_texture", DOE_COMMAND_TEXTURE_WRITE},
    {"queue_write_texture", DOE_COMMAND_TEXTURE_WRITE},
    {"texture_query", DOE_COMMAND_TEXTURE_QUERY},
    {"query_texture", DOE_COMMAND_TEXTURE_QUERY},
    {"texture_destroy", DOE_COMMAND_TEXTURE_DESTROY},
    {"destroy_texture", DOE_COMMAND_TEXTURE_DESTROY},
    {"render_draw", DOE_COMMAND_RENDER_DRAW},
    {"draw", DOE_COMMAND_RENDER_DRAW},
    {"draw_call", DOE_COMMAND_RENDER_DRAW},
    {"draw_indexed", DOE_COMMAND_RENDER_DRAW},
    {"render_pass", DOE_COMMAND_RENDER_PASS},
    {"sampler_create", DOE_COMMAND_SAMPLER_CREATE},
    {"create_sampler", DOE_COMMAND_SAMPLER_CREATE},
    {"sampler_destroy", DOE_COMMAND_SAMPLER_DESTROY},
    {"destroy_sampler", DOE_COMMAND_SAMPLER_DESTROY},
};

const char *doe_command_tag(doe_command_kind kind)
{
    switch (kind) {
    case DOE_COMMAND_UPLOAD: return "upload";
    case DOE_COMMAND_BUFFER_WRITE: return "buffer_write";
    case DOE_COMMAND_BARRIER: return "barrier";
    case DOE_COMMAND_KERNEL_DISPATCH: return "kernel_dispatch";
    case DOE_COMMAND_COPY: return "copy_buffer_to_texture";
    case DOE_COMMAND_TEXTURE_WRITE: return "texture_write";
    case DOE_COMMAND_TEXTURE_QUERY: return "texture_query";
    case DOE_COMMAND_TEXTURE_DESTROY: return "texture_destroy";
    case DOE_COMMAND_RENDER_DRAW: return "render_draw";
    case DOE_COMMAND_RENDER_PASS: return "render_pass";
    case DOE_COMMAND_SAMPLER_CREATE: return "sampler_create";
    case DOE_COMMAND_SAMPLER_DESTROY: return "sampler_destroy";
    case DOE_COMMAND_DISPATCH: return "dispatch";
    case DOE_COMMAND_DISPATCH_INDIRECT: return "dispatch_indirect";
    }
    return "unknown";
}

bool stream_reject(doe_stream_error *error, size_t index, const char *format, ...)
{
    error->command_index = index;
    va_list args;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    return false;
}

/* Every member must be one of `allowed` (NULL-terminated): a field this runtime
 * does not implement is a rejection, never a silently ignored option. */
bool stream_only_members(const doe_json_value *object, const char *const *allowed, size_t index,
    const char *what, doe_stream_error *error)
{
    for (size_t i = 0; i < object->count; i++) {
        const char *key = object->members[i].key;
        bool known = false;
        for (const char *const *name = allowed; *name; name++) {
            if (strcmp(key, *name) == 0) { known = true; break; }
        }
        if (!known) return stream_reject(error, index, "%s field \"%s\" is not implemented by doe-c-runtime", what, key);
    }
    return true;
}

/* The first present of two spellings; both present is ambiguous. */
const doe_json_value *stream_either(const doe_json_value *object, const char *a, const char *b, bool *ambiguous)
{
    const doe_json_value *first = doe_json_member_value(object, a);
    const doe_json_value *second = b ? doe_json_member_value(object, b) : NULL;
    if (first && second) *ambiguous = true;
    return first ? first : second;
}

/* One of three spellings; more than one present is ambiguous. */
const doe_json_value *stream_one_of(const doe_json_value *object, const char *a, const char *b, const char *c,
    bool *ambiguous)
{
    const char *names[3] = {a, b, c};
    const doe_json_value *found = NULL;
    for (int i = 0; i < 3; i++) {
        if (!names[i]) continue;
        const doe_json_value *value = doe_json_member_value(object, names[i]);
        if (!value) continue;
        if (found) *ambiguous = true;
        found = value;
    }
    return found;
}

bool stream_read_u64(const doe_json_value *object, const char *a, const char *b, uint64_t max, bool required,
    uint64_t fallback, uint64_t *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, a, b, &ambiguous);
    if (ambiguous) return stream_reject(error, index, "both \"%s\" and \"%s\" are present", a, b);
    if (!value) {
        if (required) return stream_reject(error, index, "\"%s\" is required", a);
        *out = fallback;
        return true;
    }
    if (!doe_json_as_u64(value, max, out)) return stream_reject(error, index, "\"%s\" must be an integer in [0, %llu]", a, (unsigned long long)max);
    return true;
}

bool stream_read_string(const doe_json_value *object, const char *a, const char *b, const char **out,
    size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, a, b, &ambiguous);
    if (ambiguous) return stream_reject(error, index, "both \"%s\" and \"%s\" are present", a, b);
    if (!value) { *out = NULL; return true; }
    if (value->kind != DOE_JSON_STRING || value->string_size == 0 || strlen(value->string) != value->string_size)
        return stream_reject(error, index, "\"%s\" must be a nonempty string", a);
    *out = value->string;
    return true;
}

static bool parse_upload(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "bytes", "alignBytes", "alignmentBytes", NULL};
    if (!stream_only_members(object, allowed, index, "upload", error)) return false;
    if (!stream_read_u64(object, "bytes", NULL, UINT64_MAX, true, 0, &command->as.upload.bytes, index, error)) return false;
    if (!stream_read_u64(object, "alignBytes", "alignmentBytes", UINT32_MAX, false, 4, &command->as.upload.align_bytes, index, error)) return false;
    if (command->as.upload.bytes == 0) return stream_reject(error, index, "upload \"bytes\" must be nonzero");
    if (command->as.upload.align_bytes == 0 || command->as.upload.align_bytes % 4 != 0)
        return stream_reject(error, index, "upload \"alignBytes\" must be a nonzero multiple of 4");
    return true;
}

static bool parse_buffer_write(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "handle", "resource_handle",
        "resourceHandle", "offset", "buffer_size", "bufferSize", "data", NULL};
    if (!stream_only_members(object, allowed, index, "buffer_write", error)) return false;
    bool ambiguous = false;
    const doe_json_value *handle = stream_one_of(object, "handle", "resource_handle", "resourceHandle", &ambiguous);
    if (ambiguous || !handle || !doe_json_as_u64(handle, UINT64_MAX, &command->as.buffer_write.handle))
        return stream_reject(error, index, "buffer_write needs one integer \"handle\"");
    if (!stream_read_u64(object, "offset", NULL, UINT64_MAX, false, 0, &command->as.buffer_write.offset, index, error)) return false;
    if (!stream_read_u64(object, "buffer_size", "bufferSize", UINT64_MAX, false, 0, &command->as.buffer_write.buffer_size, index, error)) return false;
    const doe_json_value *data = doe_json_member_value(object, "data");
    if (!data || data->kind != DOE_JSON_ARRAY || data->count == 0)
        return stream_reject(error, index, "buffer_write \"data\" must be a nonempty array of u32 words");
    if (command->as.buffer_write.offset % 4 != 0) return stream_reject(error, index, "buffer_write \"offset\" must be a multiple of 4");
    uint32_t *words = calloc(data->count, sizeof(uint32_t));
    if (!words) return stream_reject(error, index, "out of memory");
    for (size_t i = 0; i < data->count; i++) {
        uint64_t word;
        if (!doe_json_as_u64(data->items[i], UINT32_MAX, &word)) {
            free(words);
            return stream_reject(error, index, "buffer_write \"data\"[%zu] must be an integer in [0, 4294967295]", i);
        }
        words[i] = (uint32_t)word;
    }
    command->as.buffer_write.words = words;
    command->as.buffer_write.word_count = data->count;
    return true;
}

static bool parse_barrier(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "dependency_count", "dependencyCount", NULL};
    if (!stream_only_members(object, allowed, index, "barrier", error)) return false;
    return stream_read_u64(object, "dependency_count", "dependencyCount", UINT64_MAX, false, 0,
        &command->as.barrier.dependency_count, index, error);
}

static bool parse_binding(const doe_json_value *object, doe_kernel_binding *out, size_t index, size_t position,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"binding", "group", "group_index", "groupIndex", "kind", "resource_kind",
        "resourceKind", "handle", "resource_handle", "resourceHandle", "visibility", "buffer_offset", "bufferOffset",
        "buffer_size", "bufferSize", "buffer_type", "bufferType", NULL};
    if (object->kind != DOE_JSON_OBJECT) return stream_reject(error, index, "bindings[%zu] must be an object", position);
    if (!stream_only_members(object, allowed, index, "kernel_dispatch binding", error)) return false;
    uint64_t value;
    if (!stream_read_u64(object, "binding", NULL, UINT32_MAX, true, 0, &value, index, error)) return false;
    out->binding = (uint32_t)value;
    bool ambiguous = false;
    const doe_json_value *group = stream_one_of(object, "group", "group_index", "groupIndex", &ambiguous);
    if (ambiguous || (group && !doe_json_as_u64(group, UINT32_MAX, &value)))
        return stream_reject(error, index, "bindings[%zu] \"group\" must be one integer", position);
    out->group = group ? (uint32_t)value : 0;
    const doe_json_value *handle = stream_one_of(object, "handle", "resource_handle", "resourceHandle", &ambiguous);
    if (ambiguous || !handle || !doe_json_as_u64(handle, UINT64_MAX, &out->handle))
        return stream_reject(error, index, "bindings[%zu] needs one integer \"resource_handle\"", position);
    const char *kind = NULL, *kind_camel = NULL;
    if (!stream_read_string(object, "kind", "resource_kind", &kind, index, error)) return false;
    if (!stream_read_string(object, "resourceKind", NULL, &kind_camel, index, error)) return false;
    if (kind && kind_camel) return stream_reject(error, index, "bindings[%zu] names its kind more than once", position);
    if (!kind) kind = kind_camel;
    if (kind && strcasecmp(kind, "buffer") != 0)
        return stream_reject(error, index, "bindings[%zu] kind \"%s\" is not implemented by doe-c-runtime (buffers only)", position, kind);
    const char *visibility = NULL;
    if (!stream_read_string(object, "visibility", NULL, &visibility, index, error)) return false;
    if (visibility && strcasecmp(visibility, "compute") != 0)
        return stream_reject(error, index, "bindings[%zu] visibility must be \"compute\"", position);
    const char *type = NULL;
    if (!stream_read_string(object, "buffer_type", "bufferType", &type, index, error)) return false;
    if (!type) return stream_reject(error, index, "bindings[%zu] needs \"buffer_type\" (uniform, readonly or storage)", position);
    if (strcasecmp(type, "uniform") == 0) out->buffer_type = DOE_BINDING_UNIFORM;
    else if (strcasecmp(type, "storage") == 0) out->buffer_type = DOE_BINDING_STORAGE;
    else if (strcasecmp(type, "readonly") == 0 || strcasecmp(type, "read_only_storage") == 0) out->buffer_type = DOE_BINDING_READ_ONLY_STORAGE;
    else return stream_reject(error, index, "bindings[%zu] buffer_type \"%s\" is not uniform, readonly or storage", position, type);
    if (!stream_read_u64(object, "buffer_offset", "bufferOffset", UINT64_MAX, false, 0, &out->buffer_offset, index, error)) return false;
    return stream_read_u64(object, "buffer_size", "bufferSize", UINT64_MAX, false, UINT64_MAX, &out->buffer_size, index, error);
}

bool doe_semantic_present(const doe_semantic *semantic)
{
    return semantic->op_id || semantic->stage || semantic->phase || semantic->execution_plan_hash ||
        semantic->has_token_index || semantic->has_layer_index;
}

static bool parse_optional_u32(const doe_json_value *object, const char *a, const char *b, bool *has, uint32_t *out,
    size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    *has = stream_either(object, a, b, &ambiguous) != NULL;
    uint64_t value = 0;
    if (!stream_read_u64(object, a, b, UINT32_MAX, false, 0, &value, index, error)) return false;
    *out = (uint32_t)value;
    return true;
}

/* doe-zig-runtime's semantic metadata (command_stream.zig): snake_case and
 * camelCase spellings, one of each pair. A capture names a handle and a nonzero
 * size together and belongs to a semantic operation; doe-zig-runtime drops a
 * partial or unattached capture, which this runtime rejects instead. */
static bool parse_semantic(const doe_json_value *object, doe_semantic *out, size_t index, doe_stream_error *error)
{
    if (!stream_read_string(object, "semantic_op_id", "semanticOpId", &out->op_id, index, error) ||
        !stream_read_string(object, "semantic_stage", "semanticStage", &out->stage, index, error) ||
        !stream_read_string(object, "semantic_phase", "semanticPhase", &out->phase, index, error) ||
        !stream_read_string(object, "semantic_execution_plan_hash", "semanticExecutionPlanHash",
            &out->execution_plan_hash, index, error) ||
        !parse_optional_u32(object, "semantic_token_index", "semanticTokenIndex", &out->has_token_index,
            &out->token_index, index, error) ||
        !parse_optional_u32(object, "semantic_layer_index", "semanticLayerIndex", &out->has_layer_index,
            &out->layer_index, index, error)) return false;
    bool ambiguous = false;
    bool has_handle = stream_either(object, "capture_buffer_handle", "captureBufferHandle", &ambiguous) != NULL;
    bool has_size = stream_either(object, "capture_size", "captureSize", &ambiguous) != NULL;
    bool has_offset = stream_either(object, "capture_offset", "captureOffset", &ambiguous) != NULL;
    if (!has_handle && !has_size && !has_offset) return true;
    if (!has_handle || !has_size)
        return stream_reject(error, index, "a capture needs both \"captureBufferHandle\" and \"captureSize\"");
    if (!stream_read_u64(object, "capture_buffer_handle", "captureBufferHandle", UINT64_MAX, true, 0,
            &out->capture_handle, index, error) ||
        !stream_read_u64(object, "capture_offset", "captureOffset", UINT64_MAX, false, 0, &out->capture_offset, index,
            error) ||
        !stream_read_u64(object, "capture_size", "captureSize", UINT64_MAX, true, 0, &out->capture_size, index, error))
        return false;
    if (out->capture_size == 0) return stream_reject(error, index, "\"captureSize\" must be nonzero");
    if (!doe_semantic_present(out)) return stream_reject(error, index, "a capture needs a semantic operation field");
    out->has_capture = true;
    return true;
}

static bool parse_kernel_dispatch(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "kernel", "kernel_name", "entry_point",
        "entryPoint", "x", "y", "z", "repeat", "repeatSynchronization", "repeat_synchronization",
        "warmup_dispatch_count", "warmupDispatchCount", "initialize_buffers_on_create", "initializeBuffersOnCreate",
        "output_oracle", "outputOracle", "bindings", "semantic_op_id", "semanticOpId", "semantic_stage",
        "semanticStage", "semantic_phase", "semanticPhase", "semantic_token_index", "semanticTokenIndex",
        "semantic_layer_index", "semanticLayerIndex", "semantic_execution_plan_hash", "semanticExecutionPlanHash",
        "capture_buffer_handle", "captureBufferHandle", "capture_offset", "captureOffset", "capture_size",
        "captureSize", NULL};
    if (!stream_only_members(object, allowed, index, "kernel_dispatch", error)) return false;
    if (!parse_semantic(object, &command->semantic, index, error)) return false;
    if (!stream_read_string(object, "kernel", "kernel_name", &command->as.dispatch.kernel, index, error)) return false;
    if (!command->as.dispatch.kernel) return stream_reject(error, index, "kernel_dispatch requires a kernel string");
    if (!stream_read_string(object, "entry_point", "entryPoint", &command->as.dispatch.entry_point, index, error)) return false;
    if (!command->as.dispatch.entry_point) command->as.dispatch.entry_point = "main";
    uint64_t x, y, z;
    if (!stream_read_u64(object, "x", NULL, UINT32_MAX, false, 1, &x, index, error) ||
        !stream_read_u64(object, "y", NULL, UINT32_MAX, false, 1, &y, index, error) ||
        !stream_read_u64(object, "z", NULL, UINT32_MAX, false, 1, &z, index, error)) return false;
    if (x == 0 || y == 0 || z == 0) return stream_reject(error, index, "kernel_dispatch dimensions must be nonzero");
    command->as.dispatch.x = (uint32_t)x;
    command->as.dispatch.y = (uint32_t)y;
    command->as.dispatch.z = (uint32_t)z;
    if (!stream_read_u64(object, "repeat", NULL, UINT32_MAX, false, 1, &command->as.dispatch.repeat, index, error)) return false;
    if (command->as.dispatch.repeat == 0) return stream_reject(error, index, "kernel_dispatch \"repeat\" must be nonzero");
    if (!stream_read_u64(object, "warmup_dispatch_count", "warmupDispatchCount", UINT32_MAX, false, 0,
            &command->as.dispatch.warmup, index, error)) return false;
    const char *synchronization = NULL;
    if (!stream_read_string(object, "repeatSynchronization", "repeat_synchronization", &synchronization, index, error)) return false;
    if (synchronization && strcasecmp(synchronization, "dependent") && strcasecmp(synchronization, "independent"))
        return stream_reject(error, index, "repeatSynchronization \"%s\" is not dependent or independent", synchronization);
    command->as.dispatch.repeat_independent = synchronization && !strcasecmp(synchronization, "independent");
    bool ambiguous = false;
    const doe_json_value *initialize = stream_either(object, "initialize_buffers_on_create", "initializeBuffersOnCreate", &ambiguous);
    if (ambiguous || (initialize && initialize->kind != DOE_JSON_BOOL))
        return stream_reject(error, index, "\"initialize_buffers_on_create\" must be one boolean");
    command->as.dispatch.initialize_buffers = initialize && initialize->boolean;
    command->as.dispatch.oracle = stream_either(object, "output_oracle", "outputOracle", &ambiguous);
    if (ambiguous) return stream_reject(error, index, "both \"output_oracle\" and \"outputOracle\" are present");
    const doe_json_value *bindings = doe_json_member_value(object, "bindings");
    if (bindings && bindings->kind != DOE_JSON_ARRAY) return stream_reject(error, index, "kernel_dispatch \"bindings\" must be an array");
    size_t count = bindings ? bindings->count : 0;
    if (count) {
        command->as.dispatch.bindings = calloc(count, sizeof(doe_kernel_binding));
        if (!command->as.dispatch.bindings) return stream_reject(error, index, "out of memory");
    }
    command->as.dispatch.binding_count = count;
    for (size_t i = 0; i < count; i++) {
        if (!parse_binding(bindings->items[i], &command->as.dispatch.bindings[i], index, i, error)) return false;
        for (size_t j = 0; j < i; j++) {
            const doe_kernel_binding *a = &command->as.dispatch.bindings[j], *b = &command->as.dispatch.bindings[i];
            if (a->group == b->group && a->binding == b->binding)
                return stream_reject(error, index, "bindings repeat group %u binding %u", b->group, b->binding);
        }
    }
    return true;
}

/* command_parse_dispatch.zig parseDispatchDimensions: workgroupCount or
 * workgroups (three counts), else x, y, z defaulting to 1; zero is rejected. */
static bool parse_grid(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "x", "y", "z", "workgroupCount", "workgroups", NULL};
    if (!stream_only_members(object, allowed, index, command->label, error)) return false;
    bool ambiguous = false;
    const doe_json_value *counts = stream_either(object, "workgroupCount", "workgroups", &ambiguous);
    if (ambiguous) return stream_reject(error, index, "both \"workgroupCount\" and \"workgroups\" are present");
    uint64_t xyz[3] = {1, 1, 1};
    if (counts) {
        if (counts->kind != DOE_JSON_ARRAY || counts->count != 3)
            return stream_reject(error, index, "\"workgroupCount\" must hold three counts");
        for (int i = 0; i < 3; i++)
            if (!doe_json_as_u64(counts->items[i], UINT32_MAX, &xyz[i]))
                return stream_reject(error, index, "\"workgroupCount\"[%d] must be a u32", i);
    } else if (!stream_read_u64(object, "x", NULL, UINT32_MAX, false, 1, &xyz[0], index, error) ||
        !stream_read_u64(object, "y", NULL, UINT32_MAX, false, 1, &xyz[1], index, error) ||
        !stream_read_u64(object, "z", NULL, UINT32_MAX, false, 1, &xyz[2], index, error)) {
        return false;
    }
    if (!xyz[0] || !xyz[1] || !xyz[2]) return stream_reject(error, index, "%s dimensions must be nonzero", command->label);
    command->as.grid.x = (uint32_t)xyz[0];
    command->as.grid.y = (uint32_t)xyz[1];
    command->as.grid.z = (uint32_t)xyz[2];
    return true;
}

static void free_command(doe_command *command)
{
    if (command->kind == DOE_COMMAND_BUFFER_WRITE) free(command->as.buffer_write.words);
    if (command->kind == DOE_COMMAND_KERNEL_DISPATCH) free(command->as.dispatch.bindings);
    if (command->kind == DOE_COMMAND_TEXTURE_WRITE) free(command->as.texture_write.data);
    if (command->kind == DOE_COMMAND_RENDER_DRAW || command->kind == DOE_COMMAND_RENDER_PASS) free(command->as.render.indices);
}

void doe_stream_free(doe_stream *stream)
{
    for (size_t i = 0; i < stream->count; i++) free_command(&stream->commands[i]);
    free(stream->commands);
    doe_json_free(&stream->document);
    *stream = (doe_stream){0};
}

static bool parse_command(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    if (object->kind != DOE_JSON_OBJECT) return stream_reject(error, index, "command is not an object");
    const char *name = NULL;
    const char *keys[] = {"kind", "command", "command_kind"};
    for (size_t i = 0; i < 3; i++) {
        const doe_json_value *value = doe_json_member_value(object, keys[i]);
        if (!value) continue;
        if (name) return stream_reject(error, index, "command names its kind more than once");
        if (value->kind != DOE_JSON_STRING) return stream_reject(error, index, "\"%s\" must be a string", keys[i]);
        name = value->string;
    }
    if (!name) return stream_reject(error, index, "command has no \"kind\"");
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++) {
        if (strcasecmp(name, KINDS[i].name) != 0) continue;
        command->kind = KINDS[i].kind;
        command->label = doe_command_tag(command->kind);
        switch (command->kind) {
        case DOE_COMMAND_UPLOAD: return parse_upload(object, command, index, error);
        case DOE_COMMAND_BUFFER_WRITE: return parse_buffer_write(object, command, index, error);
        case DOE_COMMAND_BARRIER: return parse_barrier(object, command, index, error);
        case DOE_COMMAND_KERNEL_DISPATCH: return parse_kernel_dispatch(object, command, index, error);
        case DOE_COMMAND_COPY: return stream_parse_copy(object, name, command, index, error);
        case DOE_COMMAND_TEXTURE_WRITE: return stream_parse_texture_write(object, command, index, error);
        case DOE_COMMAND_TEXTURE_QUERY: return stream_parse_texture_query(object, command, index, error);
        case DOE_COMMAND_TEXTURE_DESTROY: return stream_parse_texture_destroy(object, command, index, error);
        case DOE_COMMAND_RENDER_DRAW:
        case DOE_COMMAND_RENDER_PASS: return stream_parse_render(object, name, command, index, error);
        case DOE_COMMAND_SAMPLER_CREATE: return stream_parse_sampler(object, command, index, error);
        case DOE_COMMAND_SAMPLER_DESTROY: return stream_parse_sampler_destroy(object, command, index, error);
        case DOE_COMMAND_DISPATCH:
        case DOE_COMMAND_DISPATCH_INDIRECT: return parse_grid(object, command, index, error);
        }
    }
    return stream_reject(error, index, "command kind \"%s\" is not implemented by doe-c-runtime", name);
}

bool doe_stream_parse(const char *text, size_t size, doe_stream *stream, doe_stream_error *error)
{
    doe_stream parsed = {0};
    *stream = (doe_stream){0};
    doe_json_error json_error;
    if (doe_json_parse(text, size, &parsed.document, &json_error) != DOE_JSON_OK)
        return stream_reject(error, SIZE_MAX, "commands JSON: %s at byte %zu", doe_json_status_name(json_error.status), json_error.offset);
    const doe_json_value *list = parsed.document.root;
    if (list->kind == DOE_JSON_OBJECT) list = doe_json_member_value(list, "commands");
    if (!list || list->kind != DOE_JSON_ARRAY) {
        doe_json_free(&parsed.document);
        return stream_reject(error, SIZE_MAX, "commands must be an array or an object with a \"commands\" array");
    }
    if (list->count) {
        parsed.commands = calloc(list->count, sizeof(doe_command));
        if (!parsed.commands) {
            doe_json_free(&parsed.document);
            return stream_reject(error, SIZE_MAX, "out of memory");
        }
    }
    for (size_t i = 0; i < list->count; i++) {
        if (!parse_command(list->items[i], &parsed.commands[i], i, error)) {
            parsed.count = i + 1; /* frees whatever the failed command already owned */
            doe_stream_free(&parsed);
            return false;
        }
    }
    parsed.count = list->count;
    *stream = parsed;
    return true;
}

bool doe_output_oracle_parse(const doe_json_value *value, doe_output_oracle *out, char *message, size_t message_size)
{
    static const char *const allowed[] = {"schema_version", "schemaVersion", "kind", "initialization", "binding_group",
        "bindingGroup", "binding", "dispatch_count", "dispatchCount", "expected_sha256", "expectedSha256",
        "reference_id", "referenceId", NULL};
    doe_stream_error error = {0};
    *out = (doe_output_oracle){0};
    bool ok = value && value->kind == DOE_JSON_OBJECT;
    if (!ok) snprintf(error.message, sizeof(error.message), "output_oracle must be an object");
    uint64_t schema = 0, group = 0, binding = 0;
    ok = ok && stream_only_members(value, allowed, 0, "output_oracle", &error);
    ok = ok && stream_read_u64(value, "schema_version", "schemaVersion", UINT32_MAX, true, 0, &schema, 0, &error);
    if (ok && schema != 1) ok = stream_reject(&error, 0, "output_oracle schema_version %llu is not implemented (1 only)", (unsigned long long)schema);
    const char *kind = NULL, *initialization = NULL, *expected = NULL;
    ok = ok && stream_read_string(value, "kind", NULL, &kind, 0, &error) &&
        stream_read_string(value, "initialization", NULL, &initialization, 0, &error) &&
        stream_read_string(value, "expected_sha256", "expectedSha256", &expected, 0, &error) &&
        stream_read_string(value, "reference_id", "referenceId", &out->reference_id, 0, &error);
    if (ok && (!kind || strcmp(kind, "sha256_exact_v1"))) ok = stream_reject(&error, 0, "output_oracle kind must be sha256_exact_v1");
    if (ok && (!initialization || strcmp(initialization, "zero_fill_v1")))
        ok = stream_reject(&error, 0, "output_oracle initialization must be zero_fill_v1");
    if (ok && !out->reference_id) ok = stream_reject(&error, 0, "output_oracle needs a reference_id");
    ok = ok && stream_read_u64(value, "binding_group", "bindingGroup", UINT32_MAX, true, 0, &group, 0, &error) &&
        stream_read_u64(value, "binding", NULL, UINT32_MAX, true, 0, &binding, 0, &error) &&
        stream_read_u64(value, "dispatch_count", "dispatchCount", UINT32_MAX, true, 0, &out->dispatch_count, 0, &error);
    if (ok && out->dispatch_count == 0) ok = stream_reject(&error, 0, "output_oracle dispatch_count must be nonzero");
    if (ok && (!expected || strlen(expected) != 64 || strspn(expected, "0123456789abcdef") != 64))
        ok = stream_reject(&error, 0, "output_oracle expected_sha256 must be 64 lowercase hex digits");
    if (!ok) {
        snprintf(message, message_size, "%s", error.message);
        return false;
    }
    out->group = (uint32_t)group;
    out->binding = (uint32_t)binding;
    memcpy(out->expected_sha256, expected, 65);
    return true;
}
