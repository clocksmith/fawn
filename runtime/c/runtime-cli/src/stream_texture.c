/* SPDX-License-Identifier: MIT
 * Copy and texture command parsing, following doe-zig-runtime's
 * command_parse_copy.zig and command_json_extra.zig field names and defaults.
 * Only 2D, single-sample uncompressed color textures are admitted; copies may
 * name a mip level; other dimensions, sample counts, aspects and formats are
 * rejected. */
#include "stream_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_io.h"

enum { TEXTURE_USAGE_COPY_DST = 2, TEXTURE_USAGE_TEXTURE_BINDING = 4 };

typedef struct format_name {
    const char *name;
    doe_texture_format format;
    uint32_t bytes;
} format_name;

static const format_name FORMATS[] = {
    {"r8unorm", DOE_FORMAT_R8_UNORM, 1},
    {"rg8unorm", DOE_FORMAT_RG8_UNORM, 2},
    {"rgba8unorm", DOE_FORMAT_RGBA8_UNORM, 4},
    {"rgba8unorm-srgb", DOE_FORMAT_RGBA8_UNORM_SRGB, 4},
    {"bgra8unorm", DOE_FORMAT_BGRA8_UNORM, 4},
    {"r32uint", DOE_FORMAT_R32_UINT, 4},
    {"r32float", DOE_FORMAT_R32_FLOAT, 4},
    {"rgba16float", DOE_FORMAT_RGBA16_FLOAT, 8},
    {"rgba32float", DOE_FORMAT_RGBA32_FLOAT, 16},
};

uint32_t doe_texture_format_bytes(doe_texture_format format)
{
    for (size_t i = 0; i < sizeof(FORMATS) / sizeof(FORMATS[0]); i++) if (FORMATS[i].format == format) return FORMATS[i].bytes;
    return 0;
}

static bool read_format(const doe_json_value *object, const char *a, const char *b, const char *c,
    bool *present, doe_texture_format *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_one_of(object, a, b, c, &ambiguous);
    *present = value != NULL;
    if (ambiguous) return stream_reject(error, index, "\"%s\" is named more than once", a);
    if (!value) return true;
    if (value->kind != DOE_JSON_STRING) return stream_reject(error, index, "\"%s\" must be a string", a);
    if (stream_texture_format(value->string, out)) return true;
    return stream_reject(error, index, "texture format \"%s\" is not implemented by doe-c-runtime", value->string);
}

bool stream_texture_format(const char *name, doe_texture_format *out)
{
    for (size_t i = 0; i < sizeof(FORMATS) / sizeof(FORMATS[0]); i++) {
        if (doe_host_casecmp(name, FORMATS[i].name) == 0) { *out = FORMATS[i].format; return true; }
    }
    return false;
}

static bool read_u32_of(const doe_json_value *object, const char *a, const char *b, const char *c, bool *present,
    uint32_t *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_one_of(object, a, b, c, &ambiguous);
    *present = value != NULL;
    if (ambiguous) return stream_reject(error, index, "\"%s\" is named more than once", a);
    uint64_t number = 0;
    if (value && !doe_json_as_u64(value, UINT32_MAX, &number))
        return stream_reject(error, index, "\"%s\" must be an integer in [0, 4294967295]", a);
    *out = (uint32_t)number;
    return true;
}

/* A value that must equal the only one implemented, when present. */
static bool require_string(const doe_json_value *object, const char *a, const char *b, const char *only,
    size_t index, doe_stream_error *error)
{
    const char *value = NULL;
    if (!stream_read_string(object, a, b, &value, index, error)) return false;
    if (value && doe_host_casecmp(value, only) != 0)
        return stream_reject(error, index, "\"%s\" \"%s\" is not implemented by doe-c-runtime (only \"%s\")", a, value, only);
    return true;
}

static bool require_u32(const doe_json_value *object, const char *a, const char *b, uint32_t only, uint32_t also,
    size_t index, doe_stream_error *error)
{
    bool present = false;
    uint32_t value = 0;
    if (!read_u32_of(object, a, b, NULL, &present, &value, index, error)) return false;
    if (present && value != only && value != also)
        return stream_reject(error, index, "\"%s\" %u is not implemented by doe-c-runtime (only %u)", a, value, only);
    return true;
}

