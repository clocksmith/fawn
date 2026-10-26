/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_mapping.h"

#include <string.h>

uint32_t dxil_stage_shader_kind(DxilStage stage) {
    switch (stage) {
        case DXIL_STAGE_COMPUTE: return DXIL_SHADER_COMPUTE;
        case DXIL_STAGE_VERTEX: return DXIL_SHADER_VERTEX;
        case DXIL_STAGE_FRAGMENT: return DXIL_SHADER_PIXEL;
    }
    return DXIL_SHADER_COMPUTE;
}

const char *dxil_stage_prefix(DxilStage stage) {
    switch (stage) {
        case DXIL_STAGE_COMPUTE: return "cs";
        case DXIL_STAGE_VERTEX: return "vs";
        case DXIL_STAGE_FRAGMENT: return "ps";
    }
    return "cs";
}

bool dxil_compute_param_opcode(DxilParamBuiltin builtin, uint32_t *opcode,
    uint32_t *call_count) {
    switch (builtin) {
        case DXIL_BUILTIN_GLOBAL_INVOCATION_ID:
            *opcode = DXIL_OP_THREAD_ID;
            *call_count = 3;
            return true;
        case DXIL_BUILTIN_LOCAL_INVOCATION_ID:
            *opcode = DXIL_OP_THREAD_ID_IN_GROUP;
            *call_count = 3;
            return true;
        case DXIL_BUILTIN_WORKGROUP_ID:
            *opcode = DXIL_OP_GROUP_ID;
            *call_count = 3;
            return true;
        case DXIL_BUILTIN_LOCAL_INVOCATION_INDEX:
            *opcode = DXIL_OP_FLATTENED_THREAD_ID_IN_GROUP;
            *call_count = 1;
            return true;
        case DXIL_BUILTIN_VERTEX_INDEX:
        case DXIL_BUILTIN_INSTANCE_INDEX:
        case DXIL_BUILTIN_POSITION:
        case DXIL_BUILTIN_FRONT_FACING:
        case DXIL_BUILTIN_OTHER:
        default:
            *opcode = 0;
            *call_count = 0;
            return false;
    }
}

bool dxil_vertex_param_loads_input(DxilParamBuiltin builtin) {
    return builtin == DXIL_BUILTIN_VERTEX_INDEX
        || builtin == DXIL_BUILTIN_INSTANCE_INDEX;
}

bool dxil_fragment_param_loads_input(bool has_location,
    DxilParamBuiltin builtin) {
    if (has_location) {
        return true;
    }
    return builtin == DXIL_BUILTIN_POSITION
        || builtin == DXIL_BUILTIN_FRONT_FACING;
}

void dxil_builtin_call(const char *name, DxilBuiltinCall *out) {
    out->opcode = 0;
    out->arg_mode = DXIL_BUILTIN_ARG_NONE;
    out->mode_value = 0;
    out->recognized = true;
    if (strcmp(name, "workgroupBarrier") == 0) {
        out->opcode = DXIL_OP_BARRIER;
        out->arg_mode = DXIL_BUILTIN_ARG_MODE_CONSTANT;
        out->mode_value = DXIL_BARRIER_SYNC_THREAD_GROUP
            | DXIL_BARRIER_TGSM_FENCE;
        return;
    }
    if (strcmp(name, "storageBarrier") == 0
        || strcmp(name, "textureBarrier") == 0) {
        out->opcode = DXIL_OP_BARRIER;
        out->arg_mode = DXIL_BUILTIN_ARG_MODE_CONSTANT;
        out->mode_value = DXIL_BARRIER_SYNC_THREAD_GROUP
            | DXIL_BARRIER_UAV_FENCE_GLOBAL;
        return;
    }
    if (strcmp(name, "subgroupElect") == 0) {
        out->opcode = DXIL_OP_WAVE_IS_FIRST_LANE;
        out->arg_mode = DXIL_BUILTIN_ARG_NONE;
        return;
    }
    if (strcmp(name, "subgroupAll") == 0) {
        out->opcode = DXIL_OP_WAVE_ALL_TRUE;
        out->arg_mode = DXIL_BUILTIN_ARG_FIRST_IF_PRESENT;
        return;
    }
    if (strcmp(name, "subgroupAny") == 0) {
        out->opcode = DXIL_OP_WAVE_ANY_TRUE;
        out->arg_mode = DXIL_BUILTIN_ARG_FIRST_IF_PRESENT;
        return;
    }
    if (strcmp(name, "subgroupBallot") == 0) {
        out->opcode = DXIL_OP_WAVE_ACTIVE_BALLOT;
        out->arg_mode = DXIL_BUILTIN_ARG_FIRST_IF_PRESENT;
        return;
    }
    if (strcmp(name, "subgroupAdd") == 0) {
        out->opcode = DXIL_OP_WAVE_ACTIVE_OP;
        out->arg_mode = DXIL_BUILTIN_ARG_FIRST_PLUS_WAVE_SUM;
        return;
    }
    /* Unrecognized builtin: arguments are still evaluated by the caller
     * and the result is constant 0. Note subgroupAdd with zero arguments
     * also yields constant 0 in Zig; that arity rule lives with the
     * caller, since this table maps names, not arities. */
    out->recognized = false;
}

