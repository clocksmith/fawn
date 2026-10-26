/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_NUMERIC_H
#define DOE_COMPILER_NUMERIC_H
#include "builtin.h"
#include <fenv.h>

/* A pipeline owns this automatic record. No helper changes process-global state.
 * Finish restoration before publishing output, on failure as well as success. */
typedef struct {
    fenv_t saved;
#if defined(__x86_64__)
    uint32_t saved_mxcsr;
    uint16_t saved_x87_control;
#elif defined(__aarch64__) || defined(__arm64__)
    uint64_t saved_fpcr, saved_fpsr;
#endif
    bool active;
} NumericEnvironment;

bool numeric_environment_begin(Source *source, NumericEnvironment *environment);
bool numeric_environment_end(Source *source, NumericEnvironment *environment);
/* All arguments have the selected overload type and have been materialized.
 * The signature registry owns arity; callers provide its complete argument span. */
bool numeric_builtin(Source *source, size_t offset, const NumericBuiltin *signature,
                     const NumericValue *arguments, NumericValue *result);
/* Declared-width scalar libm evaluation; caller owns finite bit packing. */
bool numeric_math(Source *, size_t, NumericBuiltinKind, bool single,
                  double input, double other, double *result);
/* Two signed-magnitude sums cover four signed64 products, including 2^128.
 * The caller initializes to zero and adds at most four admitted AI lane pairs. */
typedef struct { uint32_t positive[5], negative[5]; } NumericDotAccumulator;
bool numeric_dot_accumulate(Source *, size_t, NumericDotAccumulator *, NumericValue, NumericValue);
bool numeric_dot_finish(Source *, size_t, const NumericDotAccumulator *, NumericValue *);
double numeric_half_value(uint64_t bits);
bool numeric_half_pack(Source *, size_t, double, NumericValue *);
bool numeric_parse_literal(Source *source, NameSpan literal, NumericValue *result);
bool numeric_materialize(Source *source, size_t offset, NumericValue input,
                         IrType target, NumericValue *result);
/* API pipeline values reject fractional/out-of-range integers rather than
 * using WGSL explicit constructors' truncating/saturating conversions. */
bool numeric_override(Source *, size_t, double, IrType, NumericValue *);
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
/* Diagnostic-free partial fold of concrete runtime integer operations. Shifts
 * mask the u32 count modulo 32; const-stage numeric_binary checks are unchanged.
 * Unsupported types/operations and undefined division decline without modifying
 * result or source diagnostics. Admitted arithmetic uses the scalar owner. */
bool numeric_runtime_integer_binary(Source *, ExpressionKind, NumericValue,
                                    NumericValue, NumericValue *);
uint32_t numeric_integer_bits(NumericBuiltinKind, uint32_t, bool);
#endif