static bool read_handle(const doe_json_value *object, uint64_t *out, size_t index, doe_stream_error *error)
{
    const char *names[] = {"texture_handle", "textureHandle", "handle", "resource_handle", "resourceHandle",
        "target_handle", "targetHandle"};
    const doe_json_value *found = NULL;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const doe_json_value *value = doe_json_member_value(object, names[i]);
        if (!value) continue;
        if (found) return stream_reject(error, index, "the texture handle is named more than once");
        found = value;
    }
    if (!found || !doe_json_as_u64(found, UINT64_MAX, out) || *out == 0)
        return stream_reject(error, index, "a nonzero integer texture handle is required");
    return true;
}

/* src_<field> / src<Field> spellings of one copy side. */
typedef struct side_names {
    char snake[48], camel[48];
} side_names;

static side_names side_field(const char *side, const char *snake, const char *camel)
{
    side_names names;
    snprintf(names.snake, sizeof(names.snake), "%s_%s", side, snake);
    snprintf(names.camel, sizeof(names.camel), "%s%s", side, camel);
    return names;
}

static const char *const COPY_FIELDS[] = {"kind", "command", "command_kind", "bytes", "direction",
    "src_handle", "srcHandle", "dst_handle", "dstHandle", "src_kind", "srcKind", "dst_kind", "dstKind",
    "src_width", "srcWidth", "dst_width", "dstWidth", "src_height", "srcHeight", "dst_height", "dstHeight",
    "src_depth_or_array_layers", "srcDepthOrArrayLayers", "src_depth", "dst_depth_or_array_layers",
    "dstDepthOrArrayLayers", "dst_depth", "src_format", "srcFormat", "dst_format", "dstFormat",
    "src_usage", "srcUsage", "dst_usage", "dstUsage", "src_dimension", "srcDimension", "dst_dimension", "dstDimension",
    "src_view_dimension", "srcViewDimension", "dst_view_dimension", "dstViewDimension",
    "src_mip_level", "srcMipLevel", "dst_mip_level", "dstMipLevel", "src_sample_count", "srcSampleCount",
    "dst_sample_count", "dstSampleCount", "src_aspect", "srcAspect", "dst_aspect", "dstAspect",
    "src_bytes_per_row", "srcBytesPerRow", "dst_bytes_per_row", "dstBytesPerRow",
    "src_rows_per_image", "srcRowsPerImage", "dst_rows_per_image", "dstRowsPerImage",
    "src_offset", "srcOffset", "dst_offset", "dstOffset", NULL};

