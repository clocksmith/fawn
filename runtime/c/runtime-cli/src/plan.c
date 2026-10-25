/* SPDX-License-Identifier: MIT */
#include "plan.h"
#include "stream_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_io.h"

#define DEFAULT_KERNEL_ROOT "bench/inference-pipeline/kernels"

char *doe_plan_asset_root(void)
{
    const char *env = getenv("DOE_BENCH_ASSET_CACHE_DIR");
    const char *home = getenv("HOME");
    const char *format = env ? "%s" : home ? "%s/.cache/doe/bench_synthetic_assets" : "%scache/doe/bench_synthetic_assets";
    const char *base = env ? env : home ? home : "";
    int size = snprintf(NULL, 0, format, base);
    char *out = size < 0 ? NULL : malloc((size_t)size + 1);
    if (out) snprintf(out, (size_t)size + 1, format, base);
    return out;
}

/* At most limit + 1 bytes, so a longer file reads as longer than `limit`. */
static char *read_all(const char *path, size_t limit, size_t *size)
{
    FILE *file = doe_host_fopen(path, "rb");
    if (!file) return NULL;
    char *bytes = malloc(limit + 1);
    size_t length = bytes ? fread(bytes, 1, limit + 1, file) : 0;
    bool failed = !bytes || ferror(file);
    fclose(file);
    if (failed) { free(bytes); return NULL; }
    *size = length;
    return bytes;
}

/* One of up to three spellings, as a u64; absent gives `fallback` unless required. */
static bool read_u64_of(const doe_json_value *object, const char *a, const char *b, const char *c, bool required,
    uint64_t fallback, uint64_t *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_one_of(object, a, b, c, &ambiguous);
    if (ambiguous) return stream_reject(error, index, "\"%s\" is given more than once", a);
    if (!value) {
        if (required) return stream_reject(error, index, "\"%s\" is required", a);
        *out = fallback;
        return true;
    }
    if (!doe_json_as_u64(value, UINT64_MAX, out)) return stream_reject(error, index, "\"%s\" must be an unsigned integer", a);
    return true;
}

static bool read_string_of(const doe_json_value *object, const char *a, const char *b, const char *c, bool required,
    const char **out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_one_of(object, a, b, c, &ambiguous);
    if (ambiguous) return stream_reject(error, index, "\"%s\" is given more than once", a);
    if (!value) {
        *out = NULL;
        return !required || stream_reject(error, index, "\"%s\" is required", a);
    }
    if (value->kind != DOE_JSON_STRING || !value->string_size || strlen(value->string) != value->string_size)
        return stream_reject(error, index, "\"%s\" must be a nonempty string", a);
    *out = value->string;
    return true;
}

static bool lower_buffer_write(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "handle", "resource_handle", "resourceHandle", "offset",
        "buffer_offset", "bufferOffset", "bufferSize", "buffer_size", "data", NULL};
    if (!stream_only_members(object, allowed, index, "plan buffer_write", error)) return false;
    if (!read_u64_of(object, "handle", "resource_handle", "resourceHandle", true, 0, &command->as.buffer_write.handle, index, error) ||
        !read_u64_of(object, "offset", "buffer_offset", "bufferOffset", false, 0, &command->as.buffer_write.offset, index, error) ||
        !read_u64_of(object, "bufferSize", "buffer_size", NULL, false, 0, &command->as.buffer_write.buffer_size, index, error)) return false;
    const doe_json_value *data = doe_json_member_value(object, "data");
    if (!data || data->kind != DOE_JSON_ARRAY || !data->count)
        return stream_reject(error, index, "plan buffer_write \"data\" must be a nonempty array of u32 words");
    if (command->as.buffer_write.offset % 4) return stream_reject(error, index, "plan buffer_write \"offset\" must be a multiple of 4");
    command->as.buffer_write.words = calloc(data->count, sizeof(uint32_t));
    if (!command->as.buffer_write.words) return stream_reject(error, index, "out of memory");
    command->as.buffer_write.word_count = data->count;
    for (size_t i = 0; i < data->count; i++) {
        uint64_t word;
        if (!doe_json_as_u64(data->items[i], UINT32_MAX, &word))
            return stream_reject(error, index, "plan buffer_write \"data\"[%zu] must be a u32", i);
        command->as.buffer_write.words[i] = (uint32_t)word;
    }
    return true;
}

/* synthetic_assets.zig: <root>/<namespace>/<key>.bin holds exactly byteLength
 * bytes; the generator, seed and scale only describe how it was made. */
