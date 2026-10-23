/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

bool type_handle(const CompilerWorkspace *c, TypeId type) {
    return type >= 7u && (c->types[type - 7u].kind == TYPE_TEXTURE || c->types[type - 7u].kind == TYPE_SAMPLER);
}
bool type_image_named(const Source *s, NameSpan name) {
    return source_span_is(s, name, "sampler") || source_span_is(s, name, "sampler_comparison") ||
        (name.length >= 8u && memcmp(s->source + name.offset, "texture_", 8u) == 0);
}
bool image_arrayed(uint32_t dimension) {
    return dimension == DOE_COMPILER_VIEW_2D_ARRAY || dimension == DOE_COMPILER_VIEW_CUBE_ARRAY;
}
uint32_t image_coordinate_width(uint32_t dimension) {
    if (dimension == DOE_COMPILER_VIEW_1D) return 1u;
    if (dimension == DOE_COMPILER_VIEW_2D || dimension == DOE_COMPILER_VIEW_2D_ARRAY) return 2u;
    return 3u;
}
bool type_image_intern(Source *s, uint32_t kind, TypeId scalar, ImageType image, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u; i < c->type_count; ++i) {
        const TypeRecord *t = &c->types[i];
        if (t->kind == kind && t->element == scalar &&
            t->state.image.dimension == image.dimension && t->state.image.format == image.format &&
            t->state.image.access == image.access && t->state.image.flags == image.flags) {
            *out = i + 7u;
            return true;
        }
    }
    if (c->type_count == c->limits.types)
        return source_pool_error(s, DOE_COMPILER_POOL_TYPES, (size_t)c->type_count + 1u, s->length, "handle type capacity exceeded");
    const TypeRecord t = {.kind = kind, .flags = TYPE_RESOLVED, .element = scalar,
        .state.image = image, .target = NO_ID, .aux = NO_ID};
    *out = c->type_count + 7u;
    c->types[c->type_count++] = t;
    return true;
}
bool image_format(const Source *s, NameSpan name, uint32_t *format, TypeId *scalar) {
    static const struct { const char *name; uint32_t format; TypeId scalar; } formats[] = {
        {"rgba32float", 1u, IR_F32}, {"rgba16float", 2u, IR_F32}, {"r32float", 3u, IR_F32},
        {"rgba8unorm", 4u, IR_F32}, {"rgba8snorm", 5u, IR_F32}, {"rg32float", 6u, IR_F32},
        {"rgba32sint", 21u, IR_I32}, {"rgba16sint", 22u, IR_I32}, {"rgba8sint", 23u, IR_I32},
        {"r32sint", 24u, IR_I32}, {"rg32sint", 25u, IR_I32}, {"rgba32uint", 30u, IR_U32},
        {"rgba16uint", 31u, IR_U32}, {"rgba8uint", 32u, IR_U32}, {"r32uint", 33u, IR_U32},
        {"rg32uint", 35u, IR_U32}
    };
    for (size_t i = 0u; i < sizeof(formats) / sizeof(formats[0]); ++i)
        if (source_span_is(s, name, formats[i].name)) {
            *format = formats[i].format;
            *scalar = formats[i].scalar;
            return true;
        }
    return false;
}
size_t reflect_handle_count(const CompilerWorkspace *c) {
    size_t count = 0u;
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (type_handle(c, c->resources[i].type)) ++count;
    return count;
}
bool reflect_buffers_only(Source *s) {
    const CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (type_handle(c, c->resources[i].type))
            return source_error(s, DOE_COMPILER_UNSUPPORTED, c->resources[i].name.offset,
                "texture and sampler reflection requires resource ABI6");
    return true;
}
void reflect_handles(const CompilerWorkspace *c, DoeCompilerHandleV6 *handles) {
    size_t count = 0u;
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const TypeId type = c->resources[i].type;
        if (!type_handle(c, type)) continue;
        const TypeRecord *t = &c->types[type - 7u];
        DoeCompilerHandleV6 *out = &handles[count++];
        memset(out, 0, sizeof(*out));
        out->resource_index = i;
        out->view_dimension = t->state.image.dimension;
        out->sample_scalar = t->element;
        out->storage_format = t->state.image.format;
        out->flags = t->state.image.flags;
    }
}
