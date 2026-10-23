/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include <string.h>

static bool image_spelling(const Source *s, NameSpan name, uint32_t *kind, ImageType *image, bool *generic) {
    static const struct { const char *name; uint32_t dimension, flags; bool generic, storage; } textures[] = {
        {"texture_1d", 1u, 0u, true, false}, {"texture_2d", 2u, 0u, true, false},
        {"texture_2d_array", 3u, 0u, true, false}, {"texture_cube", 4u, 0u, true, false},
        {"texture_cube_array", 5u, 0u, true, false}, {"texture_3d", 6u, 0u, true, false},
        {"texture_multisampled_2d", 2u, DOE_COMPILER_HANDLE_MULTISAMPLED, true, false},
        {"texture_storage_1d", 1u, 0u, true, true}, {"texture_storage_2d", 2u, 0u, true, true},
        {"texture_storage_2d_array", 3u, 0u, true, true}, {"texture_storage_3d", 6u, 0u, true, true},
        {"texture_depth_2d", 2u, DOE_COMPILER_HANDLE_DEPTH, false, false},
        {"texture_depth_2d_array", 3u, DOE_COMPILER_HANDLE_DEPTH, false, false},
        {"texture_depth_cube", 4u, DOE_COMPILER_HANDLE_DEPTH, false, false},
        {"texture_depth_cube_array", 5u, DOE_COMPILER_HANDLE_DEPTH, false, false},
        {"texture_depth_multisampled_2d", 2u, DOE_COMPILER_HANDLE_DEPTH | DOE_COMPILER_HANDLE_MULTISAMPLED, false, false}
    };
    *image = (ImageType){0};
    *generic = false;
    if (source_span_is(s, name, "sampler") || source_span_is(s, name, "sampler_comparison")) {
        *kind = TYPE_SAMPLER;
        if (source_span_is(s, name, "sampler_comparison")) image->flags = DOE_COMPILER_HANDLE_COMPARISON;
        return true;
    }
    *kind = TYPE_TEXTURE;
    for (size_t i = 0u; i < sizeof(textures) / sizeof(textures[0]); ++i)
        if (source_span_is(s, name, textures[i].name)) {
            image->dimension = textures[i].dimension;
            image->flags = textures[i].flags;
            image->access = textures[i].storage ? 0u : DOE_COMPILER_READ;
            *generic = textures[i].generic;
            return true;
        }
    return false;
}
bool sema_image_type(Source *s, TypeRef ref, TypeId scalar, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    const NameSpan name = source_name_at(s, ref.offset);
    ImageType image;
    uint32_t kind;
    bool generic;
    if (!image_spelling(s, name, &kind, &image, &generic))
        return source_error(s, DOE_COMPILER_UNSUPPORTED, name.offset, "unsupported texture type");
    if (generic != (ref.syntax != NO_ID))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "incorrect handle type arguments");
    if (kind == TYPE_SAMPLER) scalar = IR_VOID;
    else if (!generic) scalar = IR_F32;
    else {
        const GenericSyntax *g = &c->generic_syntax[ref.syntax];
        if (image.access == 0u) {
            if (g->element.syntax != NO_ID || g->count_expression == NO_ID ||
                c->expressions[g->count_expression].kind != EXPRESSION_NAME)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "storage texture requires format and access enumerants");
            const NameSpan format = source_name_at(s, g->element.offset);
            const NameSpan access = c->expressions[g->count_expression].value.name;
            if (sema_module_declared(s, format) || sema_module_declared(s, access))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "storage texture enumerant is shadowed");
            if (!image_format(s, format, &image.format, &scalar))
                return source_error(s, DOE_COMPILER_UNSUPPORTED, format.offset, "unsupported storage texture format");
            if (source_span_is(s, access, "read")) image.access = DOE_COMPILER_READ;
            else if (source_span_is(s, access, "write")) image.access = DOE_COMPILER_WRITE;
            else if (source_span_is(s, access, "read_write")) image.access = DOE_COMPILER_READ_WRITE;
            else return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, access.offset, "invalid storage texture access");
        } else if (g->count_expression != NO_ID || (scalar != IR_I32 && scalar != IR_U32 && scalar != IR_F32)) {
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "sampled texture requires one i32, u32 or f32 type");
        }
    }
    return type_image_intern(s, kind, scalar, image, out);
}
uint32_t image_builtin_kind(const Source *s, NameSpan name) {
    static const struct { const char *name; ImageOperation operation; } builtins[] = {
        {"textureLoad", IMAGE_LOAD}, {"textureStore", IMAGE_STORE},
        {"textureDimensions", IMAGE_DIMENSIONS}, {"textureNumLevels", IMAGE_LEVELS},
        {"textureNumLayers", IMAGE_LAYERS}, {"textureNumSamples", IMAGE_SAMPLES},
        {"textureSampleLevel", IMAGE_SAMPLE_LEVEL}
    };
    for (size_t i = 0u; i < sizeof(builtins) / sizeof(builtins[0]); ++i)
        if (source_span_is(s, name, builtins[i].name)) {
            return (uint32_t)builtins[i].operation;
        }
    return NO_ID;
}
TypeId sema_image_result(Source *s, const Expression *node, const TypeRecord *t, ImageOperation op) {
    const ImageType image = t->state.image;
    const bool storage = image.format != 0u, arrayed = image_arrayed(image.dimension);
    const bool multisampled = (image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u;
    const bool cube = image.dimension == DOE_COMPILER_VIEW_CUBE || image.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY;
    uint32_t arity = 1u, width = 1u;
    TypeId scalar = IR_U32;
    switch (op) {
        case IMAGE_LOAD:
            if (cube || image.access == DOE_COMPILER_WRITE) goto invalid;
            arity = 2u + (arrayed ? 1u : 0u) + (storage ? 0u : 1u);
            width = (image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u ? 1u : 4u;
            scalar = t->element;
            break;
        case IMAGE_STORE:
            if (!storage || image.access == DOE_COMPILER_READ) goto invalid;
            arity = 3u + (arrayed ? 1u : 0u);
            scalar = IR_VOID;
            break;
        case IMAGE_DIMENSIONS:
            if (!storage && !multisampled && node->right == 2u) arity = 2u;
            width = cube ? 2u : image_coordinate_width(image.dimension);
            break;
        case IMAGE_LEVELS:
            if (storage || multisampled) goto invalid;
            break;
        case IMAGE_LAYERS:
            if (!arrayed) goto invalid;
            break;
        case IMAGE_SAMPLES:
            if (!multisampled) goto invalid;
            break;
        case IMAGE_SAMPLE_LEVEL:
            if (storage || multisampled || t->element != IR_F32 || (image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u) goto invalid;
            arity = 4u + (arrayed ? 1u : 0u);
            width = 4u;
            scalar = IR_F32;
            break;
    }
    if (arity != node->right) goto invalid;
    TypeId result = scalar;
    if (width != 1u && !type_intern(s, TYPE_VECTOR, scalar, width, &result)) return NO_ID;
    return result;
invalid:
    (void)source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "no matching texture builtin overload");
    return NO_ID;
}
bool sema_image_argument(Source *s, const TypeRecord *texture, ImageOperation op, uint32_t index,
                           ExpressionValue *value) {
    const ImageType image = texture->state.image;
    if (op == IMAGE_SAMPLE_LEVEL && index == 1u) {
        const TypeId type = value->constant.type;
        if (!type_handle(s->compiler, type) || s->compiler->types[type - 7u].kind != TYPE_SAMPLER ||
            s->compiler->types[type - 7u].state.image.flags != 0u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, value->value.source_offset, "sampling requires a non-comparison sampler");
        return true;
    }
    const uint32_t coordinate = op == IMAGE_SAMPLE_LEVEL ? 2u : 1u;
    const bool data = op == IMAGE_STORE && index == 2u + (image_arrayed(image.dimension) ? 1u : 0u);
    const bool is_coordinate = (op == IMAGE_LOAD || op == IMAGE_STORE || op == IMAGE_SAMPLE_LEVEL) && index == coordinate;
    const bool level = op == IMAGE_SAMPLE_LEVEL && index == 3u + (image_arrayed(image.dimension) ? 1u : 0u);
    const uint32_t width = data ? 4u : is_coordinate ? image_coordinate_width(image.dimension) : 1u;
    TypeId scalar = data ? texture->element : (level || (is_coordinate && op == IMAGE_SAMPLE_LEVEL)) ? IR_F32 : IR_I32;
    const TypeId original = type_leaf(s->compiler, value->constant.type);
    if (scalar == IR_I32 && original == IR_U32) scalar = IR_U32;
    TypeId expected = scalar;
    if (width != 1u && !type_intern(s, TYPE_VECTOR, scalar, width, &expected)) return false;
    return sema_materialize(s, value, expected);
}
bool sema_image_auxiliaries(Source *s, TypeId texture, ImageOperation op) {
    const TypeRecord *t = &s->compiler->types[texture - 7u];
    const uint32_t dimension = t->state.image.dimension;
    const uint32_t width = image_coordinate_width(dimension);
    const bool cube = dimension == DOE_COMPILER_VIEW_CUBE || dimension == DOE_COMPILER_VIEW_CUBE_ARRAY;
    const uint32_t query = (cube ? 2u : width) + (image_arrayed(dimension) ? 1u : 0u);
    TypeId ignored;
    if (query != 1u && !type_intern(s, TYPE_VECTOR, IR_U32, query, &ignored)) return false;
    if ((op == IMAGE_LOAD || op == IMAGE_STORE) && width != 1u &&
        !type_intern(s, TYPE_VECTOR, IR_U32, width, &ignored)) return false;
    if (op == IMAGE_LOAD && !type_intern(s, TYPE_VECTOR, t->element, 4u, &ignored)) return false;
    return op != IMAGE_SAMPLE_LEVEL || !image_arrayed(dimension) ||
        type_intern(s, TYPE_VECTOR, IR_F32, width + 1u, &ignored);
}