void dxil_unary_opcode(DxilUnaryOp op, bool is_float, uint32_t *opcode,
    DxilUnaryForm *form) {
    switch (op) {
        case DXIL_UNARY_NEG:
            *opcode = is_float ? DXIL_BINOP_FSUB : DXIL_BINOP_SUB;
            *form = DXIL_UNARY_ZERO_MINUS;
            return;
        case DXIL_UNARY_NOT:
        case DXIL_UNARY_BIT_NOT:
            *opcode = DXIL_BINOP_XOR;
            *form = DXIL_UNARY_XOR_ALL_ONES;
            return;
    }
    *opcode = DXIL_BINOP_XOR;
    *form = DXIL_UNARY_XOR_ALL_ONES;
}

void dxil_binary_opcode(DxilBinaryOp op, bool is_float, bool is_signed,
    bool *is_comparison, uint32_t *opcode) {
    *is_comparison = false;
    switch (op) {
        case DXIL_BINARY_ADD:
            *opcode = is_float ? DXIL_BINOP_FADD : DXIL_BINOP_ADD;
            return;
        case DXIL_BINARY_SUB:
            *opcode = is_float ? DXIL_BINOP_FSUB : DXIL_BINOP_SUB;
            return;
        case DXIL_BINARY_MUL:
            *opcode = is_float ? DXIL_BINOP_FMUL : DXIL_BINOP_MUL;
            return;
        case DXIL_BINARY_DIV:
            *opcode = is_float ? DXIL_BINOP_FDIV
                : is_signed ? DXIL_BINOP_SDIV : DXIL_BINOP_UDIV;
            return;
        case DXIL_BINARY_REM:
            *opcode = is_float ? DXIL_BINOP_FREM
                : is_signed ? DXIL_BINOP_SREM : DXIL_BINOP_UREM;
            return;
        case DXIL_BINARY_BIT_AND:
            *opcode = DXIL_BINOP_AND;
            return;
        case DXIL_BINARY_BIT_OR:
            *opcode = DXIL_BINOP_OR;
            return;
        case DXIL_BINARY_BIT_XOR:
            *opcode = DXIL_BINOP_XOR;
            return;
        case DXIL_BINARY_SHIFT_LEFT:
            *opcode = DXIL_BINOP_SHL;
            return;
        case DXIL_BINARY_SHIFT_RIGHT:
            *opcode = is_signed ? DXIL_BINOP_ASHR : DXIL_BINOP_LSHR;
            return;
        case DXIL_BINARY_LOGICAL_AND:
            *opcode = DXIL_BINOP_AND;
            return;
        case DXIL_BINARY_LOGICAL_OR:
            *opcode = DXIL_BINOP_OR;
            return;
        case DXIL_BINARY_EQUAL:
            *is_comparison = true;
            *opcode = is_float ? DXIL_CMP_FCMP_OEQ : DXIL_CMP_ICMP_EQ;
            return;
        case DXIL_BINARY_NOT_EQUAL:
            *is_comparison = true;
            *opcode = is_float ? DXIL_CMP_FCMP_ONE : DXIL_CMP_ICMP_NE;
            return;
        case DXIL_BINARY_LESS:
            *is_comparison = true;
            *opcode = is_float ? DXIL_CMP_FCMP_OLT
                : is_signed ? DXIL_CMP_ICMP_SLT : DXIL_CMP_ICMP_ULT;
            return;
        case DXIL_BINARY_LESS_EQUAL:
            *is_comparison = true;
            *opcode = is_float ? DXIL_CMP_FCMP_OLE
                : is_signed ? DXIL_CMP_ICMP_SLE : DXIL_CMP_ICMP_ULE;
            return;
        case DXIL_BINARY_GREATER:
            *is_comparison = true;
            *opcode = is_float ? DXIL_CMP_FCMP_OGT
                : is_signed ? DXIL_CMP_ICMP_SGT : DXIL_CMP_ICMP_UGT;
            return;
        case DXIL_BINARY_GREATER_EQUAL:
            *is_comparison = true;
            *opcode = is_float ? DXIL_CMP_FCMP_OGE
                : is_signed ? DXIL_CMP_ICMP_SGE : DXIL_CMP_ICMP_UGE;
            return;
    }
    *opcode = DXIL_BINOP_ADD;
}

