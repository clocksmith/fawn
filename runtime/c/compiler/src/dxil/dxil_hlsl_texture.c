/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_texture.h"

#include <string.h>

static bool shape(const DxilIrModule *module, DxilIrTypeId id,
    DxilIrScalar scalar, uint8_t width) {
    DxilIrType type;
    if (dxil_ir_type_get(module, id, &type) != DXIL_OK) return false;
    if (width > 1u) {
        if (type.kind != DXIL_IR_TYPE_VECTOR || type.len != width ||
            dxil_ir_type_get(module, type.elem, &type) != DXIL_OK) return false;
    }
    return type.kind == DXIL_IR_TYPE_SCALAR && type.scalar == scalar;
}

static bool integer_shape(const DxilIrModule *module, DxilIrTypeId id, uint8_t width) {
    return shape(module, id, DXIL_IR_SCALAR_I32, width) ||
        shape(module, id, DXIL_IR_SCALAR_U32, width) ||
        shape(module, id, DXIL_IR_SCALAR_ABSTRACT_INT, width);
}

static bool float_shape(const DxilIrModule *module, DxilIrTypeId id, uint8_t width) {
    return shape(module, id, DXIL_IR_SCALAR_F32, width) ||
        shape(module, id, DXIL_IR_SCALAR_ABSTRACT_FLOAT, width);
}

static bool storage_float(DxilIrTextureFormat format) {
    return format == DXIL_IR_TEXFMT_RGBA8UNORM || format == DXIL_IR_TEXFMT_RGBA8SNORM ||
        format == DXIL_IR_TEXFMT_RGBA16FLOAT || format == DXIL_IR_TEXFMT_R32FLOAT ||
        format == DXIL_IR_TEXFMT_RG32FLOAT || format == DXIL_IR_TEXFMT_RGBA32FLOAT;
}

static uint8_t texture_width(DxilIrTypeKind kind) {
    if (kind == DXIL_IR_TYPE_TEXTURE_1D) return 1u;
    if (kind == DXIL_IR_TYPE_TEXTURE_2D || kind == DXIL_IR_TYPE_TEXTURE_DEPTH_2D ||
        kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D) return 2u;
    if (kind == DXIL_IR_TYPE_TEXTURE_CUBE || kind == DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE ||
        kind == DXIL_IR_TYPE_TEXTURE_3D) return 3u;
    return 0u;
}

static bool depth_texture(DxilIrTypeKind kind) {
    return kind == DXIL_IR_TYPE_TEXTURE_DEPTH_2D || kind == DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE;
}

static const DxilIrGlobal *bound_texture(const DxilHlslEmitter *emitter,
    const DxilIrExpr *expr) {
    if (expr->kind != DXIL_IR_EXPR_GLOBAL_REF || expr->index >= emitter->module->global_count)
        return NULL;
    const DxilIrGlobal *global = &emitter->module->globals[expr->index];
    return global->has_binding && global->ty == expr->ty ? global : NULL;
}

static DxilStatus emit_method(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    DxilIrRange args, uint32_t texture_at, const char *method) {
    DxilStatus status = dxil_hlsl_emit_expr(emitter, function,
        function->expr_args[args.start + texture_at]);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, method);
    DxilIrRange operands = {args.start + texture_at + 1u, args.len - texture_at - 1u};
    if (status == DXIL_OK) status = dxil_hlsl_emit_expr_list(emitter, function, operands);
    return status == DXIL_OK ? dxil_hlsl_write_text(emitter, ")") : status;
}

static DxilStatus helper_call(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    const char *prefix, const char *global_name, DxilIrRange operands) {
    DxilStatus status = dxil_hlsl_write_text(emitter, prefix);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global_name);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "(");
    if (status == DXIL_OK) status = dxil_hlsl_emit_expr_list(emitter, function, operands);
    return status == DXIL_OK ? dxil_hlsl_write_text(emitter, ")") : status;
}

