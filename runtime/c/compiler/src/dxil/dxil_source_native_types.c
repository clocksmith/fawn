/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_internal.h"
#include <string.h>
#include <stdio.h>

DxilIrTypeId dxil_source_native_type(DxilSourceBridge *b, TypeId type) {
    if (type < b->module->type_count) return type;
    (void)dxil_source_native_fail(b, 0u, DXIL_INVALID_IR, "typed native DXIL type identity is invalid");
    return DXIL_IR_INVALID_ID;
}
static DxilIrScalar scalar(TypeId type) {
    switch (type) {
        case IR_VOID: case IR_PRIMITIVE_COUNT: return DXIL_IR_SCALAR_VOID;
        case IR_BOOL: return DXIL_IR_SCALAR_BOOL;
        case IR_I32: return DXIL_IR_SCALAR_I32;
        case IR_U32: return DXIL_IR_SCALAR_U32;
        case IR_F32: return DXIL_IR_SCALAR_F32;
        case IR_F16: return DXIL_IR_SCALAR_F16;
        case IR_ABSTRACT_INT: return DXIL_IR_SCALAR_ABSTRACT_INT;
        case IR_ABSTRACT_FLOAT: return DXIL_IR_SCALAR_ABSTRACT_FLOAT;
    }
    return DXIL_IR_SCALAR_VOID;
}
static DxilIrBuiltin builtin(uint32_t value) {
    switch (value) {
        case DOE_COMPILER_BUILTIN_POSITION: return DXIL_IR_BUILTIN_POSITION;
        case DOE_COMPILER_BUILTIN_VERTEX_INDEX: return DXIL_IR_BUILTIN_VERTEX_INDEX;
        case DOE_COMPILER_BUILTIN_INSTANCE_INDEX: return DXIL_IR_BUILTIN_INSTANCE_INDEX;
        case DOE_COMPILER_BUILTIN_FRONT_FACING: return DXIL_IR_BUILTIN_FRONT_FACING;
        case DOE_COMPILER_BUILTIN_FRAG_DEPTH: return DXIL_IR_BUILTIN_FRAG_DEPTH;
        case DOE_COMPILER_BUILTIN_SAMPLE_INDEX: return DXIL_IR_BUILTIN_SAMPLE_INDEX;
        case DOE_COMPILER_BUILTIN_SAMPLE_MASK: return DXIL_IR_BUILTIN_SAMPLE_MASK;
        case DOE_COMPILER_BUILTIN_SUBGROUP_SIZE: return DXIL_IR_BUILTIN_SUBGROUP_SIZE;
        case DOE_COMPILER_BUILTIN_SUBGROUP_INVOCATION_ID: return DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID;
        case DOE_COMPILER_BUILTIN_CLIP_DISTANCES: return DXIL_IR_BUILTIN_CLIP_DISTANCES;
        case DOE_COMPILER_BUILTIN_PRIMITIVE_INDEX: return DXIL_IR_BUILTIN_PRIMITIVE_INDEX;
        default: return DXIL_IR_BUILTIN_NONE;
    }
}
bool dxil_source_native_io(DxilSourceBridge *b, uint32_t owner,
    uint32_t index, DxilIrIoAttr *out) {
    const StageWorkspace *stages = b->source.stages;
    for (uint32_t i = 0u; stages != NULL && i < stages->attribute_count; ++i) {
        const InterfaceAttribute *a = &stages->attributes[i];
        if (a->owner != owner || a->index != index) continue;
        *out = (DxilIrIoAttr){.builtin = builtin(a->builtin),
            .has_location = (a->flags & STAGE_HAS_LOCATION) != 0u,
            .location = a->location_expression,
            .has_interpolation = (a->flags & STAGE_HAS_INTERPOLATION) != 0u,
            .interpolation = a->interpolation == DOE_COMPILER_INTERPOLATION_FLAT ? DXIL_IR_INTERP_FLAT :
                a->interpolation == DOE_COMPILER_INTERPOLATION_LINEAR ? DXIL_IR_INTERP_LINEAR : DXIL_IR_INTERP_PERSPECTIVE,
            .has_sampling = a->sampling != 0u,
            .sampling = a->sampling == DOE_COMPILER_SAMPLING_CENTROID ? DXIL_IR_SAMPLING_CENTROID :
                a->sampling == DOE_COMPILER_SAMPLING_SAMPLE ? DXIL_IR_SAMPLING_SAMPLE :
                a->sampling == DOE_COMPILER_SAMPLING_FIRST ? DXIL_IR_SAMPLING_FIRST :
                a->sampling == DOE_COMPILER_SAMPLING_EITHER ? DXIL_IR_SAMPLING_EITHER : DXIL_IR_SAMPLING_CENTER,
            .invariant = (a->flags & STAGE_HAS_INVARIANT) != 0u,
            .has_blend_src = (a->flags & STAGE_HAS_BLEND_SRC) != 0u,
            .blend_src = a->blend_expression};
        return true;
    }
    return false;
}
static bool texture(DxilSourceBridge *b, const TypeRecord *type, DxilIrType *out) {
    const ImageType image = type->state.image;
    if ((image.flags & DOE_COMPILER_HANDLE_EXTERNAL) != 0u)
        return dxil_source_native_fail(b, type->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
            "retained native DXIL IR has no external texture type");
    if (image.format != 0u) {
        if (image.dimension != DOE_COMPILER_VIEW_2D)
            return dxil_source_native_fail(b, type->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
                "retained native DXIL IR supports only 2D storage textures");
        out->kind = DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
        out->access = image.access == DOE_COMPILER_READ_WRITE ? DXIL_IR_ACCESS_READ_WRITE :
            image.access == DOE_COMPILER_READ ? DXIL_IR_ACCESS_READ : DXIL_IR_ACCESS_WRITE;
        switch (image.format) {
#define FORMAT(name) case DOE_COMPILER_FORMAT_##name: out->format = DXIL_IR_TEXFMT_##name; break
            FORMAT(RGBA8UNORM); FORMAT(RGBA8SNORM); FORMAT(RGBA8UINT); FORMAT(RGBA8SINT);
            FORMAT(RGBA16UINT); FORMAT(RGBA16SINT); FORMAT(RGBA16FLOAT);
            FORMAT(R32UINT); FORMAT(R32SINT); FORMAT(R32FLOAT);
            FORMAT(RG32UINT); FORMAT(RG32SINT); FORMAT(RG32FLOAT);
            FORMAT(RGBA32UINT); FORMAT(RGBA32SINT); FORMAT(RGBA32FLOAT);
#undef FORMAT
            default: return dxil_source_native_fail(b, type->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
                "texture format is outside retained native DXIL IR");
        }
    } else if ((image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u) {
        if ((image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u ||
            (image.dimension != DOE_COMPILER_VIEW_2D && image.dimension != DOE_COMPILER_VIEW_CUBE))
            return dxil_source_native_fail(b, type->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
                "depth texture shape is outside retained native DXIL IR");
        out->kind = image.dimension == DOE_COMPILER_VIEW_2D ? DXIL_IR_TYPE_TEXTURE_DEPTH_2D : DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE;
    } else if ((image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u)
        out->kind = DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D;
    else switch (image.dimension) {
        case DOE_COMPILER_VIEW_1D: out->kind = DXIL_IR_TYPE_TEXTURE_1D; break;
        case DOE_COMPILER_VIEW_2D: out->kind = DXIL_IR_TYPE_TEXTURE_2D; break;
        case DOE_COMPILER_VIEW_2D_ARRAY: out->kind = DXIL_IR_TYPE_TEXTURE_2D_ARRAY; break;
        case DOE_COMPILER_VIEW_CUBE: out->kind = DXIL_IR_TYPE_TEXTURE_CUBE; break;
        case DOE_COMPILER_VIEW_3D: out->kind = DXIL_IR_TYPE_TEXTURE_3D; break;
        default: return dxil_source_native_fail(b, type->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
            "texture shape is outside retained native DXIL IR");
    }
    return true;
}
bool dxil_source_native_types(DxilSourceBridge *b) {
    const CompilerWorkspace *c = b->compiler;
    if (c->type_count > DXIL_IR_MAX_TYPES - IR_PRIMITIVE_COUNT)
        return dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL type capacity exceeded");
    b->module->type_count = c->type_count + IR_PRIMITIVE_COUNT;
    for (TypeId i = 0u; i < IR_PRIMITIVE_COUNT; ++i)
        b->module->types[i] = (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = scalar(i)};
    for (uint32_t i = 0u; i < c->type_count; ++i) {
        const TypeRecord *type = &c->types[i];
        DxilIrType *out = &b->module->types[i + IR_PRIMITIVE_COUNT];
        out->elem = type->element;
        switch (type->kind) {
            case TYPE_VECTOR: out->kind = DXIL_IR_TYPE_VECTOR; out->len = (uint8_t)type->count; break;
            case TYPE_MATRIX: {
                const TypeRecord *column = &c->types[type->element - IR_PRIMITIVE_COUNT];
                out->kind = DXIL_IR_TYPE_MATRIX; out->columns = (uint8_t)type->count;
                out->rows = (uint8_t)column->count; out->elem = column->element; break;
            }
            case TYPE_ARRAY: case TYPE_RUNTIME_ARRAY:
                out->kind = DXIL_IR_TYPE_ARRAY; out->has_len = type->kind == TYPE_ARRAY;
                out->array_len = type->count; break;
            case TYPE_ATOMIC: out->kind = DXIL_IR_TYPE_ATOMIC; break;
            case TYPE_STRUCT: {
                if (b->module->struct_count == DXIL_IR_MAX_STRUCTS || type->count > DXIL_IR_MAX_STRUCT_FIELDS)
                    return dxil_source_native_fail(b, type->name.offset, DXIL_OUTPUT_TOO_LARGE, "native DXIL structure capacity exceeded");
                out->kind = DXIL_IR_TYPE_STRUCT; out->struct_id = b->module->struct_count++;
                DxilIrStructDef *def = &b->module->structs[out->struct_id];
                def->name = dxil_source_native_name(b, type->name); def->field_count = type->count;
                for (uint32_t j = 0u; j < type->count; ++j) {
                    const MemberRecord *member = &c->members[type->element + j];
                    def->fields[j] = (DxilIrStructField){.name = dxil_source_native_name(b, member->name), .ty = member->type};
                    def->fields[j].has_io = dxil_source_native_io(b, STAGE_ATTRIBUTE_MEMBER, type->element + j, &def->fields[j].io);
                }
                break;
            }
            case TYPE_POINTER:
                out->kind = DXIL_IR_TYPE_REF;
                out->access = type->count == POINTER_UNIFORM || type->count == POINTER_STORAGE_READ ? DXIL_IR_ACCESS_READ : DXIL_IR_ACCESS_READ_WRITE;
                switch (type->count) {
                    case POINTER_FUNCTION: out->addr_space = DXIL_IR_ADDR_FUNCTION; break;
                    case POINTER_PRIVATE: out->addr_space = DXIL_IR_ADDR_PRIVATE; break;
                    case POINTER_WORKGROUP: out->addr_space = DXIL_IR_ADDR_WORKGROUP; break;
                    case POINTER_UNIFORM: out->addr_space = DXIL_IR_ADDR_UNIFORM; break;
                    case POINTER_STORAGE_READ: case POINTER_STORAGE_WRITE: out->addr_space = DXIL_IR_ADDR_STORAGE; break;
                    default: return dxil_source_native_fail(b, type->name.offset, DXIL_UNSUPPORTED_CONSTRUCT, "pointer space is outside retained native DXIL IR");
                }
                break;
            case TYPE_SAMPLER:
                out->kind = (type->state.image.flags & DOE_COMPILER_HANDLE_COMPARISON) != 0u ? DXIL_IR_TYPE_SAMPLER_COMPARISON : DXIL_IR_TYPE_SAMPLER; break;
            case TYPE_TEXTURE: if (!texture(b, type, out)) return false; break;
            case TYPE_ATOMIC_RESULT: case TYPE_FREXP_RESULT: case TYPE_MODF_RESULT: {
                if (b->module->struct_count == DXIL_IR_MAX_STRUCTS)
                    return dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL result structure capacity exceeded");
                out->kind = DXIL_IR_TYPE_STRUCT; out->struct_id = b->module->struct_count++;
                DxilIrStructDef *def = &b->module->structs[out->struct_id];
                char name[48];
                (void)snprintf(name, sizeof(name), "doe_native_result_%u", i);
                def->name = dxil_source_native_text(b, name); def->field_count = 2u;
                const bool atomic = type->kind == TYPE_ATOMIC_RESULT;
                def->fields[0] = (DxilIrStructField){.name = dxil_source_native_text(b, atomic ? "old_value" : "fract"), .ty = type->element};
                def->fields[1] = (DxilIrStructField){.name = dxil_source_native_text(b, atomic ? "exchanged" : type->kind == TYPE_FREXP_RESULT ? "exp" : "whole"),
                    .ty = atomic ? IR_BOOL : type->state.result.second};
                break;
            }
            default: return dxil_source_native_fail(b, 0u, DXIL_INVALID_IR, "unknown canonical native DXIL type kind");
        }
    }
    return b->status == DXIL_OK;
}
