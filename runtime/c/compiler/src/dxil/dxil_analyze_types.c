/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_analyze_internal.h"

#include <string.h>



static bool intern_type(DxilAnalyzeParser *parser,
    const DxilIrType *type, DxilIrTypeId *out_id) {
    if (dxil_ir_intern_type(parser->module, type, out_id)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

/* Parses a scalar name only (for vector/matrix/texture elements).
 * Unknown names fail UnknownType, known non-scalar-appropriate names
 * fail InvalidType; both at sema stage. */
static bool parse_element_scalar(DxilAnalyzeParser *parser,
    DxilIrScalar *out_scalar) {
    DxilIrScalar scalar = DXIL_IR_SCALAR_VOID;
    if (parser->tok.kind != DXIL_TOK_IDENT) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    if (dxil_parse_scalar_name_to_enum(parser->tok.text, parser->tok.len,
            &scalar)) {
        *out_scalar = scalar;
        dxil_parse_advance(parser);
        return true;
    }
    if (dxil_parse_find_struct(parser, parser->tok.text, parser->tok.len) >= 0) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_TYPE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    dxil_parse_fail_tok(parser, DXIL_DIAG_UNKNOWN_TYPE, DXIL_DIAG_STAGE_SEMA);
    return false;
}

bool dxil_parse_type(DxilAnalyzeParser *parser,
    DxilIrTypeId *out_id) {
    const char *text = NULL;
    size_t len = 0;
    size_t offset = 0;
    DxilIrScalar scalar = DXIL_IR_SCALAR_VOID;
    DxilIrType type;
    if (parser->failed) {
        return false;
    }
    if (parser->tok.kind != DXIL_TOK_IDENT) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    text = parser->tok.text;
    len = parser->tok.len;
    offset = parser->tok.offset;
    if (dxil_parse_scalar_name_to_enum(text, len, &scalar)) {
        dxil_parse_advance(parser);
        *out_id = dxil_parse_scalar_type_id(parser, scalar);
        return true;
    }
    if ((len == 4 || len == 4) && text[0] == 'v' && text[1] == 'e'
        && text[2] == 'c'
        && (text[3] == '2' || text[3] == '3' || text[3] == '4')) {
        uint8_t vec_len = (uint8_t)(text[3] - '0');
        DxilIrScalar elem = DXIL_IR_SCALAR_VOID;
        dxil_parse_advance(parser);
        if (!dxil_parse_expect_punct(parser, '<')) {
            return false;
        }
        if (!parse_element_scalar(parser, &elem)) {
            return false;
        }
        if (elem == DXIL_IR_SCALAR_VOID) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_TYPE,
                DXIL_DIAG_STAGE_SEMA, offset);
            return false;
        }
        if (!dxil_parse_expect_punct(parser, '>')) {
            return false;
        }
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_VECTOR;
        type.elem = dxil_parse_scalar_type_id(parser, elem);
        type.len = vec_len;
        return intern_type(parser, &type, out_id);
    }
    if (len == 6 && text[0] == 'm' && text[1] == 'a'
        && text[2] == 't' && text[3] >= '2' && text[3] <= '4'
        && text[4] == 'x' && text[5] >= '2' && text[5] <= '4') {
        uint8_t columns = (uint8_t)(text[3] - '0');
        uint8_t rows = (uint8_t)(text[5] - '0');
        DxilIrScalar elem = DXIL_IR_SCALAR_VOID;
        dxil_parse_advance(parser);
        if (!dxil_parse_expect_punct(parser, '<')) {
            return false;
        }
        if (!parse_element_scalar(parser, &elem)) {
            return false;
        }
        if (elem != DXIL_IR_SCALAR_F32
            && elem != DXIL_IR_SCALAR_F16
            && elem != DXIL_IR_SCALAR_ABSTRACT_FLOAT) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_TYPE,
                DXIL_DIAG_STAGE_SEMA, offset);
            return false;
        }
        if (!dxil_parse_expect_punct(parser, '>')) {
            return false;
        }
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_MATRIX;
        type.elem = dxil_parse_scalar_type_id(parser, elem);
        type.columns = columns;
        type.rows = rows;
        return intern_type(parser, &type, out_id);
    }
    if (len == 5 && memcmp(text, "array", 5) == 0) {
        DxilIrTypeId elem_id = DXIL_IR_INVALID_ID;
        bool has_len = false;
        uint32_t array_len = 0;
        dxil_parse_advance(parser);
        if (!dxil_parse_expect_punct(parser, '<')) {
            return false;
        }
        if (!dxil_parse_type(parser, &elem_id)) {
            return false;
        }
        if (dxil_parse_accept_punct(parser, ',')) {
            uint64_t value = 0;
            if (!dxil_parse_expect_int_value(parser, &value)) {
                return false;
            }
            if (value > UINT32_MAX) {
                dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_WGSL,
                    DXIL_DIAG_STAGE_PARSER);
                return false;
            }
            has_len = true;
            array_len = (uint32_t)value;
        }
        if (!dxil_parse_expect_punct(parser, '>')) {
            return false;
        }
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_ARRAY;
        type.elem = elem_id;
        type.has_len = has_len;
        type.array_len = array_len;
        return intern_type(parser, &type, out_id);
    }
    if (len == 6 && memcmp(text, "atomic", 6) == 0) {
        DxilIrScalar elem = DXIL_IR_SCALAR_VOID;
        dxil_parse_advance(parser);
        if (!dxil_parse_expect_punct(parser, '<')) {
            return false;
        }
        if (!parse_element_scalar(parser, &elem)) {
            return false;
        }
        if (elem != DXIL_IR_SCALAR_I32
            && elem != DXIL_IR_SCALAR_U32) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_TYPE,
                DXIL_DIAG_STAGE_SEMA, offset);
            return false;
        }
        if (!dxil_parse_expect_punct(parser, '>')) {
            return false;
        }
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_ATOMIC;
        type.elem = dxil_parse_scalar_type_id(parser, elem);
        return intern_type(parser, &type, out_id);
    }
    if (len == 7 && memcmp(text, "sampler", 7) == 0) {
        dxil_parse_advance(parser);
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_SAMPLER;
        return intern_type(parser, &type, out_id);
    }
    if (len == 18 && memcmp(text, "sampler_comparison", 18) == 0) {
        dxil_parse_advance(parser);
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_SAMPLER_COMPARISON;
        return intern_type(parser, &type, out_id);
    }
    if (len == 16 && memcmp(text, "texture_depth_2d", 16) == 0) {
        dxil_parse_advance(parser);
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_TEXTURE_DEPTH_2D;
        return intern_type(parser, &type, out_id);
    }
    if (len == 18 && memcmp(text, "texture_depth_cube", 18) == 0) {
        dxil_parse_advance(parser);
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE;
        return intern_type(parser, &type, out_id);
    }
    {
        DxilIrTypeKind texture_kind;
        const char *texture_name = NULL;
        size_t texture_len = 0;
        if (len == 10 && memcmp(text, "texture_1d", 10) == 0) {
            texture_kind = DXIL_IR_TYPE_TEXTURE_1D;
            texture_name = "texture_1d";
            texture_len = 10;
        } else if (len == 10 && memcmp(text, "texture_2d", 10) == 0) {
            texture_kind = DXIL_IR_TYPE_TEXTURE_2D;
            texture_name = "texture_2d";
            texture_len = 10;
        } else if (len == 16
            && memcmp(text, "texture_2d_array", 16) == 0) {
            texture_kind = DXIL_IR_TYPE_TEXTURE_2D_ARRAY;
            texture_name = "texture_2d_array";
            texture_len = 16;
        } else if (len == 12
            && memcmp(text, "texture_cube", 12) == 0) {
            texture_kind = DXIL_IR_TYPE_TEXTURE_CUBE;
            texture_name = "texture_cube";
            texture_len = 12;
        } else if (len == 22
            && memcmp(text, "texture_multisampled_2d", 22) == 0) {
            texture_kind = DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D;
            texture_name = "texture_multisampled_2d";
            texture_len = 22;
        } else if (len == 10 && memcmp(text, "texture_3d", 10) == 0) {
            texture_kind = DXIL_IR_TYPE_TEXTURE_3D;
            texture_name = "texture_3d";
            texture_len = 10;
        } else {
            texture_kind = DXIL_IR_TYPE_SAMPLER;
            texture_name = NULL;
            texture_len = 0;
        }
        if (texture_name != NULL) {
            DxilIrScalar elem = DXIL_IR_SCALAR_VOID;
            (void)texture_len;
            dxil_parse_advance(parser);
            if (!dxil_parse_expect_punct(parser, '<')) {
                return false;
            }
            if (!parse_element_scalar(parser, &elem)) {
                return false;
            }
            if (elem != DXIL_IR_SCALAR_F32
                && elem != DXIL_IR_SCALAR_I32
                && elem != DXIL_IR_SCALAR_U32) {
                dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_TYPE,
                    DXIL_DIAG_STAGE_SEMA, offset);
                return false;
            }
            if (!dxil_parse_expect_punct(parser, '>')) {
                return false;
            }
            memset(&type, 0, sizeof(type));
            type.kind = texture_kind;
            type.elem = dxil_parse_scalar_type_id(parser, elem);
            return intern_type(parser, &type, out_id);
        }
    }
    if (len == 18 && memcmp(text, "texture_storage_2d", 18) == 0) {
        static const struct DxilAnalyzeFormat {
            const char *name;
            DxilIrTextureFormat format;
        } formats[] = {
            { "rgba8unorm", DXIL_IR_TEXFMT_RGBA8UNORM },
            { "rgba8snorm", DXIL_IR_TEXFMT_RGBA8SNORM },
            { "rgba8uint", DXIL_IR_TEXFMT_RGBA8UINT },
            { "rgba8sint", DXIL_IR_TEXFMT_RGBA8SINT },
            { "rgba16uint", DXIL_IR_TEXFMT_RGBA16UINT },
            { "rgba16sint", DXIL_IR_TEXFMT_RGBA16SINT },
            { "rgba16float", DXIL_IR_TEXFMT_RGBA16FLOAT },
            { "r32uint", DXIL_IR_TEXFMT_R32UINT },
            { "r32sint", DXIL_IR_TEXFMT_R32SINT },
            { "r32float", DXIL_IR_TEXFMT_R32FLOAT },
            { "rg32uint", DXIL_IR_TEXFMT_RG32UINT },
            { "rg32sint", DXIL_IR_TEXFMT_RG32SINT },
            { "rg32float", DXIL_IR_TEXFMT_RG32FLOAT },
            { "rgba32uint", DXIL_IR_TEXFMT_RGBA32UINT },
            { "rgba32sint", DXIL_IR_TEXFMT_RGBA32SINT },
            { "rgba32float", DXIL_IR_TEXFMT_RGBA32FLOAT },
        };
        const char *format_text = NULL;
        size_t format_len = 0;
        size_t format_offset = 0;
        const char *access_text = NULL;
        size_t access_len = 0;
        DxilIrTextureFormat format = DXIL_IR_TEXFMT_RGBA8UNORM;
        DxilIrAccessMode access = DXIL_IR_ACCESS_READ_WRITE;
        size_t fi = 0;
        bool found = false;
        dxil_parse_advance(parser);
        if (!dxil_parse_expect_punct(parser, '<')) {
            return false;
        }
        if (!dxil_parse_expect_ident(parser, &format_text, &format_len,
                &format_offset)) {
            return false;
        }
        for (fi = 0; fi < sizeof(formats) / sizeof(formats[0]); fi++) {
            if (strlen(formats[fi].name) == format_len
                && memcmp(formats[fi].name, format_text,
                    format_len) == 0) {
                format = formats[fi].format;
                found = true;
                break;
            }
        }
        if (!found) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_TYPE,
                DXIL_DIAG_STAGE_SEMA, format_offset);
            return false;
        }
        if (!dxil_parse_expect_punct(parser, ',')) {
            return false;
        }
        if (!dxil_parse_expect_ident(parser, &access_text, &access_len, NULL)) {
            return false;
        }
        if (access_len == 4 && memcmp(access_text, "read", 4) == 0) {
            access = DXIL_IR_ACCESS_READ;
        } else if (access_len == 5
            && memcmp(access_text, "write", 5) == 0) {
            access = DXIL_IR_ACCESS_WRITE;
        } else if (access_len == 10
            && memcmp(access_text, "read_write", 10) == 0) {
            access = DXIL_IR_ACCESS_READ_WRITE;
        } else {
            dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_TYPE,
                DXIL_DIAG_STAGE_SEMA);
            return false;
        }
        if (!dxil_parse_expect_punct(parser, '>')) {
            return false;
        }
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
        type.format = format;
        type.access = access;
        return intern_type(parser, &type, out_id);
    }
    {
        int struct_id = dxil_parse_find_struct(parser, text, len);
        if (struct_id >= 0) {
            dxil_parse_advance(parser);
            memset(&type, 0, sizeof(type));
            type.kind = DXIL_IR_TYPE_STRUCT;
            type.struct_id = (DxilIrStructId)struct_id;
            return intern_type(parser, &type, out_id);
        }
    }
    dxil_parse_fail_at(parser, DXIL_DIAG_UNKNOWN_TYPE, DXIL_DIAG_STAGE_SEMA,
        offset);
    return false;
}

