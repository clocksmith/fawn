/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_planner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TSIR_FIRST_FIT_TILE_CAP 64u
#define TSIR_FIRST_FIT_PE_GRID_WIDTH_CAP 8u

/* ---- small owned-list helpers: first error aborts the plan ---- */

typedef struct PlanList {
    TsirRejectionEntry *items;
    size_t count;
    size_t cap;
} PlanList;

static TsirStatus list_push(PlanList *list, TsirRejectionEntry entry) {
    TsirRejectionEntry *grown = NULL;
    if (list->count == list->cap) {
        size_t next = list->cap == 0 ? 8 : list->cap * 2;
        grown = (TsirRejectionEntry *)realloc(list->items,
            next * sizeof(*grown));
        if (grown == NULL) {
            return TSIR_ERR_NOMEM;
        }
        list->items = grown;
        list->cap = next;
    }
    list->items[list->count] = entry;
    list->count += 1;
    return TSIR_OK;
}

static char *dup_string(const char *text) {
    size_t len = 0;
    char *copy = NULL;
    if (text == NULL) {
        return NULL;
    }
    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

static TsirStatus append_rejection(PlanList *list,
    TsirRejectionReason reason, uint32_t function_index, const char *field,
    uint32_t node_index, const char *detail_text) {
    char path[128];
    char *path_copy = NULL;
    char *detail_copy = NULL;
    TsirRejectionEntry entry;
    int written = snprintf(path, sizeof(path), "functions[%u].%s[%u]",
        function_index, field, node_index);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return TSIR_ERR_INVALID;
    }
    path_copy = dup_string(path);
    detail_copy = dup_string(detail_text);
    if (path_copy == NULL || detail_copy == NULL) {
        free(path_copy);
        free(detail_copy);
        return TSIR_ERR_NOMEM;
    }
    entry.reason = reason;
    entry.node_path = path_copy;
    entry.detail = detail_copy;
    return list_push(list, entry);
}

void tsir_planner_default_options(TsirPlannerOptions *options) {
    if (options == NULL) {
        return;
    }
    options->loader.fabric_streaming = false;
    options->loader.max_stream_chunk_bytes = 0;
    memset(options->emitter_digest, 0, 32);
    options->emitter_params_json = TSIR_DEFAULT_EMITTER_PARAMS_JSON;
}

uint32_t tsir_planner_choose_tile(const char *upper_bound) {
    uint64_t parsed = 0;
    size_t i = 0;
    uint32_t capped = 0;
    uint32_t out = 1;
    if (upper_bound == NULL || upper_bound[0] == '\0') {
        return 1;
    }
    i = 0;
    if (upper_bound[0] == '+') {
        i = 1;
        if (upper_bound[1] == '\0') {
            return 1;
        }
    }
    for (; upper_bound[i] != '\0'; i++) {
        char c = upper_bound[i];
        if (c < '0' || c > '9') {
            return 1;
        }
        parsed = parsed * 10 + (uint64_t)(c - '0');
        if (parsed > 0xffffffffu) {
            return 1;
        }
    }
    if (parsed == 0) {
        return 1;
    }
    capped = parsed > TSIR_FIRST_FIT_TILE_CAP ? TSIR_FIRST_FIT_TILE_CAP :
        (uint32_t)parsed;
    while (out <= capped / 2) {
        out *= 2;
    }
    return out;
}

TsirPEGridShape tsir_planner_choose_pe_grid(
    const TsirTargetDescriptor *descriptor) {
    TsirPEGridShape grid;
    uint32_t width = 0;
    if (descriptor == NULL || descriptor->fabric_color_count == 0) {
        grid.width = 1;
        grid.height = 1;
        return grid;
    }
    width = descriptor->max_collective_group_size;
    if (width < 1) {
        width = 1;
    }
    if (width > TSIR_FIRST_FIT_PE_GRID_WIDTH_CAP) {
        width = TSIR_FIRST_FIT_PE_GRID_WIDTH_CAP;
    }
    grid.width = width;
    grid.height = 1;
    return grid;
}

