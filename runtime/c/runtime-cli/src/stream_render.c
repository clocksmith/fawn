/* SPDX-License-Identifier: MIT
 * render_draw, render_pass and sampler command parsing, following
 * doe-zig-runtime's command_parse_render.zig and command_json_extra.zig names,
 * defaults and validation. draw_indirect and draw_indexed_indirect take the
 * render_draw fields. Fields of the explicit-attachment, depth, vertex buffer,
 * bind group and indirect-buffer forms are not admitted. */
#include "stream_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { DEFAULT_TARGET_EXTENT = 64 };

static const char *const RENDER_FIELDS[] = {"kind", "command", "command_kind", "draw_count", "drawCount",
    "vertex_count", "vertexCount", "instance_count", "instanceCount", "first_vertex", "firstVertex",
    "first_instance", "firstInstance", "index_count", "indexCount", "first_index", "firstIndex",
    "base_vertex", "baseVertex", "index_data", "indexData", "indices", "index_format", "indexFormat",
    "target_handle", "targetHandle", "target_width", "targetWidth", "target_height", "targetHeight",
    "target_format", "targetFormat", "pipeline_mode", "pipelineMode", "bind_group_mode", "bindGroupMode",
    "encode_mode", "encodeMode", "viewport_x", "viewportX", "viewport_y", "viewportY", "viewport_width",
    "viewportWidth", "viewport_height", "viewportHeight", "viewport_min_depth", "viewportMinDepth",
    "viewport_max_depth", "viewportMaxDepth", "scissor_x", "scissorX", "scissor_y", "scissorY",
    "scissor_width", "scissorWidth", "scissor_height", "scissorHeight", "blend_r", "blendR", "blend_g", "blendG",
    "blend_b", "blendB", "blend_a", "blendA", "stencil_reference", "stencilReference",
    "bind_group_dynamic_offsets", "bindGroupDynamicOffsets", NULL};

static bool read_u32(const doe_json_value *object, const char *a, const char *b, bool *present, uint32_t fallback,
    uint32_t *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, a, b, &ambiguous);
    if (ambiguous) return stream_reject(error, index, "both \"%s\" and \"%s\" are present", a, b);
    if (present) *present = value != NULL;
    uint64_t number = fallback;
    if (value && !doe_json_as_u64(value, UINT32_MAX, &number))
        return stream_reject(error, index, "\"%s\" must be an integer in [0, 4294967295]", a);
    *out = (uint32_t)number;
    return true;
}

static bool read_float(const doe_json_value *object, const char *a, const char *b, bool *present, double fallback,
    double *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_either(object, a, b, &ambiguous);
    if (ambiguous) return stream_reject(error, index, "both \"%s\" and \"%s\" are present", a, b);
    if (present) *present = value != NULL;
    *out = fallback;
    if (!value) return true;
    if (value->kind != DOE_JSON_NUMBER || !isfinite(value->number) || fabs(value->number) > 3.4e38)
        return stream_reject(error, index, "\"%s\" must be a finite number", a);
    *out = value->number;
    return true;
}

/* A mode string, checked against doe-zig-runtime's accepted spellings. */
static bool read_mode(const doe_json_value *object, const char *a, const char *b, const char *const *accepted,
    size_t index, doe_stream_error *error)
{
    const char *value = NULL;
    if (!stream_read_string(object, a, b, &value, index, error)) return false;
    if (!value) return true;
    for (const char *const *name = accepted; *name; name++) if (strcasecmp(value, *name) == 0) return true;
    return stream_reject(error, index, "\"%s\" \"%s\" is not a doe-zig-runtime value", a, value);
}

