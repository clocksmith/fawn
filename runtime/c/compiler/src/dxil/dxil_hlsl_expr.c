/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *scalar_name(DxilIrScalar scalar, bool *ok) {
    *ok = true;
    switch (scalar) {
        case DXIL_IR_SCALAR_VOID:
            return "void";
        case DXIL_IR_SCALAR_BOOL:
            return "bool";
        case DXIL_IR_SCALAR_I32:
        case DXIL_IR_SCALAR_ABSTRACT_INT:
            return "int";
        case DXIL_IR_SCALAR_U32:
            return "uint";
        case DXIL_IR_SCALAR_F32:
        case DXIL_IR_SCALAR_ABSTRACT_FLOAT:
            return "float";
        case DXIL_IR_SCALAR_F16:
            return "half";
    }
    *ok = false;
    return "";
}

DxilStatus dxil_hlsl_emit_type_only(DxilHlslEmitter *emitter,
    DxilIrTypeId ty) {
    DxilIrType type;
    DxilStatus status = DXIL_OK;
    if (dxil_ir_type_get(emitter->module, ty, &type) != DXIL_OK) {
        return DXIL_INVALID_IR;
    }
    switch (type.kind) {
        case DXIL_IR_TYPE_SCALAR: {
            bool ok = false;
            const char *name = scalar_name(type.scalar, &ok);
            if (!ok) {
                return DXIL_INVALID_IR;
            }
            return dxil_hlsl_write_text(emitter, name);
        }
        case DXIL_IR_TYPE_VECTOR: {
            DxilIrType elem;
            bool ok = false;
            const char *name = NULL;
            if (dxil_ir_type_get(emitter->module, type.elem, &elem)
                    != DXIL_OK
                || elem.kind != DXIL_IR_TYPE_SCALAR) {
                return DXIL_INVALID_IR;
            }
            name = scalar_name(elem.scalar, &ok);
            if (!ok) {
                return DXIL_INVALID_IR;
            }
            status = dxil_hlsl_write_text(emitter, name);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_hlsl_write_u32(emitter, type.len);
        }
        case DXIL_IR_TYPE_MATRIX: {
            DxilIrType elem;
            bool ok = false;
            const char *name = NULL;
            if (dxil_ir_type_get(emitter->module, type.elem, &elem)
                    != DXIL_OK
                || elem.kind != DXIL_IR_TYPE_SCALAR) {
                return DXIL_INVALID_IR;
            }
            switch (elem.scalar) {
                case DXIL_IR_SCALAR_F32:
                case DXIL_IR_SCALAR_ABSTRACT_FLOAT:
                    name = "float";
                    break;
                case DXIL_IR_SCALAR_F16:
                    name = "half";
                    break;
                case DXIL_IR_SCALAR_VOID:
                case DXIL_IR_SCALAR_BOOL:
                case DXIL_IR_SCALAR_ABSTRACT_INT:
                case DXIL_IR_SCALAR_I32:
                case DXIL_IR_SCALAR_U32:
                default:
                    return DXIL_INVALID_IR;
            }
            (void)ok;
            status = dxil_hlsl_write_text(emitter, name);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, type.columns);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, "x");
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_hlsl_write_u32(emitter, type.rows);
        }
        case DXIL_IR_TYPE_ARRAY:
            return DXIL_INVALID_IR;
        case DXIL_IR_TYPE_ATOMIC: {
            DxilIrType inner;
            if (dxil_ir_type_get(emitter->module, type.elem, &inner)
                    != DXIL_OK
                || inner.kind != DXIL_IR_TYPE_SCALAR) {
                return DXIL_INVALID_IR;
            }
            switch (inner.scalar) {
                case DXIL_IR_SCALAR_U32:
                    return dxil_hlsl_write_text(emitter, "uint");
                case DXIL_IR_SCALAR_I32:
                case DXIL_IR_SCALAR_ABSTRACT_INT:
                    return dxil_hlsl_write_text(emitter, "int");
                case DXIL_IR_SCALAR_VOID:
                case DXIL_IR_SCALAR_BOOL:
                case DXIL_IR_SCALAR_ABSTRACT_FLOAT:
                case DXIL_IR_SCALAR_F32:
                case DXIL_IR_SCALAR_F16:
                default:
                    return DXIL_INVALID_IR;
            }
        }
        case DXIL_IR_TYPE_STRUCT: {
            if (type.struct_id >= emitter->module->struct_count) {
                return DXIL_INVALID_IR;
            }
            return dxil_hlsl_write_text(emitter,
                emitter->module->structs[type.struct_id].name);
        }
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
            return dxil_hlsl_write_text(emitter, "SamplerState");
        case DXIL_IR_TYPE_TEXTURE_1D:
            return dxil_hlsl_write_text(emitter, "Texture1D<float4>");
        case DXIL_IR_TYPE_TEXTURE_2D:
            return dxil_hlsl_write_text(emitter, "Texture2D<float4>");
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
            return dxil_hlsl_write_text(emitter, "Texture2DArray<float4>");
        case DXIL_IR_TYPE_TEXTURE_CUBE:
            return dxil_hlsl_write_text(emitter, "TextureCube<float4>");
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
            return dxil_hlsl_write_text(emitter, "Texture2DMS<float4>");
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
            return dxil_hlsl_write_text(emitter, "Texture2D<float>");
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
            return dxil_hlsl_write_text(emitter, "TextureCube<float>");
        case DXIL_IR_TYPE_TEXTURE_3D:
            return dxil_hlsl_write_text(emitter, "Texture3D<float4>");
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
            if (type.access == DXIL_IR_ACCESS_READ) {
                return dxil_hlsl_write_text(emitter, "Texture2D<float4>");
            }
            return dxil_hlsl_write_text(emitter, "RWTexture2D<float4>");
        case DXIL_IR_TYPE_REF:
            return dxil_hlsl_emit_type_only(emitter, type.elem);
    }
    return DXIL_INVALID_IR;
}