bool tsir_supports_numerical_mode(const TsirTargetDescriptor *descriptor,
    TsirScalarKind kind) {
    const char *mode = NULL;
    size_t i = 0;
    if (descriptor == NULL) {
        return false;
    }
    switch (kind) {
        case TSIR_SCALAR_F32:
            mode = "f32";
            break;
        case TSIR_SCALAR_F16:
            mode = "f16";
            break;
        case TSIR_SCALAR_BF16:
            mode = "bf16";
            break;
        case TSIR_SCALAR_I32:
        case TSIR_SCALAR_U32:
        default:
            return false;
    }
    for (i = 0; i < descriptor->native_numerical_mode_count; i++) {
        if (descriptor->native_numerical_modes[i] != NULL &&
            strcmp(descriptor->native_numerical_modes[i], mode) == 0) {
            return true;
        }
    }
    return false;
}

static bool estimate_binding_bytes(const TsirBufferBinding *binding,
    uint64_t *out) {
    uint64_t elems = 1;
    size_t i = 0;
    uint64_t elem_size = 0;
    if (binding->logical_shape_len > 0 && binding->logical_shape == NULL) {
        return false;
    }
    elem_size = tsir_scalar_byte_size(binding->elem);
    if (elem_size == 0) {
        return false;
    }
    for (i = 0; i < binding->logical_shape_len; i++) {
        uint64_t dim = binding->logical_shape[i];
        if (dim == 0) {
            return false;
        }
        if (elems > UINT64_MAX / dim) {
            return false;
        }
        elems *= dim;
    }
    if (elems > UINT64_MAX / elem_size) {
        return false;
    }
    *out = elems * elem_size;
    return true;
}

static bool choose_slice_axis(const TsirBufferBinding *binding,
    uint32_t *out_axis) {
    uint64_t best_dim = 0;
    uint32_t best_index = 0;
    size_t i = 0;
    if (binding->logical_shape_len == 0 || binding->logical_shape == NULL) {
        return false;
    }
    best_dim = binding->logical_shape[0];
    for (i = 1; i < binding->logical_shape_len; i++) {
        if (binding->logical_shape[i] > best_dim) {
            best_dim = binding->logical_shape[i];
            best_index = (uint32_t)i;
        }
    }
    if (best_dim <= 1) {
        return false;
    }
    *out_axis = best_index;
    return true;
}

static uint64_t choose_stream_chunk_bytes(
    const TsirTargetDescriptor *descriptor,
    const TsirLoaderCapabilities *loader) {
    uint64_t budget = descriptor->pe_working_memory_bytes;
    if (budget < 1) {
        budget = 1;
    }
    if (loader->max_stream_chunk_bytes == 0) {
        return budget;
    }
    return loader->max_stream_chunk_bytes < budget ?
        loader->max_stream_chunk_bytes : budget;
}

static bool collective_exactness_satisfies(const char *capability_name,
    TsirExactnessClass required) {
    const char *required_name = tsir_exactness_class_name(required);
    if (capability_name == NULL || required_name == NULL) {
        return false;
    }
    if (strcmp(capability_name, required_name) == 0) {
        return true;
    }
    if (strcmp(capability_name, "bit_exact_solo") == 0) {
        return required == TSIR_EXACTNESS_ALGORITHM_EXACT ||
            required == TSIR_EXACTNESS_TOLERANCE_BOUNDED;
    }
    return false;
}

static bool supports_collective(const TsirTargetDescriptor *descriptor,
    TsirCollectiveKind kind, TsirExactnessClass exactness) {
    const char *kind_name = tsir_collective_kind_name(kind);
    size_t i = 0;
    if (kind_name == NULL) {
        return false;
    }
    for (i = 0; i < descriptor->native_collective_count; i++) {
        const TsirNativeCollective *cap = &descriptor->native_collectives[i];
        if (cap->kind_name != NULL &&
            strcmp(cap->kind_name, kind_name) == 0 &&
            collective_exactness_satisfies(cap->exactness_name, exactness)) {
            return true;
        }
    }
    return false;
}

static bool needs_fabric_color(TsirCollectiveKind kind) {
    return kind == TSIR_COLLECTIVE_FABRIC_REDUCE ||
        kind == TSIR_COLLECTIVE_FABRIC_BROADCAST ||
        kind == TSIR_COLLECTIVE_FABRIC_ALLREDUCE;
}

/* Plan one function's residency, collectives, and reductions, in the
 * Zig order (residency, then collectives, then reductions), appending
 * rejections to list. Allocated per-function arrays are returned
 * through the out parameters; on error the caller frees partials. */