static bool parse_indices(const doe_json_value *object, doe_render_draw *draw, bool kind_indexed, size_t index,
    doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *data = stream_one_of(object, "index_data", "indexData", "indices", &ambiguous);
    if (ambiguous) return stream_reject(error, index, "the index data is named more than once");
    bool has_count = false;
    if (!read_u32(object, "index_count", "indexCount", &has_count, 0, &draw->index_count, index, error)) return false;
    draw->indexed = kind_indexed || has_count || data;
    const char *format = NULL;
    if (!stream_read_string(object, "index_format", "indexFormat", &format, index, error)) return false;
    if (format && strcasecmp(format, "uint16") && strcasecmp(format, "u16") && strcasecmp(format, "uint32") &&
        strcasecmp(format, "u32"))
        return stream_reject(error, index, "\"index_format\" \"%s\" is not uint16 or uint32", format);
    if (!read_u32(object, "first_index", "firstIndex", NULL, 0, &draw->first_index, index, error)) return false;
    uint64_t magnitude = 0;
    bool ambiguous_base = false;
    const doe_json_value *base = stream_either(object, "base_vertex", "baseVertex", &ambiguous_base);
    if (ambiguous_base) return stream_reject(error, index, "both \"base_vertex\" and \"baseVertex\" are present");
    if (base) {
        magnitude = base->magnitude;
        if (base->kind != DOE_JSON_NUMBER || !base->integer || magnitude > (base->negative ? UINT64_C(2147483648) : INT32_MAX))
            return stream_reject(error, index, "\"base_vertex\" must be a 32-bit integer");
        draw->base_vertex = base->negative ? (int32_t)(-(int64_t)magnitude) : (int32_t)magnitude;
    }
    if (!draw->indexed) return true;
    if (!data || data->kind != DOE_JSON_ARRAY || data->count == 0)
        return stream_reject(error, index, "an indexed draw needs a nonempty \"index_data\" array");
    draw->indices = calloc(data->count, sizeof(uint32_t));
    if (!draw->indices) return stream_reject(error, index, "out of memory");
    draw->index_data_count = data->count;
    bool needs_32 = false;
    for (size_t i = 0; i < data->count; i++) {
        uint64_t value;
        if (!doe_json_as_u64(data->items[i], UINT32_MAX, &value))
            return stream_reject(error, index, "\"index_data\"[%zu] must be an integer in [0, 4294967295]", i);
        draw->indices[i] = (uint32_t)value;
        if (value > UINT16_MAX) needs_32 = true;
    }
    /* An undeclared format is uint16 unless an index needs 32 bits. */
    draw->index_uint32 = format ? (strcasecmp(format, "uint32") == 0 || strcasecmp(format, "u32") == 0) : needs_32;
    if (!draw->index_uint32 && needs_32) return stream_reject(error, index, "an index exceeds the uint16 index format");
    if (!has_count) draw->index_count = (uint32_t)data->count;
    if (draw->index_count == 0) return stream_reject(error, index, "\"index_count\" must be nonzero");
    if ((uint64_t)draw->first_index + draw->index_count > data->count)
        return stream_reject(error, index, "first_index + index_count exceeds the index data");
    return true;
}

