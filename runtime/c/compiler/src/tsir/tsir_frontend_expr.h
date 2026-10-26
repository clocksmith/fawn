/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_FRONTEND_EXPR_H
#define DOE_TSIR_FRONTEND_EXPR_H

#include "tsir_wgsl_ir.h"

/* frontend_expr.zig, ported. String results are freshly allocated
 * (free them); a return of 1 means "found, *out allocated", 0 means
 * "shape not recognized, fall back to the Zig placeholder", and -1
 * means allocation failure. Callers map -1 to TSIR_ERR_NOMEM and
 * never treat 0 as an error. All bound strings follow Zig's
 * exclusive-upper-bound convention: `i <= N` gives N + 1, and the
 * same +1 suffix applies to symbolic and dispatch-guard bounds. */

typedef enum TsirStepSign {
    TSIR_STEP_POSITIVE = 0,
    TSIR_STEP_NEGATIVE = 1,
    TSIR_STEP_UNKNOWN = 2
} TsirStepSign;

int tsir_expr_literal_upper_bound(const TsirIrFunction *func,
    bool has_cond, uint32_t cond_id, uint32_t induction,
    uint64_t *out);
int tsir_expr_symbolic_upper_bound(const TsirWgslModule *module,
    const TsirIrFunction *func, bool has_cond, uint32_t cond_id,
    uint32_t induction, char **out);
int tsir_expr_extract_step(const TsirWgslModule *module,
    const TsirIrFunction *func, bool has_cont, uint32_t cont_id,
    uint32_t induction, char **out);
int tsir_expr_extract_init_bound(const TsirWgslModule *module,
    const TsirIrFunction *func, bool has_init, uint32_t init_id,
    char **out);
TsirStepSign tsir_expr_detect_step_sign(const TsirIrFunction *func,
    bool has_cont, uint32_t cont_id, uint32_t induction);
bool tsir_expr_dispatch_axis_letter(const TsirIrFunction *func,
    bool has_init, uint32_t init_id, const char **letter_out);
int tsir_expr_scan_dispatch_guard(const TsirWgslModule *module,
    const TsirIrFunction *func, uint32_t range_start,
    uint32_t range_len, uint32_t start_pos, uint32_t dispatch_local,
    char **out);
bool tsir_expr_uniform_field_access(const TsirIrFunction *func,
    uint32_t expr_id, uint32_t *global_index_out,
    const char **field_name_out);
int tsir_expr_override_or_const_name(const TsirIrGlobal *global,
    char **out);

#endif