static bool lower_buffer_load(const doe_json_value *object, const char *asset_root, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "handle", "resource_handle", "resourceHandle", "offset",
        "buffer_offset", "bufferOffset", "byteLength", "byte_length", "bufferSize", "buffer_size", "cacheNamespace",
        "cache_namespace", "cacheKey", "cache_key", "assetKey", "asset_key", "generator", "seed", "scale", NULL};
    if (!stream_only_members(object, allowed, index, "plan buffer_load", error)) return false;
    uint64_t byte_length = 0, seed = 0;
    const char *space = NULL, *key = NULL, *asset_key = NULL, *generator = NULL;
    bool ambiguous = false;
    const doe_json_value *length = stream_one_of(object, "byteLength", "byte_length", NULL, &ambiguous);
    if (!length) length = stream_one_of(object, "bufferSize", "buffer_size", NULL, &ambiguous);
    if (ambiguous || !length || !doe_json_as_u64(length, UINT64_MAX, &byte_length))
        return stream_reject(error, index, "plan buffer_load needs one integer \"byteLength\"");
    const doe_json_value *scale = doe_json_member_value(object, "scale");
    if (!read_u64_of(object, "handle", "resource_handle", "resourceHandle", true, 0, &command->as.buffer_write.handle, index, error) ||
        !read_u64_of(object, "offset", "buffer_offset", "bufferOffset", false, 0, &command->as.buffer_write.offset, index, error) ||
        !read_u64_of(object, "bufferSize", "buffer_size", NULL, false, byte_length, &command->as.buffer_write.buffer_size, index, error) ||
        !read_string_of(object, "cacheNamespace", "cache_namespace", NULL, true, &space, index, error) ||
        !read_string_of(object, "cacheKey", "cache_key", NULL, true, &key, index, error) ||
        !read_string_of(object, "assetKey", "asset_key", NULL, false, &asset_key, index, error) ||
        !read_string_of(object, "generator", NULL, NULL, true, &generator, index, error) ||
        !read_u64_of(object, "seed", NULL, NULL, true, 0, &seed, index, error)) return false;
    if (!scale || scale->kind != DOE_JSON_NUMBER) return stream_reject(error, index, "plan buffer_load \"scale\" must be a number");
    if (!byte_length || byte_length % 4 || command->as.buffer_write.offset % 4 || byte_length > SIZE_MAX)
        return stream_reject(error, index, "plan buffer_load byteLength and offset must be nonzero multiples of 4");
    if (strstr(space, "..") || strstr(key, "..") || strchr(key, '/'))
        return stream_reject(error, index, "plan buffer_load cache names must not leave the asset cache");
    int path_size = snprintf(NULL, 0, "%s/%s/%s.bin", asset_root, space, key);
    char *path = path_size < 0 ? NULL : malloc((size_t)path_size + 1);
    if (!path) return stream_reject(error, index, "out of memory");
    snprintf(path, (size_t)path_size + 1, "%s/%s/%s.bin", asset_root, space, key);
    size_t read = 0;
    char *bytes = read_all(path, (size_t)byte_length, &read);
    if (!bytes || read != byte_length) {
        stream_reject(error, index, "plan buffer_load asset %.180s %s", path,
            bytes ? "does not hold byteLength bytes" : "cannot be read");
        free(bytes);
        free(path);
        return false;
    }
    free(path);
    command->as.buffer_write.word_count = (size_t)byte_length / 4;
    command->as.buffer_write.words = malloc((size_t)byte_length);
    if (!command->as.buffer_write.words) { free(bytes); return stream_reject(error, index, "out of memory"); }
    for (size_t i = 0; i < command->as.buffer_write.word_count; i++) {
        const unsigned char *b = (const unsigned char *)bytes + 4 * i;
        command->as.buffer_write.words[i] = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    }
    free(bytes);
    return true;
}