typedef struct PlannedFunction {
    uint32_t *tiles;
    TsirResidencyDecision *residency;
    size_t residency_count;
    TsirCollectiveRealizationNode *collectives;
    size_t collective_count;
    TsirReductionRealizationNode *reductions;
    size_t reduction_count;
} PlannedFunction;

static void free_planned_function(PlannedFunction *pf) {
    free(pf->tiles);
    free(pf->residency);
    free(pf->collectives);
    free(pf->reductions);
    pf->tiles = NULL;
    pf->residency = NULL;
    pf->collectives = NULL;
    pf->reductions = NULL;
    pf->residency_count = 0;
    pf->collective_count = 0;
    pf->reduction_count = 0;
}

static TsirStatus plan_function(const TsirSemanticFunction *func,
    uint32_t function_index, const TsirTargetDescriptor *descriptor,
    TsirPEGridShape pe_grid, const TsirLoaderCapabilities *loader,
    PlanList *rejections, PlannedFunction *pf) {
    size_t i = 0;
    size_t residency_cap = 0;
    size_t collective_cap = 0;
    size_t reduction_cap = 0;
    TsirStatus status = TSIR_OK;

    memset(pf, 0, sizeof(*pf));
    if (func->axis_count > 0) {
        pf->tiles = (uint32_t *)malloc(func->axis_count * sizeof(uint32_t));
        if (pf->tiles == NULL) {
            return TSIR_ERR_NOMEM;
        }
        for (i = 0; i < func->axis_count; i++) {
            pf->tiles[i] = tsir_planner_choose_tile(func->axes[i].upper_bound);
        }
    }

    /* Residency. */
    residency_cap = func->binding_count == 0 ? 0 : func->binding_count;
    if (residency_cap > 0) {
        pf->residency = (TsirResidencyDecision *)malloc(
            residency_cap * sizeof(TsirResidencyDecision));
        if (pf->residency == NULL) {
            status = TSIR_ERR_NOMEM;
            goto done;
        }
    }
    for (i = 0; i < func->binding_count; i++) {
        const TsirBufferBinding *binding = &func->bindings[i];
        uint64_t bytes = 0;
        bool estimated = estimate_binding_bytes(binding, &bytes);
        TsirResidencyDecision decision;
        memset(&decision, 0, sizeof(decision));
        decision.binding_index = (uint32_t)i;
        if (estimated) {
            if (bytes <= descriptor->pe_working_memory_bytes) {
                decision.class = TSIR_RESIDENCY_PE_REPLICATED;
                pf->residency[pf->residency_count] = decision;
                pf->residency_count += 1;
                continue;
            }
            {
                uint32_t axis = 0;
                if (choose_slice_axis(binding, &axis)) {
                    uint32_t shards = pe_grid.width < 1 ? 1 : pe_grid.width;
                    uint64_t sliced = bytes / shards +
                        (bytes % shards != 0 ? 1 : 0);
                    if (sliced <= descriptor->pe_working_memory_bytes) {
                        decision.class = TSIR_RESIDENCY_PE_SLICED;
                        decision.axis.has = true;
                        decision.axis.value = axis;
                        decision.shards.has = true;
                        decision.shards.value = shards;
                        pf->residency[pf->residency_count] = decision;
                        pf->residency_count += 1;
                        continue;
                    }
                }
            }
        } else {
            if (descriptor->runtime_sized_binding_policy ==
                TSIR_POLICY_HOST_COPIED) {
                decision.class = TSIR_RESIDENCY_HOST_COPIED;
                pf->residency[pf->residency_count] = decision;
                pf->residency_count += 1;
                continue;
            }
            if (descriptor->runtime_sized_binding_policy ==
                TSIR_POLICY_FABRIC_STREAMED_WITH_LOADER &&
                loader->fabric_streaming &&
                descriptor->fabric_color_count > 0) {
                decision.class = TSIR_RESIDENCY_FABRIC_STREAMED;
                decision.fabric_color.has = true;
                decision.fabric_color.value = 0;
                decision.chunk_bytes.has = true;
                decision.chunk_bytes.value =
                    choose_stream_chunk_bytes(descriptor, loader);
                pf->residency[pf->residency_count] = decision;
                pf->residency_count += 1;
                continue;
            }
        }
        if (loader->fabric_streaming && descriptor->fabric_color_count > 0) {
            decision.class = TSIR_RESIDENCY_FABRIC_STREAMED;
            decision.fabric_color.has = true;
            decision.fabric_color.value = 0;
            decision.chunk_bytes.has = true;
            decision.chunk_bytes.value =
                choose_stream_chunk_bytes(descriptor, loader);
            pf->residency[pf->residency_count] = decision;
            pf->residency_count += 1;
            continue;
        }
        status = append_rejection(rejections, TSIR_REJECT_PE_BUDGET_EXHAUSTED,
            function_index, "bindings", (uint32_t)i,
            "binding footprint exceeds per-PE budget without legal slice or stream");
        if (status != TSIR_OK) {
            goto done;
        }
    }

    /* Collective synthesis. */
    collective_cap = func->collective_count;
    if (collective_cap > 0) {
        pf->collectives = (TsirCollectiveRealizationNode *)malloc(
            collective_cap * sizeof(TsirCollectiveRealizationNode));
        if (pf->collectives == NULL) {
            status = TSIR_ERR_NOMEM;
            goto done;
        }
    }
    {
        uint32_t next_color = 0;
        for (i = 0; i < func->collective_count; i++) {
            const TsirCollectiveSemanticNode *node = &func->collectives[i];
            TsirCollectiveRealizationNode out_node;
            uint32_t group_size = 0;
            uint32_t max_group = descriptor->max_collective_group_size;
            if (node->kind != TSIR_COLLECTIVE_WORKGROUP_BARRIER &&
                !tsir_supports_numerical_mode(descriptor, node->dtype)) {
                status = append_rejection(rejections,
                    TSIR_REJECT_TARGET_UNFIT, function_index, "collectives",
                    (uint32_t)i,
                    "collective dtype is not native on target");
                if (status != TSIR_OK) {
                    goto done;
                }
                continue;
            }
            if (!supports_collective(descriptor, node->kind,
                node->exactness.class)) {
                status = append_rejection(rejections,
                    TSIR_REJECT_COLLECTIVE_NOT_REPRESENTABLE, function_index,
                    "collectives", (uint32_t)i,
                    "target lacks native collective exactness");
                if (status != TSIR_OK) {
                    goto done;
                }
                continue;
            }
            memset(&out_node, 0, sizeof(out_node));
            out_node.semantic_index = (uint32_t)i;
            out_node.tree_shape = TSIR_TREE_LINEAR;
            if (needs_fabric_color(node->kind)) {
                if (next_color >= descriptor->fabric_color_count) {
                    status = append_rejection(rejections,
                        TSIR_REJECT_COLLECTIVE_NOT_REPRESENTABLE,
                        function_index, "collectives", (uint32_t)i,
                        "target fabric color budget exhausted");
                    if (status != TSIR_OK) {
                        goto done;
                    }
                    continue;
                }
                out_node.fabric_color.has = true;
                out_node.fabric_color.value = next_color;
                next_color += 1;
            }
            if (max_group < 1) {
                max_group = 1;
            }
            if (node->axis >= 0 &&
                (size_t)node->axis < func->axis_count) {
                uint32_t tile = pf->tiles[node->axis];
                uint32_t capped = tile < 1 ? 1 : tile;
                group_size = capped < max_group ? capped : max_group;
            } else {
                group_size = max_group;
            }
            out_node.group_size = group_size;
            pf->collectives[pf->collective_count] = out_node;
            pf->collective_count += 1;
        }
    }

    /* Reduction synthesis. */
    reduction_cap = func->reduction_count;
    if (reduction_cap > 0) {
        pf->reductions = (TsirReductionRealizationNode *)malloc(
            reduction_cap * sizeof(TsirReductionRealizationNode));
        if (pf->reductions == NULL) {
            status = TSIR_ERR_NOMEM;
            goto done;
        }
    }
    for (i = 0; i < func->reduction_count; i++) {
        const TsirReductionRegion *reduction = &func->reductions[i];
        if (!tsir_supports_numerical_mode(descriptor,
            reduction->contract.accumulation)) {
            status = append_rejection(rejections, TSIR_REJECT_TARGET_UNFIT,
                function_index, "reductions", (uint32_t)i,
                "reduction accumulation dtype is not native on target");
            if (status != TSIR_OK) {
                goto done;
            }
            continue;
        }
        if (reduction->contract.associativity ==
            TSIR_ASSOC_ASSOCIATIVE_ALLOWED) {
            TsirReductionRealizationNode node;
            node.semantic_index = (uint32_t)i;
            node.tree_shape = TSIR_TREE_LINEAR;
            pf->reductions[pf->reduction_count] = node;
            pf->reduction_count += 1;
        }
    }

done:
    if (status != TSIR_OK) {
        free_planned_function(pf);
    }
    return status;
}