bool dxil_parse_is_type_starter(DxilAnalyzeParser *parser) {
    const char *text = parser->tok.text;
    size_t len = parser->tok.len;
    DxilIrScalar scalar = DXIL_IR_SCALAR_VOID;
    if (parser->tok.kind != DXIL_TOK_IDENT) {
        return false;
    }
    if (dxil_parse_scalar_name_to_enum(text, len, &scalar)) {
        return true;
    }
    if (len == 4 && text[0] == 'v' && text[1] == 'e'
        && text[2] == 'c'
        && (text[3] == '2' || text[3] == '3' || text[3] == '4')) {
        return true;
    }
    if (len == 6 && text[0] == 'm' && text[1] == 'a'
        && text[2] == 't' && text[3] >= '2' && text[3] <= '4'
        && text[4] == 'x' && text[5] >= '2' && text[5] <= '4') {
        return true;
    }
    if ((len == 5 && memcmp(text, "array", 5) == 0)
        || (len == 6 && memcmp(text, "atomic", 6) == 0)
        || (len == 7 && memcmp(text, "sampler", 7) == 0)
        || (len == 18 && memcmp(text, "sampler_comparison", 18) == 0)
        || (len == 10 && memcmp(text, "texture_1d", 10) == 0)
        || (len == 10 && memcmp(text, "texture_2d", 10) == 0)
        || (len == 16 && memcmp(text, "texture_2d_array", 16) == 0)
        || (len == 12 && memcmp(text, "texture_cube", 12) == 0)
        || (len == 22
            && memcmp(text, "texture_multisampled_2d", 22) == 0)
        || (len == 10 && memcmp(text, "texture_3d", 10) == 0)
        || (len == 16 && memcmp(text, "texture_depth_2d", 16) == 0)
        || (len == 18 && memcmp(text, "texture_depth_cube", 18) == 0)
        || (len == 18 && memcmp(text, "texture_storage_2d", 18) == 0)) {
        return true;
    }
    return dxil_parse_find_struct(parser, text, len) >= 0;
}

