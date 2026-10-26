// SPDX-License-Identifier: MIT
// WGSL to CSL mappings mirroring emit_csl_maps.zig.
#include "csl_maps.h"

#include <string.h>

const char *csl_unary_op_text(CslUnaryOp op) {
    switch (op) {
        case CSL_UNARY_NEG: return "-";
        case CSL_UNARY_NOT: return "!";
        case CSL_UNARY_BIT_NOT: return "~";
        default: return NULL;
    }
}

const char *csl_binary_op_text(CslBinaryOp op) {
    switch (op) {
        case CSL_BIN_ADD: return "+";
        case CSL_BIN_SUB: return "-";
        case CSL_BIN_MUL: return "*";
        case CSL_BIN_DIV: return "/";
        case CSL_BIN_REM: return "%";
        case CSL_BIN_BIT_AND: return "&";
        case CSL_BIN_BIT_OR: return "|";
        case CSL_BIN_BIT_XOR: return "^";
        case CSL_BIN_SHIFT_LEFT: return "<<";
        case CSL_BIN_SHIFT_RIGHT: return ">>";
        case CSL_BIN_EQUAL: return "==";
        case CSL_BIN_NOT_EQUAL: return "!=";
        case CSL_BIN_LESS: return "<";
        case CSL_BIN_LESS_EQUAL: return "<=";
        case CSL_BIN_GREATER: return ">";
        case CSL_BIN_GREATER_EQUAL: return ">=";
        case CSL_BIN_LOGICAL_AND: return "and";
        case CSL_BIN_LOGICAL_OR: return "or";
        default: return NULL;
    }
}

const char *csl_assign_op_text(CslAssignOp op) {
    switch (op) {
        case CSL_ASSIGN: return "=";
        case CSL_ASSIGN_ADD: return "+=";
        case CSL_ASSIGN_SUB: return "-=";
        case CSL_ASSIGN_MUL: return "*=";
        case CSL_ASSIGN_DIV: return "/=";
        case CSL_ASSIGN_REM: return "%=";
        case CSL_ASSIGN_BIT_AND: return "&=";
        case CSL_ASSIGN_BIT_OR: return "|=";
        case CSL_ASSIGN_BIT_XOR: return "^=";
        case CSL_ASSIGN_SHIFT_LEFT: return "<<=";
        case CSL_ASSIGN_SHIFT_RIGHT: return ">>=";
        default: return NULL;
    }
}

static const char *lookup(const char *name,
    const char *const *keys, const char *const *values, size_t count) {
    size_t i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(name, keys[i]) == 0) {
            return values[i];
        }
    }
    return NULL;
}

static int in_set(const char *name, const char *const *keys, size_t count) {
    size_t i;
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(name, keys[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *csl_math_builtin(const char *name) {
    static const char *const keys[] = { "abs", "sqrt", "exp", "exp2",
        "log", "log2", "sin", "cos", "tanh", "floor", "ceil", "round", "pow" };
    static const char *const values[] = { "math.abs", "math.sqrt",
        "math.exp", "math.exp2", "math.log", "math.log2", "math.sin",
        "math.cos", "math.tanh", "math.floor", "math.ceil", "math.round",
        "math.pow" };
    return lookup(name, keys, values, sizeof(keys) / sizeof(keys[0]));
}

int csl_needs_inline_expansion(const char *name) {
    static const char *const keys[] = { "clamp", "min", "max", "fma",
        "mix", "select", "sign", "step", "smoothstep", "fract", "trunc" };
    return in_set(name, keys, sizeof(keys) / sizeof(keys[0]));
}

int csl_is_unsupported_builtin(const char *name) {
    static const char *const keys[] = {
        "textureSample", "textureSampleLevel", "textureSampleGrad",
        "textureSampleCompare", "textureLoad", "textureStore",
        "textureDimensions", "textureNumLayers", "textureNumLevels",
        "textureNumSamples",
        "atomicLoad", "atomicStore", "atomicAdd", "atomicSub",
        "atomicMax", "atomicMin", "atomicAnd", "atomicOr", "atomicXor",
        "atomicExchange", "atomicCompareExchangeWeak",
        "dpdx", "dpdy", "fwidth",
        "subgroupAdd", "subgroupMul", "subgroupMin", "subgroupMax",
        "subgroupAnd", "subgroupOr", "subgroupXor", "subgroupBallot",
        "subgroupBroadcast", "subgroupBroadcastFirst", "subgroupShuffle"
    };
    return in_set(name, keys, sizeof(keys) / sizeof(keys[0]));
}
