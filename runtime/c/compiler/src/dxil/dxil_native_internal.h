/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_NATIVE_INTERNAL_H
#define DOE_DXIL_NATIVE_INTERNAL_H

#include "dxil_builder.h"
#include "dxil_mapping.h"
#include "dxil_ir.h"
#include "dxil_native.h"

#define DXIL_NATIVE_MAX_DEPTH 64u

typedef struct DxilNativeResource {
    uint32_t class;
    uint32_t kind;
    uint32_t group;
    uint32_t binding;
    uint32_t global_index;
    DxilValueIndex handle_value;
} DxilNativeResource;

typedef struct DxilNativeEmitter {
    const DxilIrModule *module;
    DxilBuilder builder;
    DxilNativeResource resources[DXIL_NATIVE_MAX_GLOBALS];
    uint32_t resource_count;
    uint32_t dx_op_fn;
    DxilTypeIndex i32_ty;
    DxilTypeIndex f32_ty;
    DxilTypeIndex void_ty;
    DxilTypeIndex i1_ty;
} DxilNativeEmitter;

/* Const and dx.op-call helpers defined in dxil_native.c, used by the
 * statement/expression walk in dxil_native_walk.c. */
DxilStatus dxil_native_const_value(DxilNativeEmitter *emitter,
    DxilTypeIndex ty, DxilConstKind kind, int64_t int_value,
    uint64_t uint_value, uint64_t float_bits, DxilValueIndex *out);
DxilStatus dxil_native_const_i32(DxilNativeEmitter *emitter,
    int32_t value, DxilValueIndex *out);
DxilStatus dxil_native_const_u32(DxilNativeEmitter *emitter,
    uint32_t value, DxilValueIndex *out);
DxilStatus dxil_native_const_f32(DxilNativeEmitter *emitter,
    float value, DxilValueIndex *out);
DxilStatus dxil_native_dx_op_call(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, uint32_t opcode,
    const DxilValueIndex *extra_args, size_t extra_count,
    DxilValueIndex *out);

/* Entry-body dispatch defined in dxil_native_walk.c, used by
 * dxil_native_emit in dxil_native.c. */
DxilStatus dxil_native_emit_entry_body(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function,
    DxilIrStage stage);

#endif /* DOE_DXIL_NATIVE_INTERNAL_H */