static bool lower_binding(const doe_json_value *object, doe_kernel_binding *out, size_t index, size_t position,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "binding", "group", "group_index", "groupIndex", "resource_handle",
        "resourceHandle", "handle", "buffer_size", "bufferSize", "buffer_type", "bufferType", "visibility", NULL};
    if (object->kind != DOE_JSON_OBJECT) return stream_reject(error, index, "bindings[%zu] must be an object", position);
    if (!stream_only_members(object, allowed, index, "plan kernel_dispatch binding", error)) return false;
    const char *kind = NULL, *type = NULL, *visibility = NULL;
    uint64_t binding = 0, group = 0;
    if (!read_string_of(object, "kind", NULL, NULL, true, &kind, index, error) ||
        !read_u64_of(object, "binding", NULL, NULL, true, 0, &binding, index, error) ||
        !read_u64_of(object, "group", "group_index", "groupIndex", false, 0, &group, index, error) ||
        !read_u64_of(object, "resource_handle", "resourceHandle", "handle", true, 0, &out->handle, index, error) ||
        !read_u64_of(object, "buffer_size", "bufferSize", NULL, true, 0, &out->buffer_size, index, error) ||
        !read_string_of(object, "buffer_type", "bufferType", NULL, true, &type, index, error) ||
        !read_string_of(object, "visibility", NULL, NULL, false, &visibility, index, error)) return false;
    if (doe_host_casecmp(kind, "buffer")) return stream_reject(error, index, "bindings[%zu] kind \"%s\" is not a buffer", position, kind);
    if (binding > UINT32_MAX || group > UINT32_MAX) return stream_reject(error, index, "bindings[%zu] index exceeds u32", position);
    if (visibility && doe_host_casecmp(visibility, "compute"))
        return stream_reject(error, index, "bindings[%zu] visibility must be \"compute\"", position);
    out->binding = (uint32_t)binding;
    out->group = (uint32_t)group;
    if (!doe_host_casecmp(type, "uniform")) out->buffer_type = DOE_BINDING_UNIFORM;
    else if (!doe_host_casecmp(type, "storage")) out->buffer_type = DOE_BINDING_STORAGE;
    else if (!doe_host_casecmp(type, "readonly") || !doe_host_casecmp(type, "read_only_storage") ||
        !doe_host_casecmp(type, "read-only-storage") || !doe_host_casecmp(type, "readonly_storage"))
        out->buffer_type = DOE_BINDING_READ_ONLY_STORAGE;
    else return stream_reject(error, index, "bindings[%zu] buffer_type \"%s\" is not uniform, storage or readonly", position, type);
    return true;
}

/* Fields of the compatibility command stream that doe-plan-executor reads past:
 * its rows carry the plan step identity, and it takes no captures or oracles. */
static const char *const SUPERSEDED[] = {"semanticOpId", "semanticStage", "semanticPhase", "semanticTokenIndex",
    "semanticLayerIndex", "semanticExecutionPlanHash", "captureBufferHandle", "captureOffset", "captureSize",
    "output_oracle", "outputOracle", NULL};

static bool lower_kernel_dispatch(const doe_json_value *object, doe_command *command, size_t index, bool *superseded,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "kernel", "kernel_name", "kernelName", "entry_point", "entryPoint",
        "x", "y", "z", "workgroupCount", "workgroups", "initialize_buffers_on_create", "initializeBuffersOnCreate",
        "bindings", "semanticOpId", "semanticStage", "semanticPhase", "semanticTokenIndex", "semanticLayerIndex",
        "semanticExecutionPlanHash", "captureBufferHandle", "captureOffset", "captureSize", "output_oracle",
        "outputOracle", NULL};
    if (!stream_only_members(object, allowed, index, "plan kernel_dispatch", error)) return false;
    *superseded = false;
    for (const char *const *name = SUPERSEDED; *name; name++)
        if (doe_json_member_value(object, *name)) *superseded = true;
    if (!read_string_of(object, "kernel", "kernel_name", "kernelName", true, &command->as.dispatch.kernel, index, error) ||
        !read_string_of(object, "entry_point", "entryPoint", NULL, false, &command->as.dispatch.entry_point, index, error)) return false;
    if (!command->as.dispatch.entry_point) command->as.dispatch.entry_point = "main";
    bool ambiguous = false;
    const doe_json_value *groups = stream_one_of(object, "workgroupCount", "workgroups", NULL, &ambiguous);
    uint64_t xyz[3];
    for (int axis = 0; axis < 3; axis++) {
        const char *name = (const char *[]){"x", "y", "z"}[axis];
        const doe_json_value *value = doe_json_member_value(object, name);
        if (!value && groups && groups->kind == DOE_JSON_ARRAY && groups->count == 3) value = groups->items[axis];
        if (ambiguous || !value || !doe_json_as_u64(value, UINT32_MAX, &xyz[axis]) || !xyz[axis])
            return stream_reject(error, index, "plan kernel_dispatch needs nonzero u32 \"%s\"", name);
    }
    command->as.dispatch.x = (uint32_t)xyz[0];
    command->as.dispatch.y = (uint32_t)xyz[1];
    command->as.dispatch.z = (uint32_t)xyz[2];
    command->as.dispatch.repeat = 1;
    const doe_json_value *initialize = stream_one_of(object, "initialize_buffers_on_create",
        "initializeBuffersOnCreate", NULL, &ambiguous);
    if (ambiguous || (initialize && initialize->kind != DOE_JSON_BOOL))
        return stream_reject(error, index, "\"initialize_buffers_on_create\" must be one boolean");
    command->as.dispatch.initialize_buffers = initialize && initialize->boolean;
    const doe_json_value *bindings = doe_json_member_value(object, "bindings");
    if (!bindings || bindings->kind != DOE_JSON_ARRAY)
        return stream_reject(error, index, "plan kernel_dispatch \"bindings\" must be an array");
    if (bindings->count) {
        command->as.dispatch.bindings = calloc(bindings->count, sizeof(doe_kernel_binding));
        if (!command->as.dispatch.bindings) return stream_reject(error, index, "out of memory");
    }
    command->as.dispatch.binding_count = bindings->count;
    for (size_t i = 0; i < bindings->count; i++) {
        if (!lower_binding(bindings->items[i], &command->as.dispatch.bindings[i], index, i, error)) return false;
        for (size_t j = 0; j < i; j++)
            if (command->as.dispatch.bindings[j].group == command->as.dispatch.bindings[i].group && command->as.dispatch.bindings[j].binding == command->as.dispatch.bindings[i].binding)
                return stream_reject(error, index, "bindings repeat group %u binding %u", command->as.dispatch.bindings[i].group,
                    command->as.dispatch.bindings[i].binding);
    }
    return true;
}