DxilStatus dxil_hlsl_emit_typed_name(DxilHlslEmitter *emitter,
    DxilIrTypeId ty, const char *name) {
    DxilIrType type;
    DxilStatus status = DXIL_OK;
    if (dxil_ir_type_get(emitter->module, ty, &type) != DXIL_OK) {
        return DXIL_INVALID_IR;
    }
    if (type.kind == DXIL_IR_TYPE_ARRAY) {
        if (!type.has_len) {
            return DXIL_INVALID_IR;
        }
        status = dxil_hlsl_emit_type_only(emitter, type.elem);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, " ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, name);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, "[");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_u32(emitter, type.array_len);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, "]");
    }
    status = dxil_hlsl_emit_type_only(emitter, ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_hlsl_write_text(emitter, " ");
    if (status != DXIL_OK) {
        return status;
    }
    return dxil_hlsl_write_text(emitter, name);
}

DxilStatus dxil_hlsl_emit_constant(DxilHlslEmitter *emitter,
    const DxilIrGlobal *global) {
    switch (global->init_kind) {
        case DXIL_IR_CONST_BOOL:
            return dxil_hlsl_write_text(emitter,
                global->init_bool ? "true" : "false");
        case DXIL_IR_CONST_INT: {
            DxilIrType type;
            if (dxil_ir_type_get(emitter->module, global->ty, &type)
                    == DXIL_OK
                && type.kind == DXIL_IR_TYPE_SCALAR
                && type.scalar == DXIL_IR_SCALAR_I32) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d",
                    (int)(int32_t)(uint32_t)global->init_int);
                return dxil_hlsl_write_text(emitter, buf);
            }
            {
                DxilStatus status = dxil_hlsl_write_u64(emitter,
                    global->init_int);
                if (status != DXIL_OK) {
                    return status;
                }
            }
            if (dxil_ir_type_get(emitter->module, global->ty, &type)
                    == DXIL_OK
                && type.kind == DXIL_IR_TYPE_SCALAR
                && type.scalar == DXIL_IR_SCALAR_U32) {
                return dxil_hlsl_write_text(emitter, "u");
            }
            return DXIL_OK;
        }
        case DXIL_IR_CONST_FLOAT:
            return dxil_hlsl_write_float(emitter, global->init_float);
    }
    return DXIL_INVALID_IR;
}