bool stream_parse_render(const doe_json_value *object, const char *name, doe_command *command, size_t index,
    doe_stream_error *error)
{
    if (!stream_only_members(object, RENDER_FIELDS, index, "render", error)) return false;
    doe_render_draw *draw = &command->as.render;
    bool present = false;
    if (!read_u32(object, "draw_count", "drawCount", &present, 0, &draw->draw_count, index, error)) return false;
    if (!present) return stream_reject(error, index, "\"draw_count\" is required");
    if (!read_u32(object, "vertex_count", "vertexCount", NULL, 3, &draw->vertex_count, index, error) ||
        !read_u32(object, "instance_count", "instanceCount", NULL, 1, &draw->instance_count, index, error) ||
        !read_u32(object, "first_vertex", "firstVertex", NULL, 0, &draw->first_vertex, index, error) ||
        !read_u32(object, "first_instance", "firstInstance", NULL, 0, &draw->first_instance, index, error) ||
        !read_u32(object, "target_width", "targetWidth", NULL, DEFAULT_TARGET_EXTENT, &draw->target_width, index, error) ||
        !read_u32(object, "target_height", "targetHeight", NULL, DEFAULT_TARGET_EXTENT, &draw->target_height, index, error))
        return false;
    if (!draw->draw_count || !draw->vertex_count || !draw->instance_count || !draw->target_width || !draw->target_height)
        return stream_reject(error, index, "render counts and target extents must be nonzero");
    if (!stream_read_u64(object, "target_handle", "targetHandle", UINT64_MAX, false, 0, &draw->target_handle, index, error))
        return false;
    if (draw->target_handle == UINT64_C(0xFFFFFFFFFFFFFFFE)) draw->target_handle = 0; /* doe-zig-runtime's default handle */
    const char *format = NULL;
    if (!stream_read_string(object, "target_format", "targetFormat", &format, index, error)) return false;
    draw->target_format = DOE_FORMAT_RGBA8_UNORM;
    if (format && !stream_texture_format(format, &draw->target_format))
        return stream_reject(error, index, "target format \"%s\" is not implemented by doe-c-runtime", format);
    static const char *const pipeline_modes[] = {"static", "redundant", NULL};
    static const char *const bind_group_modes[] = {"no-change", "no_change", "redundant", NULL};
    static const char *const encode_modes[] = {"render-bundle", "render_bundle", "bundle", "render-pass", "render_pass",
        "pass", NULL};
    if (!read_mode(object, "pipeline_mode", "pipelineMode", pipeline_modes, index, error) ||
        !read_mode(object, "bind_group_mode", "bindGroupMode", bind_group_modes, index, error) ||
        !read_mode(object, "encode_mode", "encodeMode", encode_modes, index, error)) return false;
    bool ambiguous = false;
    const doe_json_value *offsets = stream_either(object, "bind_group_dynamic_offsets", "bindGroupDynamicOffsets", &ambiguous);
    if (ambiguous || (offsets && offsets->kind != DOE_JSON_ARRAY))
        return stream_reject(error, index, "\"bind_group_dynamic_offsets\" must be one array");
    for (size_t i = 0; offsets && i < offsets->count; i++) {
        uint64_t offset;
        if (!doe_json_as_u64(offsets->items[i], UINT32_MAX, &offset))
            return stream_reject(error, index, "\"bind_group_dynamic_offsets\"[%zu] must be an integer", i);
    }
    double value[6];
    if (!read_float(object, "viewport_x", "viewportX", NULL, 0, &value[0], index, error) ||
        !read_float(object, "viewport_y", "viewportY", NULL, 0, &value[1], index, error) ||
        !read_float(object, "viewport_width", "viewportWidth", &draw->has_viewport_width, 0, &value[2], index, error) ||
        !read_float(object, "viewport_height", "viewportHeight", &draw->has_viewport_height, 0, &value[3], index, error) ||
        !read_float(object, "viewport_min_depth", "viewportMinDepth", NULL, 0, &value[4], index, error) ||
        !read_float(object, "viewport_max_depth", "viewportMaxDepth", NULL, 1, &value[5], index, error)) return false;
    if (value[4] < 0 || value[4] > 1 || value[5] < 0 || value[5] > 1 || value[5] < value[4])
        return stream_reject(error, index, "viewport depths must satisfy 0 <= min <= max <= 1");
    draw->viewport_x = (float)value[0];
    draw->viewport_y = (float)value[1];
    draw->viewport_width = (float)value[2];
    draw->viewport_height = (float)value[3];
    draw->viewport_min_depth = (float)value[4];
    draw->viewport_max_depth = (float)value[5];
    if (!read_u32(object, "scissor_x", "scissorX", NULL, 0, &draw->scissor_x, index, error) ||
        !read_u32(object, "scissor_y", "scissorY", NULL, 0, &draw->scissor_y, index, error) ||
        !read_u32(object, "scissor_width", "scissorWidth", &draw->has_scissor_width, 0, &draw->scissor_width, index, error) ||
        !read_u32(object, "scissor_height", "scissorHeight", &draw->has_scissor_height, 0, &draw->scissor_height, index, error) ||
        !read_u32(object, "stencil_reference", "stencilReference", &draw->has_stencil_reference, 0, &draw->stencil_reference,
            index, error)) return false;
    const char *blend_names[4][2] = {{"blend_r", "blendR"}, {"blend_g", "blendG"}, {"blend_b", "blendB"}, {"blend_a", "blendA"}};
    for (int i = 0; i < 4; i++) {
        if (!read_float(object, blend_names[i][0], blend_names[i][1], &present, 0, &draw->blend[i], index, error)) return false;
        if (present) draw->has_blend_constant = true;
    }
    bool kind_indexed = strcasecmp(name, "draw_indexed") == 0 || strcasecmp(name, "draw_indexed_indirect") == 0;
    return parse_indices(object, draw, kind_indexed, index, error);
}

static bool read_sampler_handle(const doe_json_value *object, uint64_t *out, size_t index, doe_stream_error *error)
{
    const char *names[] = {"sampler_handle", "samplerHandle", "handle", "resource_handle", "resourceHandle"};
    const doe_json_value *found = NULL;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const doe_json_value *value = doe_json_member_value(object, names[i]);
        if (!value) continue;
        if (found) return stream_reject(error, index, "the sampler handle is named more than once");
        found = value;
    }
    if (!found || !doe_json_as_u64(found, UINT64_MAX, out)) return stream_reject(error, index, "an integer sampler handle is required");
    return true;
}