static DxilStatus emit_load_store(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    DxilIrTypeId result, bool store, DxilIrRange args, const DxilIrExpr *const *values) {
    DxilIrType type;
    if (dxil_ir_type_get(emitter->module, values[0]->ty, &type) != DXIL_OK)
        return DXIL_INVALID_IR;
    bool storage = type.kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
    uint8_t width = type.kind == DXIL_IR_TYPE_TEXTURE_1D ? 1u : 2u;
    if (type.kind != DXIL_IR_TYPE_TEXTURE_1D && type.kind != DXIL_IR_TYPE_TEXTURE_2D &&
        !storage) return DXIL_UNSUPPORTED_BUILTIN;
    if (storage && (unsigned)type.access > DXIL_IR_ACCESS_READ_WRITE) return DXIL_INVALID_IR;
    if (storage && !storage_float(type.format)) return DXIL_UNSUPPORTED_BUILTIN;
    if (!integer_shape(emitter->module, values[1]->ty, width)) return DXIL_INVALID_IR;
    if (store) {
        if (!storage || type.access == DXIL_IR_ACCESS_READ || args.len != 3u ||
            !float_shape(emitter->module, values[2]->ty, 4u) ||
            !shape(emitter->module, result, DXIL_IR_SCALAR_VOID, 1u)) return DXIL_INVALID_IR;
    } else {
        if (!float_shape(emitter->module, result, 4u) ||
            (storage && (type.access == DXIL_IR_ACCESS_WRITE || args.len != 2u)) ||
            (!storage && (args.len != 3u ||
                !integer_shape(emitter->module, values[2]->ty, 1u)))) return DXIL_INVALID_IR;
    }
    const DxilIrGlobal *global = bound_texture(emitter, values[0]);
    if (global) {
        DxilIrRange operands = {args.start + 1u, args.len - 1u};
        return helper_call(emitter, function, store ? "doe_textureStore_" : "doe_textureLoad_",
            global->name, operands);
    }
    DxilStatus status = dxil_hlsl_emit_expr(emitter, function, function->expr_args[args.start]);
    bool indexed = storage && type.access != DXIL_IR_ACCESS_READ;
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        indexed ? "[int2(" : width == 1u ? ".Load(int2(" : ".Load(int3(");
    if (status == DXIL_OK) status = dxil_hlsl_emit_expr(emitter, function,
        function->expr_args[args.start + 1u]);
    if (!indexed) {
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ", ");
        if (status == DXIL_OK) status = storage ? dxil_hlsl_write_text(emitter, "0") :
            dxil_hlsl_emit_expr(emitter, function, function->expr_args[args.start + 2u]);
    }
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, indexed ? ")]" : "))");
    if (store) {
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, " = ");
        if (status == DXIL_OK) status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start + 2u]);
    }
    return status;
}

static bool mip_texture(DxilIrTypeKind kind) {
    return kind == DXIL_IR_TYPE_TEXTURE_1D || kind == DXIL_IR_TYPE_TEXTURE_2D ||
        kind == DXIL_IR_TYPE_TEXTURE_2D_ARRAY || kind == DXIL_IR_TYPE_TEXTURE_CUBE ||
        kind == DXIL_IR_TYPE_TEXTURE_DEPTH_2D || kind == DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE ||
        kind == DXIL_IR_TYPE_TEXTURE_3D;
}