static bool parse_side(const doe_json_value *object, const char *side, bool texture, doe_copy_side *out,
    size_t index, doe_stream_error *error)
{
    side_names n = side_field(side, "handle", "Handle");
    if (!stream_read_u64(object, n.snake, n.camel, UINT64_MAX, true, 0, &out->handle, index, error)) return false;
    const char *kind = NULL;
    n = side_field(side, "kind", "Kind");
    if (!stream_read_string(object, n.snake, n.camel, &kind, index, error)) return false;
    if (kind && doe_host_casecmp(kind, texture ? "texture" : "buffer") != 0)
        return stream_reject(error, index, "\"%s\" \"%s\" contradicts the copy direction", n.snake, kind);
    bool present = false;
    n = side_field(side, "bytes_per_row", "BytesPerRow");
    if (!read_u32_of(object, n.snake, n.camel, NULL, &present, &out->bytes_per_row, index, error)) return false;
    n = side_field(side, "rows_per_image", "RowsPerImage");
    if (!read_u32_of(object, n.snake, n.camel, NULL, &present, &out->rows_per_image, index, error)) return false;
    n = side_field(side, "offset", "Offset");
    if (!stream_read_u64(object, n.snake, n.camel, UINT64_MAX, false, 0, &out->offset, index, error)) return false;
    const char *texture_fields[][2] = {{"width", "Width"}, {"height", "Height"}, {"depth_or_array_layers", "DepthOrArrayLayers"},
        {"depth", NULL}, {"format", "Format"}, {"usage", "Usage"}, {"dimension", "Dimension"},
        {"view_dimension", "ViewDimension"}, {"mip_level", "MipLevel"}, {"sample_count", "SampleCount"}, {"aspect", "Aspect"}};
    if (!texture) {
        for (size_t i = 0; i < sizeof(texture_fields) / sizeof(texture_fields[0]); i++) {
            n = side_field(side, texture_fields[i][0], texture_fields[i][1] ? texture_fields[i][1] : "");
            if (doe_json_member_value(object, n.snake) || (texture_fields[i][1] && doe_json_member_value(object, n.camel)))
                return stream_reject(error, index, "\"%s\" has no effect on a buffer side and is rejected", n.snake);
        }
        return true;
    }
    /* doe-zig-runtime ignores a texture side's offset; a nonzero one is an error here. */
    if (out->offset) return stream_reject(error, index, "\"%s_offset\" has no effect on a texture side", side);
    doe_texture_desc *t = &out->texture;
    t->handle = out->handle;
    side_names depth = side_field(side, "depth_or_array_layers", "DepthOrArrayLayers");
    side_names depth_short = side_field(side, "depth", "");
    n = side_field(side, "width", "Width");
    if (!read_u32_of(object, n.snake, n.camel, NULL, &present, &t->width, index, error)) return false;
    n = side_field(side, "height", "Height");
    if (!read_u32_of(object, n.snake, n.camel, NULL, &present, &t->height, index, error)) return false;
    if (!read_u32_of(object, depth.snake, depth.camel, depth_short.snake, &present, &t->depth, index, error)) return false;
    /* Zero extents are one texel, as doe-zig-runtime creates them. */
    if (!t->width) t->width = 1;
    if (!t->height) t->height = 1;
    if (!t->depth) t->depth = 1;
    t->format = DOE_FORMAT_R8_UNORM; /* doe-zig-runtime's undefined-format texture */
    n = side_field(side, "format", "Format");
    if (!read_format(object, n.snake, n.camel, NULL, &present, &t->format, index, error)) return false;
    n = side_field(side, "usage", "Usage");
    if (!stream_read_u64(object, n.snake, n.camel, UINT32_MAX, false, 0, &t->usage, index, error)) return false;
    n = side_field(side, "dimension", "Dimension");
    if (!require_string(object, n.snake, n.camel, "2d", index, error)) return false;
    n = side_field(side, "view_dimension", "ViewDimension");
    if (!require_string(object, n.snake, n.camel, t->depth > 1 ? "2d-array" : "2d", index, error)) return false;
    n = side_field(side, "mip_level", "MipLevel");
    if (!read_u32_of(object, n.snake, n.camel, NULL, &present, &out->mip_level, index, error)) return false;
    uint32_t largest = t->width > t->height ? t->width : t->height, levels = 1;
    while (largest >>= 1) levels++;
    if (out->mip_level >= levels)
        return stream_reject(error, index, "\"%s\" %u is beyond the %ux%u texture's %u levels", n.snake, out->mip_level,
            t->width, t->height, levels);
    t->mip_levels = out->mip_level + 1;
    n = side_field(side, "sample_count", "SampleCount");
    if (!require_u32(object, n.snake, n.camel, 1, 0, index, error)) return false;
    n = side_field(side, "aspect", "Aspect");
    return require_string(object, n.snake, n.camel, "all", index, error);
}