bool dxil_hlsl_builtin_is_intrinsic(DxilIrBuiltin builtin) {
    return builtin == DXIL_IR_BUILTIN_SUBGROUP_SIZE
        || builtin == DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID;
}

const char *dxil_hlsl_builtin_intrinsic(DxilIrBuiltin builtin) {
    switch (builtin) {
        case DXIL_IR_BUILTIN_SUBGROUP_SIZE:
            return "WaveGetLaneCount()";
        case DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID:
            return "WaveGetLaneIndex()";
        case DXIL_IR_BUILTIN_NONE:
        case DXIL_IR_BUILTIN_POSITION:
        case DXIL_IR_BUILTIN_FRAG_DEPTH:
        case DXIL_IR_BUILTIN_FRONT_FACING:
        case DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID:
        case DXIL_IR_BUILTIN_LOCAL_INVOCATION_ID:
        case DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX:
        case DXIL_IR_BUILTIN_WORKGROUP_ID:
        case DXIL_IR_BUILTIN_NUM_WORKGROUPS:
        case DXIL_IR_BUILTIN_SAMPLE_INDEX:
        case DXIL_IR_BUILTIN_SAMPLE_MASK:
        case DXIL_IR_BUILTIN_VERTEX_INDEX:
        case DXIL_IR_BUILTIN_INSTANCE_INDEX:
        case DXIL_IR_BUILTIN_CLIP_DISTANCES:
        case DXIL_IR_BUILTIN_PRIMITIVE_INDEX:
        default:
            return NULL;
    }
}

static const char *unary_text(DxilIrUnaryOp op) {
    switch (op) {
        case DXIL_IR_UNARY_NEG:
            return "-";
        case DXIL_IR_UNARY_NOT:
            return "!";
        case DXIL_IR_UNARY_BIT_NOT:
            return "~";
    }
    return "";
}

static const char *binary_text(DxilIrBinaryOp op) {
    switch (op) {
        case DXIL_IR_BINARY_ADD:
            return "+";
        case DXIL_IR_BINARY_SUB:
            return "-";
        case DXIL_IR_BINARY_MUL:
            return "*";
        case DXIL_IR_BINARY_DIV:
            return "/";
        case DXIL_IR_BINARY_REM:
            return "%";
        case DXIL_IR_BINARY_BIT_AND:
            return "&";
        case DXIL_IR_BINARY_BIT_OR:
            return "|";
        case DXIL_IR_BINARY_BIT_XOR:
            return "^";
        case DXIL_IR_BINARY_SHIFT_LEFT:
            return "<<";
        case DXIL_IR_BINARY_SHIFT_RIGHT:
            return ">>";
        case DXIL_IR_BINARY_EQUAL:
            return "==";
        case DXIL_IR_BINARY_NOT_EQUAL:
            return "!=";
        case DXIL_IR_BINARY_LESS:
            return "<";
        case DXIL_IR_BINARY_LESS_EQUAL:
            return "<=";
        case DXIL_IR_BINARY_GREATER:
            return ">";
        case DXIL_IR_BINARY_GREATER_EQUAL:
            return ">=";
        case DXIL_IR_BINARY_LOGICAL_AND:
            return "&&";
        case DXIL_IR_BINARY_LOGICAL_OR:
            return "||";
    }
    return "";
}

