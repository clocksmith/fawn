/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_BUILTIN_H
#define DOE_COMPILER_BUILTIN_H
#include "internal.h"

typedef enum {
    BUILTIN_COMPONENT_NUMERIC, BUILTIN_COMPONENT_FLOAT, BUILTIN_NUMERIC_REDUCTION,
    BUILTIN_FLOAT_REDUCTION, BUILTIN_FLOAT_VECTOR, BUILTIN_FLOAT_CROSS
} BuiltinFamily;

/* This read-only signature owns shape, stage-independent arity and target
 * classification. A zero extended instruction means ordinary SPIR-V lowering. */
typedef struct {
    const char *name;
    NumericBuiltinKind kind;
    uint32_t arity;
    BuiltinFamily family;
    uint32_t extended_instruction;
} NumericBuiltin;

const NumericBuiltin *builtin_named(const Source *, NameSpan);
const NumericBuiltin *builtin_kind(NumericBuiltinKind);
bool builtin_floating(const NumericBuiltin *);
bool builtin_scalar_result(const NumericBuiltin *);
bool builtin_shape(const NumericBuiltin *, uint32_t);
bool builtin_constant(Source *, uint32_t, const NumericBuiltin *,
                      const ConstantValue *, TypeId, ConstantValue *);
/* Bound values are borrowed only for this validation call; neither is mutated. */
bool builtin_clamp_bounds(Source *, uint32_t, const ConstantValue *, const ConstantValue *);
#endif