bool stream_parse_copy(const doe_json_value *object, const char *name, doe_command *command, size_t index,
    doe_stream_error *error)
{
    if (!stream_only_members(object, COPY_FIELDS, index, "copy", error)) return false;
    if (!stream_read_u64(object, "bytes", NULL, UINT64_MAX, true, 0, &command->as.copy.bytes, index, error)) return false;
    if (command->as.copy.bytes == 0) return stream_reject(error, index, "copy \"bytes\" must be nonzero");
    const char *direction = NULL;
    if (!stream_read_string(object, "direction", NULL, &direction, index, error)) return false;
    doe_copy_direction d = DOE_COPY_BUFFER_TO_BUFFER;
    const char *from = direction ? direction : name;
    if (direction) {
        if (!doe_host_casecmp(from, "buffer_to_buffer")) d = DOE_COPY_BUFFER_TO_BUFFER;
        else if (!doe_host_casecmp(from, "buffer_to_texture")) d = DOE_COPY_BUFFER_TO_TEXTURE;
        else if (!doe_host_casecmp(from, "texture_to_buffer")) d = DOE_COPY_TEXTURE_TO_BUFFER;
        else if (!doe_host_casecmp(from, "texture_to_texture")) d = DOE_COPY_TEXTURE_TO_TEXTURE;
        else return stream_reject(error, index, "copy \"direction\" \"%s\" is not a copy direction", direction);
    } else if (!doe_host_casecmp(from, "copy_buffer_to_texture") || !doe_host_casecmp(from, "copy_texture") ||
        !doe_host_casecmp(from, "copyBufferToTexture")) {
        d = DOE_COPY_BUFFER_TO_TEXTURE;
    } else if (!doe_host_casecmp(from, "copy_texture_to_buffer") || !doe_host_casecmp(from, "copyTextureToBuffer")) {
        d = DOE_COPY_TEXTURE_TO_BUFFER;
    } else if (!doe_host_casecmp(from, "copy_texture_to_texture")) {
        d = DOE_COPY_TEXTURE_TO_TEXTURE;
    }
    command->as.copy.direction = d;
    bool src_texture = d == DOE_COPY_TEXTURE_TO_BUFFER || d == DOE_COPY_TEXTURE_TO_TEXTURE;
    bool dst_texture = d == DOE_COPY_BUFFER_TO_TEXTURE || d == DOE_COPY_TEXTURE_TO_TEXTURE;
    if (!parse_side(object, "src", src_texture, &command->as.copy.src, index, error)) return false;
    return parse_side(object, "dst", dst_texture, &command->as.copy.dst, index, error);
}

bool stream_parse_texture_write(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "texture_handle", "textureHandle", "handle",
        "resource_handle", "resourceHandle", "target_handle", "targetHandle", "width", "target_width", "targetWidth", "height", "target_height",
        "targetHeight", "depth_or_array_layers", "depthOrArrayLayers", "depth", "mip_level", "mipLevel",
        "sample_count", "sampleCount", "format", "target_format", "targetFormat", "usage", "dimension",
        "view_dimension", "viewDimension", "aspect", "bytesPerRow", "rows_per_image", "rowsPerImage", "offset", "data",
        NULL};
    if (!stream_only_members(object, allowed, index, "texture_write", error)) return false;
    doe_texture_desc *t = &command->as.texture_write.texture;
    if (!read_handle(object, &t->handle, index, error)) return false;
    bool present = false;
    if (!read_u32_of(object, "width", "target_width", "targetWidth", &present, &t->width, index, error)) return false;
    if (!present) t->width = 1;
    if (!read_u32_of(object, "height", "target_height", "targetHeight", &present, &t->height, index, error)) return false;
    if (!present) t->height = 1;
    if (!read_u32_of(object, "depth_or_array_layers", "depthOrArrayLayers", "depth", &present, &t->depth, index, error)) return false;
    if (!present) t->depth = 1;
    if (!t->width || !t->height || !t->depth) return stream_reject(error, index, "texture_write extents must be nonzero");
    t->format = DOE_FORMAT_RGBA8_UNORM;
    if (!read_format(object, "format", "target_format", "targetFormat", &present, &t->format, index, error)) return false;
    if (!stream_read_u64(object, "usage", NULL, UINT32_MAX, false, TEXTURE_USAGE_COPY_DST | TEXTURE_USAGE_TEXTURE_BINDING,
            &t->usage, index, error)) return false;
    if (!require_u32(object, "mip_level", "mipLevel", 0, 0, index, error) ||
        !require_u32(object, "sample_count", "sampleCount", 1, 1, index, error) ||
        !require_string(object, "dimension", NULL, "2d", index, error) ||
        !require_string(object, "view_dimension", "viewDimension", t->depth > 1 ? "2d-array" : "2d", index, error) ||
        !require_string(object, "aspect", NULL, "all", index, error)) return false;
    if (!read_u32_of(object, "bytesPerRow", NULL, NULL, &present, &command->as.texture_write.bytes_per_row, index, error) ||
        !read_u32_of(object, "rows_per_image", "rowsPerImage", NULL, &present, &command->as.texture_write.rows_per_image,
            index, error)) return false;
    if (!stream_read_u64(object, "offset", NULL, UINT64_MAX, false, 0, &command->as.texture_write.offset, index, error)) return false;
    const doe_json_value *data = doe_json_member_value(object, "data");
    if (!data) return true;
    if (data->kind != DOE_JSON_ARRAY) return stream_reject(error, index, "texture_write \"data\" must be an array of bytes");
    if (!data->count) return true;
    uint8_t *bytes = malloc(data->count);
    if (!bytes) return stream_reject(error, index, "out of memory");
    for (size_t i = 0; i < data->count; i++) {
        uint64_t byte;
        if (!doe_json_as_u64(data->items[i], UINT8_MAX, &byte)) {
            free(bytes);
            return stream_reject(error, index, "texture_write \"data\"[%zu] must be an integer in [0, 255]", i);
        }
        bytes[i] = (uint8_t)byte;
    }
    command->as.texture_write.data = bytes;
    command->as.texture_write.data_size = data->count;
    return true;
}

