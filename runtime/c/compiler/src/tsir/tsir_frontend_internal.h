/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_FRONTEND_INTERNAL_H
#define DOE_TSIR_FRONTEND_INTERNAL_H

#include "tsir_frontend.h"

/* Shared by tsir_frontend.c and tsir_frontend_lower.c; not part of
 * the public frontend API. */
char *tsir_frontend_dup_str(const char *text);
void tsir_frontend_free_function_contents(TsirSemanticFunction *func);
TsirStatus tsir_frontend_extract_bindings(
    const TsirWgslModule *module, const TsirIrFunction *func,
    uint32_t func_index, TsirBindingList *out,
    TsirRejectionList *rejections);
TsirStatus tsir_frontend_recover_axes(const TsirWgslModule *module,
    const TsirIrFunction *func, TsirAxisList *axes);
TsirStatus tsir_frontend_recover_reductions(
    const TsirWgslModule *module, const TsirIrFunction *func,
    uint32_t func_index, TsirReductionList *reductions,
    TsirRejectionList *rejections, const uint32_t *global_indices,
    size_t global_index_count);

#endif