static bool count_matches(const doe_json_value *root, const char *key, bool required, uint64_t actual,
    doe_stream_error *error)
{
    uint64_t declared = 0;
    if (!stream_read_u64(root, key, NULL, UINT32_MAX, required, 0, &declared, SIZE_MAX, error)) return false;
    if (declared != actual)
        return stream_reject(error, SIZE_MAX, "plan \"%s\" %llu does not equal its %llu commands", key,
            (unsigned long long)declared, (unsigned long long)actual);
    return true;
}

static bool parse_root(const doe_json_value *root, const char *asset_root, doe_plan *plan, doe_stream_error *error)
{
    static const char *const allowed[] = {"schemaVersion", "planKind", "workloadId", "irPath", "irScenario",
        "description", "planPath", "commandsPath", "commandCount", "bufferWriteCount", "bufferLoadCount",
        "dispatchCount", "sourceIrSha256", "compatibilityCommandsSha256", "planSha256", "commands", NULL};
    if (root->kind != DOE_JSON_OBJECT) return stream_reject(error, SIZE_MAX, "a plan is a JSON object");
    if (!stream_only_members(root, allowed, SIZE_MAX, "plan", error)) return false;
    uint64_t schema = 0;
    const char *text = NULL;
    if (!stream_read_u64(root, "schemaVersion", NULL, UINT32_MAX, true, 0, &schema, SIZE_MAX, error)) return false;
    if (schema != 1) return stream_reject(error, SIZE_MAX, "plan schemaVersion %llu is not 1", (unsigned long long)schema);
    if (!read_string_of(root, "workloadId", NULL, NULL, true, &plan->workload_id, SIZE_MAX, error) ||
        !read_string_of(root, "irPath", NULL, NULL, true, &plan->ir_path, SIZE_MAX, error) ||
        !read_string_of(root, "planSha256", NULL, NULL, true, &plan->plan_sha256, SIZE_MAX, error) ||
        !read_string_of(root, "planKind", NULL, NULL, true, &text, SIZE_MAX, error) ||
        !read_string_of(root, "irScenario", NULL, NULL, true, &text, SIZE_MAX, error) ||
        !read_string_of(root, "sourceIrSha256", NULL, NULL, true, &text, SIZE_MAX, error) ||
        !read_string_of(root, "compatibilityCommandsSha256", NULL, NULL, true, &text, SIZE_MAX, error)) return false;
    const doe_json_value *list = doe_json_member_value(root, "commands");
    if (!list || list->kind != DOE_JSON_ARRAY) return stream_reject(error, SIZE_MAX, "plan \"commands\" must be an array");
    plan->stream.commands = list->count ? calloc(list->count, sizeof(doe_command)) : NULL;
    plan->op_ids = list->count ? calloc(list->count, sizeof(*plan->op_ids)) : NULL;
    if (list->count && (!plan->stream.commands || !plan->op_ids)) return stream_reject(error, SIZE_MAX, "out of memory");
    plan->stream.count = list->count;
    uint64_t writes = 0, loads = 0, dispatches = 0;
    for (size_t i = 0; i < list->count; i++) {
        const doe_json_value *object = list->items[i];
        doe_command *command = &plan->stream.commands[i];
        const char *kind = NULL;
        if (object->kind != DOE_JSON_OBJECT) return stream_reject(error, i, "plan command is not an object");
        if (!read_string_of(object, "kind", NULL, NULL, true, &kind, i, error)) return false;
        bool superseded = false;
        bool ok;
        if (!doe_host_casecmp(kind, "buffer_write") || !doe_host_casecmp(kind, "write_buffer") || !doe_host_casecmp(kind, "queue_write_buffer")) {
            command->kind = DOE_COMMAND_BUFFER_WRITE;
            command->label = "buffer_write";
            ok = lower_buffer_write(object, command, i, error);
            writes++;
        } else if (!doe_host_casecmp(kind, "buffer_load")) {
            command->kind = DOE_COMMAND_BUFFER_WRITE; /* Executed and hashed as a buffer write. */
            command->label = "buffer_load";
            ok = lower_buffer_load(object, asset_root, command, i, error);
            loads++;
        } else if (!doe_host_casecmp(kind, "kernel_dispatch")) {
            command->kind = DOE_COMMAND_KERNEL_DISPATCH;
            command->label = "kernel_dispatch";
            ok = lower_kernel_dispatch(object, command, i, &superseded, error);
            dispatches++;
        } else {
            return stream_reject(error, i, "plan command kind \"%s\" is not buffer_write, buffer_load or kernel_dispatch", kind);
        }
        if (!ok) return false;
        if (superseded && !plan->superseded_count++) plan->first_superseded = i;
        snprintf(plan->op_ids[i], sizeof(plan->op_ids[i]), "step-%06zu", i);
        command->semantic = (doe_semantic){.op_id = plan->op_ids[i], .stage = "runtime_plan", .phase = command->label,
            .execution_plan_hash = plan->plan_sha256};
    }
    return count_matches(root, "commandCount", true, list->count, error) &&
        count_matches(root, "bufferWriteCount", true, writes, error) &&
        count_matches(root, "bufferLoadCount", false, loads, error) &&
        count_matches(root, "dispatchCount", true, dispatches, error);
}