TsirStatus tsir_plan_realization(const TsirSemantic *semantic,
    const TsirTargetDescriptor *descriptor,
    const TsirPlannerOptions *options, TsirPlannedRealization *out) {
    TsirPlannerOptions defaults;
    TsirPEGridShape pe_grid;
    PlanList rejections;
    TsirRealizationFunction *functions = NULL;
    size_t i = 0;
    TsirStatus status = TSIR_OK;
    if (semantic == NULL || descriptor == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (semantic->function_count > 0 && semantic->functions == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (options == NULL) {
        tsir_planner_default_options(&defaults);
        options = &defaults;
    }
    if (options->emitter_params_json == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    memset(&rejections, 0, sizeof(rejections));
    pe_grid = tsir_planner_choose_pe_grid(descriptor);
    if (semantic->function_count > 0) {
        functions = (TsirRealizationFunction *)calloc(
            semantic->function_count, sizeof(TsirRealizationFunction));
        if (functions == NULL) {
            return TSIR_ERR_NOMEM;
        }
    }
    for (i = 0; i < semantic->function_count; i++) {
        PlannedFunction pf;
        TsirRealizationFunction *rf = &functions[i];
        char *params_copy = NULL;
        status = plan_function(&semantic->functions[i], (uint32_t)i,
            descriptor, pe_grid, &options->loader, &rejections, &pf);
        if (status != TSIR_OK) {
            goto fail;
        }
        params_copy = dup_string(options->emitter_params_json);
        if (params_copy == NULL) {
            free_planned_function(&pf);
            status = TSIR_ERR_NOMEM;
            goto fail;
        }
        rf->semantic_index = (uint32_t)i;
        rf->tiles.per_axis = pf.tiles;
        rf->tiles.per_axis_count = semantic->functions[i].axis_count;
        rf->pe_grid = pe_grid;
        rf->residency = pf.residency;
        rf->residency_count = pf.residency_count;
        rf->collectives = pf.collectives;
        rf->collective_count = pf.collective_count;
        rf->reductions = pf.reductions;
        rf->reduction_count = pf.reduction_count;
        rf->emitter_params_json = params_copy;
        /* Target descriptor hash: computed by the targets module in
         * Zig (descriptorHash), which is outside TSIR scope. The
         * planner output leaves it zeroed here; the coordinator must
         * supply the hash before a plan is digest-compared or
         * emitted. See gaps.md. */
        memset(rf->target_descriptor_hash, 0, 32);
    }
    out->functions = functions;
    out->function_count = semantic->function_count;
    out->contract_version = TSIR_CONTRACT_VERSION;
    memcpy(out->emitter_digest, options->emitter_digest, 32);
    out->rejections = rejections.items;
    out->rejection_count = rejections.count;
    return TSIR_OK;

fail:
    for (i = 0; i < semantic->function_count; i++) {
        free((void *)functions[i].tiles.per_axis);
        free((void *)functions[i].residency);
        free((void *)functions[i].collectives);
        free((void *)functions[i].reductions);
        free((void *)functions[i].emitter_params_json);
    }
    free(functions);
    for (i = 0; i < rejections.count; i++) {
        free((void *)rejections.items[i].node_path);
        free((void *)rejections.items[i].detail);
    }
    free(rejections.items);
    memset(out, 0, sizeof(*out));
    return status;
}

void tsir_plan_free_realization(TsirPlannedRealization *planned) {
    size_t i = 0;
    if (planned == NULL) {
        return;
    }
    for (i = 0; i < planned->function_count; i++) {
        free((void *)planned->functions[i].tiles.per_axis);
        free((void *)planned->functions[i].residency);
        free((void *)planned->functions[i].collectives);
        free((void *)planned->functions[i].reductions);
        free((void *)planned->functions[i].emitter_params_json);
    }
    free(planned->functions);
    for (i = 0; i < planned->rejection_count; i++) {
        free((void *)planned->rejections[i].node_path);
        free((void *)planned->rejections[i].detail);
    }
    free(planned->rejections);
    memset(planned, 0, sizeof(*planned));
}
