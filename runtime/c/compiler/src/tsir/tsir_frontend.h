/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_FRONTEND_H
#define DOE_TSIR_FRONTEND_H

#include "tsir_schema.h"
#include "tsir_wgsl_ir.h"

/* Frontend, mirroring frontend.zig, frontend_body.zig,
 * frontend_collectives.zig, frontend_expr.zig,
 * frontend_rejections.zig, frontend_types.zig, and
 * family_hint.zig, over the C WGSL IR module in
 * tsir_wgsl_ir.h. The lowering allocates its output (see
 * tsir_frontend_free_semantic); the caller owns the input IR
 * module for the whole call and must not free the output's
 * strings while the semantic is in use.
 *
 * Ownership notes, stated once here because they differ by
 * field: function names, axis strings, binding names and shapes,
 * body role arrays, the rms_norm epsilon path, rejection paths
 * and details, and the frontend_version copy are all owned by
 * the returned semantic and freed by tsir_frontend_free_semantic.
 * Collective exactness invariants point at a static two-element
 * table (reduction_order, associativity_grouping) and are never
 * freed. Input IR strings are borrowed, never copied into the
 * semantic except through the duplicated fields above. */

/* frontend_types.scalarKindFromIr, exactly: abstract_int -> i32,
 * abstract_float -> f32, bool -> u32, void -> u32. */
TsirScalarKind tsir_ir_scalar_to_kind(TsirIrScalar scalar);

/* Pure family-hint classification, mirroring family_hint.infer once
 * its IR evidence is supplied as booleans. Hints never change
 * feasibility: this is a classifier only. */
TsirKernelFamilyHint tsir_family_hint_classify(size_t axis_count,
    size_t reduction_count, bool fused_gemv_evidence,
    bool indirect_access);

/* family_hint.infer over real IR: the evidence booleans above are
 * computed here by walking the function's expressions (indexed
 * access covering both axis locals, indirect buffer access with
 * the Zig depth-8 local-alias cap). */
TsirKernelFamilyHint tsir_family_hint_infer_ir(
    const TsirIrFunction *func, const TsirIterationAxis *axes,
    size_t axis_count, const TsirReductionRegion *reductions,
    size_t reduction_count);

/* Growable lists used while lowering one function. Append
 * functions return false on allocation failure and preserve the
 * list's earlier contents. Rejection append duplicates both
 * strings; axis/binding append takes ownership of the strings
 * already stored in the appended struct. */
typedef struct TsirAxisList {
    TsirIterationAxis *items;
    size_t count;
    size_t cap;
} TsirAxisList;

typedef struct TsirBindingList {
    TsirBufferBinding *items;
    size_t count;
    size_t cap;
    uint32_t *global_indices;
    size_t global_count;
    size_t global_cap;
} TsirBindingList;

typedef struct TsirReductionList {
    TsirReductionRegion *items;
    size_t count;
    size_t cap;
} TsirReductionList;

typedef struct TsirCollectiveList {
    TsirCollectiveSemanticNode *items;
    size_t count;
    size_t cap;
} TsirCollectiveList;

typedef struct TsirRejectionList {
    TsirRejectionEntry *items;
    size_t count;
    size_t cap;
} TsirRejectionList;

bool tsir_frontend_axis_append(TsirAxisList *list,
    TsirIterationAxis axis);
bool tsir_frontend_binding_append(TsirBindingList *list,
    TsirBufferBinding binding, uint32_t global_index);
bool tsir_frontend_reduction_append(TsirReductionList *list,
    TsirReductionRegion reduction);
bool tsir_frontend_collective_append(TsirCollectiveList *list,
    TsirCollectiveSemanticNode node);
bool tsir_frontend_rejection_append(TsirRejectionList *list,
    TsirRejectionReason reason, const char *node_path,
    const char *detail);
void tsir_frontend_axis_free(TsirAxisList *list);
void tsir_frontend_binding_free(TsirBindingList *list);
void tsir_frontend_reduction_free(TsirReductionList *list);
void tsir_frontend_collective_free(TsirCollectiveList *list);
void tsir_frontend_rejection_free(TsirRejectionList *list);

/* Full WGSL IR -> TSIR lowering (frontend.lowerIrToTsir). NULL
 * module, digest, version, or out is TSIR_ERR_INVALID. On any
 * failure *out is left zeroed and nothing leaks from the call. */
TsirStatus tsir_frontend_lower(const TsirWgslModule *module,
    const uint8_t source_digest[32], const char *frontend_version,
    TsirSemantic *out);
void tsir_frontend_free_semantic(TsirSemantic *semantic);

#endif