static DxilStatus emit_metadata(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    DxilIrTypeId result, const char *name, DxilIrRange args, const DxilIrExpr *const *values) {
    const DxilIrGlobal *global = bound_texture(emitter, values[0]);
    DxilIrType type;
    if (!global || dxil_ir_type_get(emitter->module, global->ty, &type) != DXIL_OK)
        return DXIL_INVALID_IR;
    bool dimensions = strcmp(name, "textureDimensions") == 0;
    bool levels = strcmp(name, "textureNumLevels") == 0;
    bool storage = type.kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
    if ((!storage && !mip_texture(type.kind)) || (levels && storage) ||
        (!dimensions && !levels && type.kind != DXIL_IR_TYPE_TEXTURE_2D_ARRAY))
        return DXIL_UNSUPPORTED_BUILTIN;
    uint8_t width = 1u;
    if (dimensions && type.kind != DXIL_IR_TYPE_TEXTURE_1D)
        width = type.kind == DXIL_IR_TYPE_TEXTURE_3D ? 3u : 2u;
    if (!shape(emitter->module, result, DXIL_IR_SCALAR_U32, width) ||
        args.len != (dimensions && !storage ? 2u : 1u)) return DXIL_INVALID_IR;
    if (args.len == 2u && !integer_shape(emitter->module, values[1]->ty, 1u))
        return DXIL_INVALID_IR;
    DxilIrRange operands = {args.start + 1u, args.len - 1u};
    return helper_call(emitter, function, dimensions ? "doe_textureDimensions_" :
        levels ? "doe_textureNumLevels_" : "doe_textureNumLayers_", global->name, operands);
}

DxilStatus dxil_hlsl_emit_texture_builtin(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrTypeId result_type,
    const char *name, DxilIrRange args, bool *handled) {
    static const struct TextureOperation {
        const char *name;
        const char *method;
        uint32_t count;
    } operations[] = {
        {"textureLoad", NULL, 0u}, {"textureStore", NULL, 3u},
        {"textureSample", ".Sample(", 3u},
        {"textureSampleLevel", ".SampleLevel(", 4u},
        {"textureSampleCompare", ".SampleCmp(", 4u},
        {"textureSampleCompareLevel", ".SampleCmpLevelZero(", 4u},
        {"textureGather", ".GatherRed(", 4u},
        {"textureGatherCompare", ".GatherCmp(", 4u},
        {"textureSampleGrad", ".SampleGrad(", 5u},
        {"textureSampleOffset", ".Sample(", 4u},
        {"textureSampleLevelOffset", ".SampleLevel(", 5u},
        {"textureDimensions", NULL, 0u},
        {"textureNumLevels", NULL, 1u}, {"textureNumLayers", NULL, 1u}
    };
    *handled = false;
    const struct TextureOperation *operation = NULL;
    for (size_t i = 0u; i < sizeof(operations) / sizeof(operations[0]); ++i) {
        if (strcmp(name, operations[i].name) == 0) {
            operation = &operations[i];
            break;
        }
    }
    if (!operation) return DXIL_OK;
    *handled = true;
    if (args.len == 0u || args.len > 5u ||
        function->expr_arg_count > DXIL_IR_MAX_EXPR_ARGS ||
        function->expr_count > DXIL_IR_MAX_EXPRS ||
        args.start > function->expr_arg_count || args.len > function->expr_arg_count - args.start ||
        (operation->count != 0u && args.len != operation->count)) return DXIL_INVALID_IR;
    const DxilIrExpr *values[5] = {NULL};
    for (uint32_t i = 0u; i < args.len; ++i) {
        uint32_t id = function->expr_args[args.start + i];
        if (id >= function->expr_count) return DXIL_INVALID_IR;
        values[i] = &function->exprs[id];
    }
    if (strcmp(name, "textureLoad") == 0 || strcmp(name, "textureStore") == 0) {
        if (args.len < 2u || args.len > 3u) return DXIL_INVALID_IR;
        return emit_load_store(emitter, function, result_type,
            strcmp(name, "textureStore") == 0, args, values);
    }
    if (!operation->method) return emit_metadata(emitter, function, result_type, name, args, values);
    bool gather = strcmp(name, "textureGather") == 0;
    bool compare = strstr(name, "Compare") != NULL;
    bool gather_compare = strcmp(name, "textureGatherCompare") == 0;
    bool grad = strcmp(name, "textureSampleGrad") == 0;
    bool offset = strstr(name, "Offset") != NULL;
    uint32_t texture_at = gather ? 1u : 0u;
    DxilIrType texture;
    DxilIrType sampler;
    if (dxil_ir_type_get(emitter->module, values[texture_at]->ty, &texture) != DXIL_OK ||
        dxil_ir_type_get(emitter->module, values[texture_at + 1u]->ty, &sampler) != DXIL_OK)
        return DXIL_INVALID_IR;
    uint8_t width = texture_width(texture.kind);
    if (width == 0u || texture.kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D ||
        (compare && !depth_texture(texture.kind)) ||
        (offset && (texture.kind == DXIL_IR_TYPE_TEXTURE_CUBE ||
                    texture.kind == DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE)) ||
        ((gather || gather_compare) && width == 1u) ||
        ((gather || gather_compare) && texture.kind == DXIL_IR_TYPE_TEXTURE_3D))
        return DXIL_UNSUPPORTED_BUILTIN;
    if (sampler.kind != (compare ? DXIL_IR_TYPE_SAMPLER_COMPARISON : DXIL_IR_TYPE_SAMPLER) ||
        !float_shape(emitter->module, values[texture_at + 2u]->ty, width) ||
        !float_shape(emitter->module, result_type,
            !gather_compare && depth_texture(texture.kind) ? 1u : 4u)) return DXIL_INVALID_IR;
    const char *method = operation->method;
    if (gather) {
        static const char *methods[] = {".GatherRed(", ".GatherGreen(", ".GatherBlue(", ".GatherAlpha("};
        if (depth_texture(texture.kind) || values[0]->kind != DXIL_IR_EXPR_INT_LIT ||
            values[0]->uint_value > 3u || !integer_shape(emitter->module, values[0]->ty, 1u))
            return DXIL_INVALID_IR;
        method = methods[values[0]->uint_value];
    } else if (compare) {
        if (!float_shape(emitter->module, values[3]->ty, 1u)) return DXIL_INVALID_IR;
    } else if (grad) {
        if (!float_shape(emitter->module, values[3]->ty, width) ||
            !float_shape(emitter->module, values[4]->ty, width)) return DXIL_INVALID_IR;
    } else if (args.len >= 4u) {
        bool level = strcmp(name, "textureSampleOffset") != 0;
        if (level && !float_shape(emitter->module, values[3]->ty, 1u)) return DXIL_INVALID_IR;
        if (offset && !integer_shape(emitter->module, values[args.len - 1u]->ty, width))
            return DXIL_INVALID_IR;
    }
    return emit_method(emitter, function, args, texture_at, method);
}