bool doe_plan_parse(const char *text, size_t size, const char *asset_root, doe_plan *plan, doe_stream_error *error)
{
    *plan = (doe_plan){0};
    *error = (doe_stream_error){.command_index = SIZE_MAX};
    doe_json_error json_error;
    doe_json_status status = doe_json_parse(text, size, &plan->stream.document, &json_error);
    if (status != DOE_JSON_OK) {
        snprintf(error->message, sizeof(error->message), "plan is not JSON: %s at byte %zu",
            doe_json_status_name(status), json_error.offset);
        return false;
    }
    if (!parse_root(plan->stream.document.root, asset_root, plan, error)) {
        doe_plan_free(plan);
        return false;
    }
    return true;
}

void doe_plan_free(doe_plan *plan)
{
    doe_stream_free(&plan->stream);
    free(plan->op_ids);
    *plan = (doe_plan){0};
}

char *doe_plan_kernel_root(const char *ir_path)
{
    size_t size = 0;
    char *text = read_all(ir_path, 8u << 20, &size);
    doe_json_document document = {0};
    doe_json_error error;
    const char *root = NULL;
    if (text && size <= (8u << 20) && doe_json_parse(text, size, &document, &error) == DOE_JSON_OK &&
        document.root->kind == DOE_JSON_OBJECT) {
        const doe_json_value *shared = doe_json_member_value(document.root, "shared");
        const doe_json_value *value = shared && shared->kind == DOE_JSON_OBJECT ?
            doe_json_member_value(shared, "kernelRoot") : NULL;
        if (value && value->kind == DOE_JSON_STRING) root = value->string;
    }
    char *out = doe_host_strdup(root ? root : DEFAULT_KERNEL_ROOT);
    doe_json_free(&document);
    free(text);
    return out;
}