/* Parses a raw '@' attribute list into attrs[0..count). Each
 * attribute is '@' name ['(' int [, int [, int]] | ident [, ident]
 * ')']. Unknown shapes fail UnexpectedToken. */
bool dxil_parse_raw_attrs(DxilAnalyzeParser *parser,
    DxilAnalyzeAttr *attrs, uint32_t *out_count) {
    uint32_t count = 0;
    while (dxil_parse_is_punct(parser, '@')) {
        DxilAnalyzeAttr *attr = NULL;
        dxil_parse_advance(parser);
        if (count >= DXIL_ANALYZE_MAX_ATTRS) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA);
            return false;
        }
        attr = &attrs[count];
        memset(attr, 0, sizeof(*attr));
        if (!dxil_parse_expect_ident(parser, &attr->name, &attr->name_len,
                &attr->offset)) {
            return false;
        }
        if (dxil_parse_accept_punct(parser, '(')) {
            if (parser->tok.kind == DXIL_TOK_INT) {
                while (attr->int_count < 3) {
                    uint64_t value = 0;
                    if (!dxil_parse_expect_int_value(parser, &value)) {
                        return false;
                    }
                    attr->int_values[attr->int_count] = value;
                    attr->int_count += 1;
                    if (!dxil_parse_accept_punct(parser, ',')) {
                        break;
                    }
                }
                if (attr->int_count >= 3 && dxil_parse_is_punct(parser, ',')) {
                    dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
            } else if (parser->tok.kind == DXIL_TOK_IDENT) {
                if (!dxil_parse_expect_ident(parser, &attr->ident,
                        &attr->ident_len, NULL)) {
                    return false;
                }
                attr->has_ident = true;
                if (dxil_parse_accept_punct(parser, ',')) {
                    if (!dxil_parse_expect_ident(parser, &attr->ident2,
                            &attr->ident2_len, NULL)) {
                        return false;
                    }
                    attr->has_ident2 = true;
                }
            }
            if (!dxil_parse_expect_punct(parser, ')')) {
                return false;
            }
        }
        count += 1;
    }
    *out_count = count;
    return true;
}