static DxilStatus helper_signature(DxilHlslEmitter *emitter, const char *result,
    const char *operation, const char *name, const char *parameters) {
    DxilStatus status = dxil_hlsl_write_text(emitter, "\n");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, result);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, " doe_");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, operation);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "_");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, name);
    return status == DXIL_OK ? dxil_hlsl_write_text(emitter, parameters) : status;
}

static DxilStatus metadata_helpers(DxilHlslEmitter *emitter, const DxilIrGlobal *global,
    DxilIrTypeKind kind) {
    bool storage = kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
    bool one = kind == DXIL_IR_TYPE_TEXTURE_1D;
    bool three = kind == DXIL_IR_TYPE_TEXTURE_3D;
    bool array = kind == DXIL_IR_TYPE_TEXTURE_2D_ARRAY;
    const char *variables = one ? "    uint w = 0u; uint lvls = 0u;\n    " :
        three || array ? "    uint w = 0u; uint h = 0u; uint d = 0u; uint lvls = 0u;\n    " :
        storage ? "    uint w = 0u; uint h = 0u;\n    " :
        "    uint w = 0u; uint h = 0u; uint lvls = 0u;\n    ";
    const char *arguments = one ? "w, lvls);\n" :
        three || array ? "w, h, d, lvls);\n" : storage ? "w, h);\n" : "w, h, lvls);\n";
    DxilStatus status = helper_signature(emitter, one ? "uint" : three ? "uint3" : "uint2",
        "textureDimensions", global->name, storage ? "() {\n" : "(uint level) {\n");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, variables);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global->name);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        storage ? ".GetDimensions(" : ".GetDimensions(level, ");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, arguments);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        one ? "    return w;\n}\n" : three ? "    return uint3(w, h, d);\n}\n" :
        "    return uint2(w, h);\n}\n");
    if (storage || status != DXIL_OK) return status;
    status = helper_signature(emitter, "uint", "textureNumLevels", global->name, "() {\n");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, variables);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global->name);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ".GetDimensions(0u, ");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, arguments);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "    return lvls;\n}\n");
    if (!array || status != DXIL_OK) return status;
    status = helper_signature(emitter, "uint", "textureNumLayers", global->name, "() {\n");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, variables);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global->name);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        ".GetDimensions(0u, w, h, d, lvls);\n    return d;\n}\n");
    return status;
}

