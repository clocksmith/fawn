/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"
#include "../../targets/hlsl/float_pack.h"
#include "dxil_hlsl_texture.h"
#include "dxil_hlsl_numeric.h"
#include <string.h>

#define TRY(call) do { const DxilStatus result_ = (call); if (result_ != DXIL_OK) return result_; } while (0)

typedef struct AtomicOperation { const char *name, *intrinsic; uint32_t count; } AtomicOperation;
static const AtomicOperation operations[] = {
    {"atomicLoad", "InterlockedOr", 1u}, {"atomicStore", "InterlockedExchange", 2u},
    {"atomicAdd", "InterlockedAdd", 2u}, {"atomicSub", "InterlockedAdd", 2u},
    {"atomicMin", "InterlockedMin", 2u}, {"atomicMax", "InterlockedMax", 2u},
    {"atomicAnd", "InterlockedAnd", 2u}, {"atomicOr", "InterlockedOr", 2u},
    {"atomicXor", "InterlockedXor", 2u}, {"atomicExchange", "InterlockedExchange", 2u},
    {"atomicCompareExchangeWeak", "InterlockedCompareExchange", 3u}
};
static const AtomicOperation *atomic_operation(const char *name) {
    for (size_t i = 0u; i < sizeof(operations)/sizeof(operations[0]); ++i)
        if (strcmp(operations[i].name, name) == 0) return &operations[i];
    return NULL;
}
typedef struct AtomicPath { uint32_t global, nodes[DXIL_IR_MAX_EXPRS], count; } AtomicPath;
static bool atomic_path(const DxilIrFunction *f, uint32_t id, AtomicPath *path) {
    *path = (AtomicPath){0};
    for (uint32_t n = 0u; n < f->expr_count && id < f->expr_count; ++n) {
        const DxilIrExpr *e = &f->exprs[id];
        if (e->kind == DXIL_IR_EXPR_ADDRESS_OF || e->kind == DXIL_IR_EXPR_DEREF || e->kind == DXIL_IR_EXPR_LOAD)
            id = e->inner;
        else if (e->kind == DXIL_IR_EXPR_LOCAL_REF) {
            if (!dxil_ir_resolve_const_local_initializer(f, e->index, &id)) return false;
        } else if (e->kind == DXIL_IR_EXPR_MEMBER || e->kind == DXIL_IR_EXPR_INDEX) {
            if (path->count == DXIL_IR_MAX_EXPRS) return false;
            path->nodes[path->count++] = id;
            id = e->base;
        } else if (e->kind == DXIL_IR_EXPR_GLOBAL_REF) {
            path->global = e->index;
            return true;
        } else return false;
    }
    return false;
}
static bool runtime_atomic(const DxilHlslEmitter *e, const DxilIrFunction *f, const AtomicPath *path) {
    if (path->global >= e->module->global_count || path->count != 1u ||
        f->exprs[path->nodes[0]].kind != DXIL_IR_EXPR_INDEX) return false;
    const DxilIrGlobal *global = &e->module->globals[path->global];
    if (global->ty >= e->module->type_count || global->addr_space != DXIL_IR_ADDR_STORAGE) return false;
    const DxilIrType *type = &e->module->types[global->ty];
    return type->kind == DXIL_IR_TYPE_ARRAY && !type->has_len;
}
static DxilStatus helper_name(DxilHlslEmitter *e, const DxilIrFunction *f,
    const DxilIrExpr *call) {
    if (f < e->module->functions || f >= e->module->functions + e->module->function_count ||
        call < f->exprs || call >= f->exprs + f->expr_count) return DXIL_INVALID_IR;
    TRY(dxil_hlsl_write_text(e, "_doe_atomic_"));
    TRY(dxil_hlsl_write_u32(e, (uint32_t)(f - e->module->functions)));
    TRY(dxil_hlsl_write_text(e, "_"));
    return dxil_hlsl_write_u32(e, (uint32_t)(call - f->exprs));
}
static DxilStatus destination(DxilHlslEmitter *e, const DxilIrFunction *f,
    const AtomicPath *path) {
    TRY(dxil_hlsl_write_text(e, e->module->globals[path->global].name));
    uint32_t index = 0u;
    for (uint32_t n = path->count; n != 0u; --n) {
        const DxilIrExpr *node = &f->exprs[path->nodes[n - 1u]];
        if (node->kind == DXIL_IR_EXPR_MEMBER) {
            TRY(dxil_hlsl_write_text(e, "."));
            TRY(dxil_hlsl_write_text(e, node->field_name));
        } else {
            TRY(dxil_hlsl_write_text(e, "[i"));
            TRY(dxil_hlsl_write_u32(e, index++));
            TRY(dxil_hlsl_write_text(e, "]"));
        }
    }
    return DXIL_OK;
}
static DxilStatus atomic_helper(DxilHlslEmitter *e, const DxilIrFunction *f,
    const DxilIrExpr *call, const AtomicOperation *operation) {
    if (call->args.len != operation->count || call->args.start > f->expr_arg_count ||
        call->args.len > f->expr_arg_count - call->args.start) return DXIL_INVALID_IR;
    AtomicPath path;
    if (!atomic_path(f, f->expr_args[call->args.start], &path)) return DXIL_UNSUPPORTED_CONSTRUCT;
    if (path.global >= e->module->global_count) return DXIL_INVALID_IR;
    const DxilIrGlobal *global = &e->module->globals[path.global];
    if (!global->has_addr_space || (global->addr_space != DXIL_IR_ADDR_WORKGROUP &&
        global->addr_space != DXIL_IR_ADDR_STORAGE)) return DXIL_INVALID_IR;
    const uint32_t target = f->expr_args[call->args.start];
    if (target >= f->expr_count) return DXIL_INVALID_IR;
    DxilIrTypeId type = f->exprs[target].ty;
    for (uint32_t n = 0u; n < e->module->type_count && type < e->module->type_count; ++n) {
        const DxilIrType *t = &e->module->types[type];
        if (t->kind != DXIL_IR_TYPE_REF && t->kind != DXIL_IR_TYPE_ATOMIC) break;
        type = t->elem;
    }
    if (type >= e->module->type_count || e->module->types[type].kind != DXIL_IR_TYPE_SCALAR ||
        (e->module->types[type].scalar != DXIL_IR_SCALAR_U32 && e->module->types[type].scalar != DXIL_IR_SCALAR_I32))
        return DXIL_INVALID_IR;
    const bool store = strcmp(operation->name, "atomicStore") == 0;
    const bool compare = operation->count == 3u;
    TRY(dxil_hlsl_emit_type_only(e, call->ty));
    TRY(dxil_hlsl_write_text(e, " "));
    TRY(helper_name(e, f, call));
    TRY(dxil_hlsl_write_text(e, "("));
    bool first = true;
    uint32_t index = 0u;
    for (uint32_t n = path.count; n != 0u; --n) {
        const DxilIrExpr *node = &f->exprs[path.nodes[n - 1u]];
        if (node->kind != DXIL_IR_EXPR_INDEX) continue;
        if (!first) TRY(dxil_hlsl_write_text(e, ", "));
        TRY(dxil_hlsl_emit_type_only(e, f->exprs[node->index_expr].ty));
        TRY(dxil_hlsl_write_text(e, " i"));
        TRY(dxil_hlsl_write_u32(e, index++));
        first = false;
    }
    for (uint32_t i = 1u; i < call->args.len; ++i) {
        if (!first) TRY(dxil_hlsl_write_text(e, ", "));
        TRY(dxil_hlsl_emit_type_only(e, type));
        TRY(dxil_hlsl_write_text(e, i == 1u ? " a" : " b"));
        first = false;
    }
    TRY(dxil_hlsl_write_text(e, ") { "));
    TRY(dxil_hlsl_emit_type_only(e, store ? type : call->ty));
    TRY(dxil_hlsl_write_text(e, " result; "));
    if (runtime_atomic(e, f, &path)) {
        if (compare) TRY(dxil_hlsl_write_text(e, "result.old_value = 0; result.exchanged = false; "));
        else TRY(dxil_hlsl_write_text(e, "result = 0; "));
        TRY(dxil_hlsl_write_text(e, "uint count = 0u, stride = 0u; "));
        TRY(dxil_hlsl_write_text(e, global->name));
        TRY(dxil_hlsl_write_text(e, ".GetDimensions(count, stride); if (uint(i0) >= count) "));
        TRY(dxil_hlsl_write_text(e, store ? "return; " : "return result; "));
    }
    TRY(dxil_hlsl_write_text(e, operation->intrinsic));
    TRY(dxil_hlsl_write_text(e, "("));
    TRY(destination(e, f, &path));
    TRY(dxil_hlsl_write_text(e, ", "));
    if (operation->count == 1u) TRY(dxil_hlsl_write_text(e, "0"));
    else {
        if (strcmp(operation->name, "atomicSub") == 0) TRY(dxil_hlsl_write_text(e, "0 - "));
        TRY(dxil_hlsl_write_text(e, "a"));
    }
    if (compare) TRY(dxil_hlsl_write_text(e, ", b"));
    TRY(dxil_hlsl_write_text(e, compare ? ", result.old_value); result.exchanged = result.old_value == a; " : ", result); "));
    if (!store) TRY(dxil_hlsl_write_text(e, "return result; "));
    return dxil_hlsl_write_text(e, "}\n");
}
DxilStatus dxil_hlsl_emit_helpers(DxilHlslEmitter *e) {
    bool pack = false;
    for (uint32_t i = 0u; i < e->module->function_count; ++i) {
        const DxilIrFunction *f = &e->module->functions[i];
        for (uint32_t j = 0u; j < f->expr_count; ++j) {
            const DxilIrExpr *call = &f->exprs[j];
            if (call->kind != DXIL_IR_EXPR_CALL || call->call_kind != DXIL_IR_CALL_BUILTIN) continue;
            const AtomicOperation *operation = atomic_operation(call->name);
            if (operation != NULL) TRY(atomic_helper(e, f, call, operation));
            if (strncmp(call->name, "pack", 4u) == 0 || strncmp(call->name, "unpack", 6u) == 0) pack = true;
        }
    }
    if (pack) TRY(dxil_hlsl_write_text(e, hlsl_float_pack_definitions()));
    TRY(dxil_hlsl_emit_numeric_helpers(e));
    return dxil_hlsl_emit_texture_helpers(e);
}
DxilStatus dxil_hlsl_emit_extra_builtin(DxilHlslEmitter *e, const DxilIrFunction *f,
    DxilIrTypeId result, const char *name, DxilIrRange args, bool *handled) {
    *handled = false;
    const AtomicOperation *operation = atomic_operation(name);
    if (operation != NULL) {
        *handled = true;
        const DxilIrExpr *call = NULL;
        for (uint32_t i = 0u; i < f->expr_count; ++i) {
            const DxilIrExpr *candidate = &f->exprs[i];
            if (candidate->kind == DXIL_IR_EXPR_CALL && candidate->call_kind == DXIL_IR_CALL_BUILTIN &&
                candidate->args.start == args.start && candidate->args.len == args.len &&
                candidate->ty == result && strcmp(candidate->name, name) == 0) { call = candidate; break; }
        }
        if (call == NULL || args.len != operation->count) return DXIL_INVALID_IR;
        AtomicPath path;
        if (!atomic_path(f, f->expr_args[args.start], &path)) return DXIL_UNSUPPORTED_CONSTRUCT;
        TRY(helper_name(e, f, call));
        TRY(dxil_hlsl_write_text(e, "("));
        bool first = true;
        for (uint32_t n = path.count; n != 0u; --n) {
            const uint32_t id = path.nodes[n - 1u];
            const DxilIrExpr *node = &f->exprs[id];
            if (node->kind != DXIL_IR_EXPR_INDEX) continue;
            if (!first) TRY(dxil_hlsl_write_text(e, ", "));
            if (dxil_hlsl_pointer_index_captured(e, f, id)) {
                TRY(dxil_hlsl_emit_captured_index(e, f, id, runtime_atomic(e, f, &path)));
            } else {
                uint32_t index = node->index_expr, length;
                if (runtime_atomic(e, f, &path))
                    (void)dxil_ir_runtime_array_write_guard(f, id, &index, &length);
                TRY(dxil_hlsl_emit_expr(e, f, index));
            }
            first = false;
        }
        for (uint32_t i = 1u; i < args.len; ++i) {
            if (!first) TRY(dxil_hlsl_write_text(e, ", "));
            TRY(dxil_hlsl_emit_expr(e, f, f->expr_args[args.start + i]));
            first = false;
        }
        return dxil_hlsl_write_text(e, ")");
    }
    static const char *const packed[] = {"pack4x8snorm", "pack4x8unorm", "pack2x16snorm",
        "pack2x16unorm", "pack2x16float", "unpack4x8snorm", "unpack4x8unorm", "unpack2x16snorm",
        "unpack2x16unorm", "unpack2x16float"};
    for (size_t i = 0u; i < sizeof(packed)/sizeof(packed[0]); ++i) if (strcmp(name, packed[i]) == 0) {
        *handled = true;
        if (args.len != 1u) return DXIL_INVALID_IR;
        TRY(dxil_hlsl_write_text(e, "doe_"));
        TRY(dxil_hlsl_write_text(e, name));
        TRY(dxil_hlsl_write_text(e, "("));
        TRY(dxil_hlsl_emit_expr_list(e, f, args));
        return dxil_hlsl_write_text(e, ")");
    }
    TRY(dxil_hlsl_emit_texture_builtin(e, f, result, name, args, handled));
    if (*handled) return DXIL_OK;
    return dxil_hlsl_emit_numeric_builtin(e, f, result, name, args, handled);
}