static bool storage_kind(DxilResourceTypeKind type_kind,
    uint32_t *out_kind) {
    if (type_kind == DXIL_RESTYPE_ARRAY
        || type_kind == DXIL_RESTYPE_STRUCT) {
        *out_kind = DXIL_RESOURCE_STRUCTURED_BUFFER;
    } else {
        *out_kind = DXIL_RESOURCE_RAW_BUFFER;
    }
    return true;
}

bool dxil_classify_resource(DxilAddrSpace addr_space, DxilAccess access,
    DxilResourceTypeKind type_kind, uint32_t *out_class,
    uint32_t *out_kind) {
    switch (addr_space) {
        case DXIL_ADDR_UNIFORM:
            *out_class = DXIL_RESOURCE_CBV;
            *out_kind = DXIL_RESOURCE_CBUFFER;
            return true;
        case DXIL_ADDR_STORAGE: {
            const bool read = access == DXIL_ACCESS_READ;
            *out_class = read ? DXIL_RESOURCE_SRV : DXIL_RESOURCE_UAV;
            return storage_kind(type_kind, out_kind);
        }
        case DXIL_ADDR_HANDLE:
            switch (type_kind) {
                case DXIL_RESTYPE_SAMPLER:
                case DXIL_RESTYPE_SAMPLER_COMPARISON:
                    *out_class = DXIL_RESOURCE_SAMPLER;
                    *out_kind = DXIL_RESOURCE_KIND_SAMPLER;
                    return true;
                case DXIL_RESTYPE_TEXTURE_1D:
                    *out_class = DXIL_RESOURCE_SRV;
                    *out_kind = DXIL_RESOURCE_TEXTURE_1D;
                    return true;
                case DXIL_RESTYPE_TEXTURE_2D:
                case DXIL_RESTYPE_TEXTURE_2D_ARRAY:
                case DXIL_RESTYPE_TEXTURE_DEPTH_2D:
                    *out_class = DXIL_RESOURCE_SRV;
                    *out_kind = DXIL_RESOURCE_TEXTURE_2D;
                    return true;
                case DXIL_RESTYPE_TEXTURE_3D:
                    *out_class = DXIL_RESOURCE_SRV;
                    *out_kind = DXIL_RESOURCE_TEXTURE_3D;
                    return true;
                case DXIL_RESTYPE_TEXTURE_CUBE:
                case DXIL_RESTYPE_TEXTURE_DEPTH_CUBE:
                    *out_class = DXIL_RESOURCE_SRV;
                    *out_kind = DXIL_RESOURCE_TEXTURE_CUBE;
                    return true;
                case DXIL_RESTYPE_TEXTURE_MULTISAMPLED_2D:
                    *out_class = DXIL_RESOURCE_SRV;
                    *out_kind = DXIL_RESOURCE_TEXTURE_2D_MS;
                    return true;
                case DXIL_RESTYPE_STORAGE_TEXTURE_2D:
                    *out_class = access == DXIL_ACCESS_READ
                        ? DXIL_RESOURCE_SRV : DXIL_RESOURCE_UAV;
                    *out_kind = DXIL_RESOURCE_TEXTURE_2D;
                    return true;
                case DXIL_RESTYPE_ARRAY:
                case DXIL_RESTYPE_STRUCT:
                case DXIL_RESTYPE_OTHER:
                default:
                    return false;
            }
        case DXIL_ADDR_OTHER:
        default:
            return false;
    }
}