bool stream_parse_texture_query(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "texture_handle", "textureHandle", "handle",
        "resource_handle", "resourceHandle", "target_handle", "targetHandle", "width", "height", "depth_or_array_layers", "depthOrArrayLayers", "depth",
        "format", "target_format", "targetFormat", "dimension", "view_dimension", "viewDimension", "sample_count",
        "sampleCount", "usage", NULL};
    if (!stream_only_members(object, allowed, index, "texture_query", error)) return false;
    doe_texture_query *q = &command->as.texture_query;
    if (!read_handle(object, &q->handle, index, error)) return false;
    if (!read_u32_of(object, "width", NULL, NULL, &q->has_width, &q->width, index, error) ||
        !read_u32_of(object, "height", NULL, NULL, &q->has_height, &q->height, index, error) ||
        !read_u32_of(object, "depth_or_array_layers", "depthOrArrayLayers", "depth", &q->has_depth, &q->depth, index, error) ||
        !read_u32_of(object, "sample_count", "sampleCount", NULL, &q->has_sample_count, &q->sample_count, index, error) ||
        !read_format(object, "format", "target_format", "targetFormat", &q->has_format, &q->format, index, error)) return false;
    const doe_json_value *usage = doe_json_member_value(object, "usage");
    q->has_usage = usage != NULL;
    if (usage && !doe_json_as_u64(usage, UINT32_MAX, &q->usage))
        return stream_reject(error, index, "\"usage\" must be an integer in [0, 4294967295]");
    const char *dimension = NULL, *view = NULL;
    if (!stream_read_string(object, "dimension", NULL, &dimension, index, error) ||
        !stream_read_string(object, "view_dimension", "viewDimension", &view, index, error)) return false;
    q->has_dimension = dimension != NULL;
    q->dimension_2d = dimension && doe_host_casecmp(dimension, "2d") == 0;
    q->has_view_dimension = view != NULL;
    q->view_dimension = !view ? 0 : !doe_host_casecmp(view, "2d") ? 0 : !doe_host_casecmp(view, "2d-array") ? 1 : 2;
    return true;
}

bool stream_parse_texture_destroy(const doe_json_value *object, doe_command *command, size_t index,
    doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "texture_handle", "textureHandle", "handle",
        "resource_handle", "resourceHandle", "target_handle", "targetHandle", NULL};
    if (!stream_only_members(object, allowed, index, "texture_destroy", error)) return false;
    return read_handle(object, &command->as.texture_destroy.handle, index, error);
}
