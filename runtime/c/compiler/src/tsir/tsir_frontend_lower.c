/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include "tsir_frontend_expr.h"
#include "tsir_frontend_internal.h"
#include "tsir_frontend_passes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

TsirStatus tsir_frontend_lower(const TsirWgslModule *module,
    const uint8_t source_digest[32], const char *frontend_version,
    TsirSemantic *out) {
    TsirSemanticFunction *functions = NULL;
    TsirRejectionList rejections;
    size_t fi = 0;
    TsirStatus status = TSIR_OK;
    if (module == NULL || source_digest == NULL ||
        frontend_version == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    memset(&rejections, 0, sizeof(rejections));
    if (module->function_count > 0) {
        functions = (TsirSemanticFunction *)calloc(
            module->function_count, sizeof(TsirSemanticFunction));
        if (functions == NULL) {
            return TSIR_ERR_NOMEM;
        }
    }
    for (fi = 0; fi < module->function_count; fi++) {
        const TsirIrFunction *ir_func = &module->functions[fi];
        TsirBindingList bindings;
        TsirAxisList axes;
        TsirReductionList reductions;
        TsirCollectiveList collectives;
        TsirKernelFamilyHint hint = TSIR_HINT_UNKNOWN;
        TsirSemanticBody body;
        char *name_copy = NULL;
        memset(&bindings, 0, sizeof(bindings));
        memset(&axes, 0, sizeof(axes));
        memset(&reductions, 0, sizeof(reductions));
        memset(&collectives, 0, sizeof(collectives));
        memset(&body, 0, sizeof(body));
        status = tsir_frontend_extract_bindings(module, ir_func, (uint32_t)fi,
            &bindings, &rejections);
        if (status != TSIR_OK) {
            tsir_frontend_binding_free(&bindings);
            break;
        }
        status = tsir_frontend_recover_axes(module, ir_func, &axes);
        if (status != TSIR_OK) {
            tsir_frontend_binding_free(&bindings);
            tsir_frontend_axis_free(&axes);
            break;
        }
        status = tsir_frontend_recover_reductions(module, ir_func,
            (uint32_t)fi, &reductions, &rejections,
            bindings.global_indices, bindings.global_count);
        if (status != TSIR_OK) {
            tsir_frontend_binding_free(&bindings);
            tsir_frontend_axis_free(&axes);
            tsir_frontend_reduction_free(&reductions);
            break;
        }
        status = tsir_frontend_collect_collectives(module, ir_func,
            (uint32_t)fi, &collectives, &rejections);
        if (status != TSIR_OK) {
            tsir_frontend_binding_free(&bindings);
            tsir_frontend_axis_free(&axes);
            tsir_frontend_reduction_free(&reductions);
            tsir_frontend_collective_free(&collectives);
            break;
        }
        status = tsir_frontend_recover_rejections(ir_func,
            (uint32_t)fi, &rejections);
        if (status != TSIR_OK) {
            tsir_frontend_binding_free(&bindings);
            tsir_frontend_axis_free(&axes);
            tsir_frontend_reduction_free(&reductions);
            tsir_frontend_collective_free(&collectives);
            break;
        }
        hint = tsir_family_hint_infer_ir(ir_func, axes.items,
            axes.count, reductions.items, reductions.count);
        status = tsir_frontend_infer_body(module, hint, axes.items,
            axes.count, bindings.items, bindings.count,
            reductions.items, reductions.count, &body);
        if (status != TSIR_OK) {
            free((void *)body.binding_roles);
            free((void *)body.axis_roles);
            free((void *)body.rms_norm.epsilon.path);
            tsir_frontend_binding_free(&bindings);
            tsir_frontend_axis_free(&axes);
            tsir_frontend_reduction_free(&reductions);
            tsir_frontend_collective_free(&collectives);
            break;
        }
        name_copy = tsir_frontend_dup_str(ir_func->name);
        if (name_copy == NULL) {
            free((void *)body.binding_roles);
            free((void *)body.axis_roles);
            free((void *)body.rms_norm.epsilon.path);
            tsir_frontend_binding_free(&bindings);
            tsir_frontend_axis_free(&axes);
            tsir_frontend_reduction_free(&reductions);
            tsir_frontend_collective_free(&collectives);
            status = TSIR_ERR_NOMEM;
            break;
        }
        functions[fi].name = name_copy;
        functions[fi].family_hint = hint;
        functions[fi].axes = axes.items;
        functions[fi].axis_count = axes.count;
        functions[fi].bindings = bindings.items;
        functions[fi].binding_count = bindings.count;
        functions[fi].reductions = reductions.items;
        functions[fi].reduction_count = reductions.count;
        functions[fi].collectives = collectives.items;
        functions[fi].collective_count = collectives.count;
        functions[fi].body = body;
        memcpy(functions[fi].source_digest, source_digest, 32);
        free(bindings.global_indices);
        bindings.items = NULL;
        bindings.global_indices = NULL;
    }
    if (status != TSIR_OK) {
        size_t done = 0;
        for (done = 0; done < fi; done++) {
            tsir_frontend_free_function_contents(&functions[done]);
        }
        free(functions);
        tsir_frontend_rejection_free(&rejections);
        memset(out, 0, sizeof(*out));
        return status;
    }
    out->contract_version = TSIR_CONTRACT_VERSION;
    out->frontend_version = tsir_frontend_dup_str(frontend_version);
    if (out->frontend_version == NULL) {
        size_t done = 0;
        for (done = 0; done < module->function_count; done++) {
            tsir_frontend_free_function_contents(&functions[done]);
        }
        free(functions);
        tsir_frontend_rejection_free(&rejections);
        memset(out, 0, sizeof(*out));
        return TSIR_ERR_NOMEM;
    }
    out->functions = functions;
    out->function_count = module->function_count;
    out->rejections = rejections.items;
    out->rejection_count = rejections.count;
    return TSIR_OK;
}