DxilStatus dxil_hlsl_emit_expr_list(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrRange args) {
    uint32_t i = 0;
    DxilStatus status = DXIL_OK;
    for (i = 0; i < args.len; i++) {
        if (i > 0) {
            status = dxil_hlsl_write_text(emitter, ", ");
            if (status != DXIL_OK) {
                return status;
            }
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start + i]);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

static const char *renamed_builtin(const char *name) {
    static const struct DxilHlslRename {
        const char *wgsl;
        const char *hlsl;
    } table[] = {
        { "fract", "frac" },
        { "inverseSqrt", "rsqrt" },
        { "mix", "lerp" },
        { "countOneBits", "countbits" },
        { "reverseBits", "reversebits" },
        { "firstLeadingBit", "firstbithigh" },
        { "firstTrailingBit", "firstbitlow" },
        { "subgroupAdd", "WaveActiveSum" },
        { "subgroupExclusiveAdd", "WavePrefixSum" },
        { "subgroupMin", "WaveActiveMin" },
        { "subgroupMax", "WaveActiveMax" },
        { "subgroupBroadcast", "WaveReadLaneAt" },
        { "subgroupShuffle", "WaveReadLaneAt" },
        { "subgroupAnd", "WaveActiveBitAnd" },
        { "subgroupOr", "WaveActiveBitOr" },
        { "subgroupXor", "WaveActiveBitXor" },
        { "subgroupAll", "WaveActiveAllTrue" },
        { "subgroupAny", "WaveActiveAnyTrue" },
    };
    size_t i = 0;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].wgsl) == 0) {
            return table[i].hlsl;
        }
    }
    return NULL;
}

static bool passthrough_builtin(const char *name) {
    static const char *list[] = {
        "abs", "acos", "asin", "atan", "atan2", "ceil", "clamp",
        "cos", "cosh", "cross", "determinant", "distance", "dot",
        "exp", "exp2", "floor", "fma", "ldexp", "length", "log",
        "log2", "max", "min", "normalize", "pow", "reflect",
        "refract", "round", "saturate", "sign", "sin", "sinh",
        "smoothstep", "sqrt", "step", "tan", "tanh", "transpose",
        "trunc",
    };
    size_t i = 0;
    for (i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        if (strcmp(name, list[i]) == 0) {
            return true;
        }
    }
    return false;
}

static DxilStatus emit_builtin_call(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrTypeId result_ty,
    const char *name, DxilIrRange args) {
    DxilStatus status = DXIL_OK;
    const char *renamed = NULL;
    if (strcmp(name, "workgroupBarrier") == 0) {
        return dxil_hlsl_write_text(emitter, "GroupMemoryBarrierWithGroupSync()");
    }
    if (strcmp(name, "storageBarrier") == 0) {
        return dxil_hlsl_write_text(emitter, "AllMemoryBarrierWithGroupSync()");
    }
    if (strcmp(name, "textureBarrier") == 0) {
        return dxil_hlsl_write_text(emitter, "DeviceMemoryBarrierWithGroupSync()");
    }
    if (strcmp(name, "bitcast") == 0) {
        DxilIrType result;
        const char *fn = "asuint";
        if (args.len != 1) {
            return DXIL_INVALID_IR;
        }
        if (dxil_ir_type_get(emitter->module, result_ty, &result)
            == DXIL_OK
            && result.kind == DXIL_IR_TYPE_SCALAR) {
            switch (result.scalar) {
                case DXIL_IR_SCALAR_F32:
                case DXIL_IR_SCALAR_ABSTRACT_FLOAT:
                case DXIL_IR_SCALAR_F16:
                    fn = "asfloat";
                    break;
                case DXIL_IR_SCALAR_I32:
                case DXIL_IR_SCALAR_ABSTRACT_INT:
                    fn = "asint";
                    break;
                case DXIL_IR_SCALAR_VOID:
                case DXIL_IR_SCALAR_BOOL:
                case DXIL_IR_SCALAR_U32:
                default:
                    fn = "asuint";
                    break;
            }
        } else if (dxil_ir_type_get(emitter->module, result_ty,
                &result) == DXIL_OK
            && result.kind == DXIL_IR_TYPE_VECTOR) {
            DxilIrType elem;
            if (dxil_ir_type_get(emitter->module, result.elem,
                    &elem) == DXIL_OK
                && elem.kind == DXIL_IR_TYPE_SCALAR) {
                switch (elem.scalar) {
                    case DXIL_IR_SCALAR_F32:
                    case DXIL_IR_SCALAR_ABSTRACT_FLOAT:
                    case DXIL_IR_SCALAR_F16:
                        fn = "asfloat";
                        break;
                    case DXIL_IR_SCALAR_I32:
                    case DXIL_IR_SCALAR_ABSTRACT_INT:
                        fn = "asint";
                        break;
                    case DXIL_IR_SCALAR_VOID:
                    case DXIL_IR_SCALAR_BOOL:
                    case DXIL_IR_SCALAR_U32:
                    default:
                        fn = "asuint";
                        break;
                }
            }
        }
        status = dxil_hlsl_write_text(emitter, fn);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, "(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")");
    }
    if (strcmp(name, "select") == 0) {
        if (args.len != 3) {
            return DXIL_INVALID_IR;
        }
        status = dxil_hlsl_write_text(emitter, "((");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start + 2]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, ") ? (");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start + 1]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, ") : (");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, "))");
    }
    if (strcmp(name, "subgroupShuffleXor") == 0) {
        if (args.len != 2) {
            return DXIL_INVALID_IR;
        }
        status = dxil_hlsl_write_text(emitter, "WaveReadLaneAt(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, ", WaveGetLaneIndex() ^ ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start + 1]);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")");
    }
    if (strcmp(name, "subgroupBallot") == 0) {
        if (args.len != 1) {
            return DXIL_INVALID_IR;
        }
        status = dxil_hlsl_write_text(emitter, "WaveActiveBallot(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")");
    }
    if (strcmp(name, "subgroupElect") == 0) {
        return dxil_hlsl_write_text(emitter, "WaveIsFirstLane()");
    }
    if (strcmp(name, "countLeadingZeros") == 0) {
        if (args.len != 1) {
            return DXIL_INVALID_IR;
        }
        status = dxil_hlsl_write_text(emitter, "(((");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter,
            " == 0u) ? 32u : (31u - firstbithigh(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")))");
    }
    if (strcmp(name, "countTrailingZeros") == 0) {
        if (args.len != 1) {
            return DXIL_INVALID_IR;
        }
        status = dxil_hlsl_write_text(emitter, "(((");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter,
            " == 0u) ? 32u : firstbitlow(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr(emitter, function,
            function->expr_args[args.start]);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")))");
    }
    renamed = renamed_builtin(name);
    if (renamed != NULL) {
        status = dxil_hlsl_write_text(emitter, renamed);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, "(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr_list(emitter, function, args);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")");
    }
    if (passthrough_builtin(name)) {
        status = dxil_hlsl_write_text(emitter, name);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, "(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_expr_list(emitter, function, args);
        if (status != DXIL_OK) {
            return status;
        }
        return dxil_hlsl_write_text(emitter, ")");
    }
    return DXIL_UNSUPPORTED_BUILTIN;
}