static DxilStatus load_store_helper(DxilHlslEmitter *emitter, const DxilIrGlobal *global,
    const DxilIrType *type, bool store) {
    bool storage = type->kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
    bool one = type->kind == DXIL_IR_TYPE_TEXTURE_1D;
    DxilStatus status = helper_signature(emitter, store ? "void" : "float4",
        store ? "textureStore" : "textureLoad", global->name,
        store ? "(int2 coord, float4 value) {\n" : storage ? "(int2 coord) {\n" :
        one ? "(int coord, int level) {\n" : "(int2 coord, int level) {\n");
    if (!storage) {
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
            "    if (level < 0 || uint(level) >= doe_textureNumLevels_");
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global->name);
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
            "()) return float4(0.0, 0.0, 0.0, 0.0);\n");
    }
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        one ? "    if (coord < 0 || uint(coord) >= doe_textureDimensions_" :
        "    if (any(coord < int2(0, 0)) || any(uint2(coord) >= doe_textureDimensions_");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global->name);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        storage ? "())" : "(uint(level)))");
    if (!one && status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ")");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        store ? " return;\n    " : " return float4(0.0, 0.0, 0.0, 0.0);\n    return ");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global->name);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, store ?
        "[uint2(coord)] = value;\n}\n" : storage && type->access != DXIL_IR_ACCESS_READ ?
        "[uint2(coord)];\n}\n" : storage ? ".Load(int3(coord, 0));\n}\n" :
        one ? ".Load(int2(coord, level));\n}\n" : ".Load(int3(coord, level));\n}\n");
    return status;
}

DxilStatus dxil_hlsl_emit_texture_helpers(DxilHlslEmitter *emitter) {
    if (emitter->module->global_count > DXIL_IR_MAX_GLOBALS) return DXIL_INVALID_IR;
    for (uint32_t i = 0u; i < emitter->module->global_count; ++i) {
        const DxilIrGlobal *global = &emitter->module->globals[i];
        if (!global->has_binding) continue;
        DxilIrType type;
        if (dxil_ir_type_get(emitter->module, global->ty, &type) != DXIL_OK)
            return DXIL_INVALID_IR;
        bool storage = type.kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
        if (!storage && !mip_texture(type.kind)) continue;
        DxilStatus status = metadata_helpers(emitter, global, type.kind);
        if (status != DXIL_OK) return status;
        if (storage && !storage_float(type.format)) continue;
        if ((storage && type.access != DXIL_IR_ACCESS_WRITE) ||
            type.kind == DXIL_IR_TYPE_TEXTURE_1D || type.kind == DXIL_IR_TYPE_TEXTURE_2D) {
            status = load_store_helper(emitter, global, &type, false);
            if (status != DXIL_OK) return status;
        }
        if (storage && type.access != DXIL_IR_ACCESS_READ) {
            status = load_store_helper(emitter, global, &type, true);
            if (status != DXIL_OK) return status;
        }
    }
    return DXIL_OK;
}