const DxilAnalyzeAttr *dxil_parse_find_attr(
    const DxilAnalyzeAttr *attrs, uint32_t count, const char *name,
    bool *duplicate) {
    const DxilAnalyzeAttr *found = NULL;
    uint32_t i = 0;
    *duplicate = false;
    for (i = 0; i < count; i++) {
        if (strlen(name) == attrs[i].name_len
            && memcmp(name, attrs[i].name, attrs[i].name_len) == 0) {
            if (found != NULL) {
                *duplicate = true;
                return found;
            }
            found = &attrs[i];
        }
    }
    return found;
}

/* Interprets raw attrs as IO attributes (parameters, struct fields,
 * return types). Unknown attributes fail InvalidAttribute; unknown
 * builtin names fail UnsupportedBuiltin. */
bool dxil_parse_interpret_io_attrs(DxilAnalyzeParser *parser,
    const DxilAnalyzeAttr *attrs, uint32_t count,
    DxilIrIoAttr *out_io, bool *out_has_io) {
    uint32_t i = 0;
    bool duplicate = false;
    const DxilAnalyzeAttr *builtin_attr = NULL;
    const DxilAnalyzeAttr *location_attr = NULL;
    const DxilAnalyzeAttr *interp_attr = NULL;
    const DxilAnalyzeAttr *invariant_attr = NULL;
    const DxilAnalyzeAttr *blend_attr = NULL;
    memset(out_io, 0, sizeof(*out_io));
    out_io->builtin = DXIL_IR_BUILTIN_NONE;
    *out_has_io = count > 0;
    builtin_attr = dxil_parse_find_attr(attrs, count, "builtin", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    location_attr = dxil_parse_find_attr(attrs, count, "location", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    interp_attr = dxil_parse_find_attr(attrs, count, "interpolate", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    invariant_attr = dxil_parse_find_attr(attrs, count, "invariant", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    blend_attr = dxil_parse_find_attr(attrs, count, "blend_src", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    for (i = 0; i < count; i++) {
        const DxilAnalyzeAttr *attr = &attrs[i];
        if (attr != builtin_attr && attr != location_attr
            && attr != interp_attr && attr != invariant_attr
            && attr != blend_attr) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, attr->offset);
            return false;
        }
    }
    if (builtin_attr != NULL) {
        DxilIrBuiltin builtin = DXIL_IR_BUILTIN_NONE;
        if (!builtin_attr->has_ident
            || !dxil_parse_builtin_name_to_enum(builtin_attr->ident,
                builtin_attr->ident_len, &builtin)) {
            dxil_parse_fail_at(parser, DXIL_DIAG_UNSUPPORTED_BUILTIN,
                DXIL_DIAG_STAGE_SEMA, builtin_attr->offset);
            return false;
        }
        out_io->builtin = builtin;
    }
    if (location_attr != NULL) {
        if (location_attr->int_count != 1
            || location_attr->int_values[0] > UINT32_MAX) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, location_attr->offset);
            return false;
        }
        out_io->has_location = true;
        out_io->location = (uint32_t)location_attr->int_values[0];
    }
    if (interp_attr != NULL) {
        if (!interp_attr->has_ident) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, interp_attr->offset);
            return false;
        }
        out_io->has_interpolation = true;
        if (interp_attr->ident_len == 11
            && memcmp(interp_attr->ident, "perspective", 11) == 0) {
            out_io->interpolation = DXIL_IR_INTERP_PERSPECTIVE;
        } else if (interp_attr->ident_len == 6
            && memcmp(interp_attr->ident, "linear", 6) == 0) {
            out_io->interpolation = DXIL_IR_INTERP_LINEAR;
        } else if (interp_attr->ident_len == 4
            && memcmp(interp_attr->ident, "flat", 4) == 0) {
            out_io->interpolation = DXIL_IR_INTERP_FLAT;
        } else {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, interp_attr->offset);
            return false;
        }
        if (interp_attr->has_ident2) {
            out_io->has_sampling = true;
            if (interp_attr->ident2_len == 6
                && memcmp(interp_attr->ident2, "center", 6) == 0) {
                out_io->sampling = DXIL_IR_SAMPLING_CENTER;
            } else if (interp_attr->ident2_len == 8
                && memcmp(interp_attr->ident2, "centroid", 8) == 0) {
                out_io->sampling = DXIL_IR_SAMPLING_CENTROID;
            } else if (interp_attr->ident2_len == 6
                && memcmp(interp_attr->ident2, "sample", 6) == 0) {
                out_io->sampling = DXIL_IR_SAMPLING_SAMPLE;
            } else {
                dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                    DXIL_DIAG_STAGE_SEMA, interp_attr->offset);
                return false;
            }
        }
    }
    if (invariant_attr != NULL) {
        out_io->invariant = true;
    }
    if (blend_attr != NULL) {
        if (blend_attr->int_count != 1
            || blend_attr->int_values[0] > UINT32_MAX) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, blend_attr->offset);
            return false;
        }
        out_io->has_blend_src = true;
        out_io->blend_src = (uint32_t)blend_attr->int_values[0];
    }
    return true;
}