static DxilStatus emit_local_name(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, uint32_t local_idx) {
    const char *name = NULL;
    char buf[32];
    if (local_idx >= function->local_count) {
        return DXIL_INVALID_IR;
    }
    name = function->locals[local_idx].name;
    if (strcmp(name, "signed") == 0 || strcmp(name, "unsigned") == 0) {
        snprintf(buf, sizeof(buf), "__doe_local_%u", local_idx);
        return dxil_hlsl_write_text(emitter, buf);
    }
    return dxil_hlsl_write_text(emitter, name);
}

DxilStatus dxil_hlsl_emit_expr(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrExprId expr_id) {
    const DxilIrExpr *expr = NULL;
    DxilStatus status = DXIL_OK;
    if (emitter->depth > 512) {
        return DXIL_INVALID_IR;
    }
    if (expr_id >= function->expr_count) {
        return DXIL_INVALID_IR;
    }
    emitter->depth += 1;
    expr = &function->exprs[expr_id];
    switch (expr->kind) {
        case DXIL_IR_EXPR_BOOL_LIT:
            status = dxil_hlsl_write_text(emitter,
                expr->bool_value ? "true" : "false");
            break;
        case DXIL_IR_EXPR_INT_LIT:
            status = dxil_hlsl_write_u64(emitter, expr->uint_value);
            break;
        case DXIL_IR_EXPR_FLOAT_LIT:
            status = dxil_hlsl_write_float(emitter, expr->float_value);
            break;
        case DXIL_IR_EXPR_PARAM_REF: {
            const DxilIrParam *param = NULL;
            if (expr->index >= function->param_count) {
                status = DXIL_INVALID_IR;
                break;
            }
            param = &function->params[expr->index];
            if (param->has_io
                && dxil_hlsl_builtin_is_intrinsic(param->io.builtin)) {
                status = dxil_hlsl_write_text(emitter,
                    dxil_hlsl_builtin_intrinsic(param->io.builtin));
            } else {
                status = dxil_hlsl_write_text(emitter, param->name);
            }
            break;
        }
        case DXIL_IR_EXPR_LOCAL_REF: {
            DxilIrType local_type;
            if (expr->index >= function->local_count) {
                status = DXIL_INVALID_IR;
                break;
            }
            if (dxil_ir_type_get(emitter->module,
                    function->locals[expr->index].ty, &local_type)
                    == DXIL_OK
                && local_type.kind == DXIL_IR_TYPE_REF) {
                DxilIrExprId initializer = 0;
                if (!dxil_ir_resolve_const_local_initializer(function,
                        expr->index, &initializer)) {
                    status = DXIL_INVALID_IR;
                    break;
                }
                status = dxil_hlsl_emit_expr(emitter, function, initializer);
            } else {
                status = emit_local_name(emitter, function,
                    expr->index);
            }
            break;
        }
        case DXIL_IR_EXPR_GLOBAL_REF:
            if (expr->index >= emitter->module->global_count) {
                status = DXIL_INVALID_IR;
            } else {
                status = dxil_hlsl_write_text(emitter,
                    emitter->module->globals[expr->index].name);
            }
            break;
        case DXIL_IR_EXPR_LOAD:
        case DXIL_IR_EXPR_ADDRESS_OF:
        case DXIL_IR_EXPR_DEREF:
            status = dxil_hlsl_emit_expr(emitter, function, expr->inner);
            break;
        case DXIL_IR_EXPR_UNARY:
            status = dxil_hlsl_write_text(emitter, "(");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, unary_text(expr->unary_op));
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, expr->operand);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ")");
            break;
        case DXIL_IR_EXPR_BINARY:
            status = dxil_hlsl_write_text(emitter, "(");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, expr->lhs);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, " ");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, binary_text(expr->binary_op));
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, " ");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, expr->rhs);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ")");
            break;
        case DXIL_IR_EXPR_CALL:
            if (expr->call_kind == DXIL_IR_CALL_BUILTIN) {
                status = emit_builtin_call(emitter, function,
                    expr->ty, expr->name, expr->args);
            } else {
                status = dxil_hlsl_write_text(emitter, expr->name);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "(");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_emit_expr_list(emitter, function,
                    expr->args);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, ")");
            }
            break;
        case DXIL_IR_EXPR_CONSTRUCT: {
            DxilIrType construct_type;
            bool splat = false;
            if (dxil_ir_type_get(emitter->module, expr->construct_ty,
                    &construct_type) == DXIL_OK
                && construct_type.kind == DXIL_IR_TYPE_VECTOR
                && expr->args.len == 1) {
                const DxilIrExpr *arg = NULL;
                DxilIrExprId arg_id =
                    function->expr_args[expr->args.start];
                if (arg_id < function->expr_count) {
                    arg = &function->exprs[arg_id];
                    {
                        DxilIrType arg_type;
                        if (dxil_ir_type_get(emitter->module,
                                arg->ty, &arg_type) == DXIL_OK
                            && arg_type.kind
                                == DXIL_IR_TYPE_SCALAR) {
                            splat = true;
                        }
                    }
                }
            }
            if (splat) {
                status = dxil_hlsl_write_text(emitter, "((");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_emit_type_only(emitter, expr->construct_ty);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, ")(");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_emit_expr(emitter, function,
                    function->expr_args[expr->args.start]);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "))");
                break;
            }
            status = dxil_hlsl_emit_type_only(emitter, expr->construct_ty);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "(");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr_list(emitter, function, expr->args);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ")");
            break;
        }
        case DXIL_IR_EXPR_MEMBER:
            status = dxil_hlsl_emit_expr(emitter, function, expr->base);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ".");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, expr->field_name);
            break;
        case DXIL_IR_EXPR_INDEX:
            status = dxil_hlsl_emit_expr(emitter, function, expr->base);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "[");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, expr->index_expr);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "]");
            break;
    }
    emitter->depth -= 1;
    return status;
}

