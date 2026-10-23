/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_NUMERIC_H
#define DOE_COMPILER_NUMERIC_H
#include "internal.h"
#include <fenv.h>

/* A pipeline owns this automatic record. No helper changes process-global state.
 * Finish restoration before publishing output, on failure as well as success. */
typedef struct {
    fenv_t saved;
    uint32_t saved_mxcsr;
    bool active;
} NumericEnvironment;

bool numeric_environment_begin(Source *source, NumericEnvironment *environment);
bool numeric_environment_end(Source *source, NumericEnvironment *environment);
const ScalarBuiltin *scalar_builtin_named(const char *name);
/* All arguments have the selected overload type and have been materialized.
 * The signature registry owns arity; callers provide its complete argument span. */
bool numeric_builtin(Source *source, size_t offset, ScalarBuiltinKind kind,
                     const NumericValue *arguments, NumericValue *result);
bool numeric_parse_literal(Source *source, NameSpan literal, NumericValue *result);
bool numeric_materialize(Source *source, size_t offset, NumericValue input,
                         IrType target, NumericValue *result);
/* The caller selects constructor overloads and materializes their arguments.
 * Explicit conversion is not an overload-selection or automatic-conversion rule. */
bool numeric_convert(Source *source, size_t offset, NumericValue input,
                     IrType target, NumericValue *result);
bool numeric_bitcast(Source *source, size_t offset, NumericValue input,
                     IrType target, NumericValue *result);
/* Operands already have the selected common type; a shift's RHS is u32.
 * The caller decides whether an expression is evaluated, including short circuit.
 * These helpers evaluate const values and reject invalid evaluated operations. */
bool numeric_unary(Source *source, size_t offset, ExpressionKind operation,
                   NumericValue input, NumericValue *result);
bool numeric_binary(Source *source, size_t offset, ExpressionKind operation,
                    Comparison comparison, NumericValue left, NumericValue right,
                    NumericValue *result);
#endif