typedef struct enum_name {
    const char *name;
    uint32_t value;
} enum_name;

static bool read_enum(const doe_json_value *object, const char *a, const char *b, const enum_name *names, uint32_t fallback,
    uint32_t *out, size_t index, doe_stream_error *error)
{
    const char *value = NULL;
    if (!stream_read_string(object, a, b, &value, index, error)) return false;
    *out = fallback;
    if (!value) return true;
    for (const enum_name *n = names; n->name; n++) if (strcasecmp(value, n->name) == 0) { *out = n->value; return true; }
    return stream_reject(error, index, "\"%s\" \"%s\" is not a doe-zig-runtime value", a, value);
}

bool stream_parse_sampler(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "sampler_handle", "samplerHandle", "handle",
        "resource_handle", "resourceHandle", "address_mode_u", "addressModeU", "address_mode_v", "addressModeV",
        "address_mode_w", "addressModeW", "mag_filter", "magFilter", "min_filter", "minFilter", "mipmap_filter",
        "mipmapFilter", "lod_min_clamp", "lodMinClamp", "lod_max_clamp", "lodMaxClamp", "compare", "max_anisotropy",
        "maxAnisotropy", NULL};
    if (!stream_only_members(object, allowed, index, "sampler_create", error)) return false;
    doe_sampler_desc *s = &command->as.sampler;
    if (!read_sampler_handle(object, &s->handle, index, error)) return false;
    static const enum_name address[] = {{"clamp-to-edge", 1}, {"clamp_to_edge", 1}, {"repeat", 2}, {"mirror-repeat", 3},
        {"mirror_repeat", 3}, {NULL, 0}};
    static const enum_name filter[] = {{"nearest", 1}, {"linear", 2}, {NULL, 0}};
    static const enum_name compare[] = {{"undefined", 0}, {"never", 1}, {"less", 2}, {"equal", 3}, {"less-equal", 4},
        {"less_equal", 4}, {"greater", 5}, {"not-equal", 6}, {"not_equal", 6}, {"greater-equal", 7}, {"greater_equal", 7},
        {"always", 8}, {NULL, 0}};
    const char *modes[3][2] = {{"address_mode_u", "addressModeU"}, {"address_mode_v", "addressModeV"},
        {"address_mode_w", "addressModeW"}};
    for (int i = 0; i < 3; i++)
        if (!read_enum(object, modes[i][0], modes[i][1], address, 1, &s->address_mode[i], index, error)) return false;
    if (!read_enum(object, "mag_filter", "magFilter", filter, 1, &s->mag_filter, index, error) ||
        !read_enum(object, "min_filter", "minFilter", filter, 1, &s->min_filter, index, error) ||
        !read_enum(object, "mipmap_filter", "mipmapFilter", filter, 1, &s->mipmap_filter, index, error) ||
        !read_enum(object, "compare", NULL, compare, 0, &s->compare, index, error)) return false;
    double lod_min = 0, lod_max = 32;
    if (!read_float(object, "lod_min_clamp", "lodMinClamp", NULL, 0, &lod_min, index, error) ||
        !read_float(object, "lod_max_clamp", "lodMaxClamp", NULL, 32, &lod_max, index, error)) return false;
    s->lod_min_clamp = (float)lod_min;
    s->lod_max_clamp = (float)lod_max;
    uint32_t anisotropy = 1;
    if (!read_u32(object, "max_anisotropy", "maxAnisotropy", NULL, 1, &anisotropy, index, error)) return false;
    if (anisotropy == 0 || anisotropy > UINT16_MAX) return stream_reject(error, index, "\"max_anisotropy\" must be in [1, 65535]");
    s->max_anisotropy = (uint16_t)anisotropy;
    return true;
}

bool stream_parse_sampler_destroy(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "sampler_handle", "samplerHandle", "handle",
        "resource_handle", "resourceHandle", NULL};
    if (!stream_only_members(object, allowed, index, "sampler_destroy", error)) return false;
    return read_sampler_handle(object, &command->as.sampler_destroy.handle, index, error);
}
