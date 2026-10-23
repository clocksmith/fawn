/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../ir_view.h"
#include <inttypes.h>

typedef struct {
    uint32_t steps[DOE_HLSL_MAX_DEPTH];
    uint32_t count;
} HlslPath;
static bool collect_path(HlslEmitter *e, uint32_t path, HlslPath *out) {
    if (e->failed) return false;
    const char *error = target_access_path(e->module, path, out->steps,
        DOE_HLSL_MAX_DEPTH, &out->count);
    if (error != NULL) hlsl_error(e, error);
    return error == NULL;
}
static const TypeRecord *path_input(HlslEmitter *e, const AccessStep *step) {
    if (step->input_type < 7u || step->input_type - 7u >= e->module->type_count) {
        hlsl_error(e, "Hlsl access requires an aggregate input type");
        return NULL;
    }
    return &e->module->types[step->input_type - 7u];
}
static const ResourceRecord *resource_root(HlslEmitter *e, const IrInstruction *ir) {
    if (ir->operands.access.category != ROOT_RESOURCE) return NULL;
    if (ir->operands.access.root >= e->module->resource_count) {
        hlsl_error(e, "Hlsl resource root identity is invalid");
        return NULL;
    }
    return &e->module->resources[ir->operands.access.root];
}
static void bounds(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path) {
    hlsl_text(e, "true");
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        if (step->kind == ACCESS_MEMBER || step->kind == ACCESS_COMPONENT) continue;
        const TypeRecord *input = path_input(e, step);
        if (input == NULL) return;
        if (step->kind == ACCESS_INDEX_I32) {
            hlsl_text(e, " && (");
            hlsl_value(e, step->index);
            hlsl_text(e, " >= 0)");
        } else if (step->kind != ACCESS_INDEX_U32) {
            hlsl_error(e, "Hlsl dynamic access kind is unsupported");
            return;
        }
        hlsl_text(e, " && (uint(");
        hlsl_value(e, step->index);
        hlsl_text(e, ") < ");
        if (input->kind == TYPE_RUNTIME_ARRAY) {
            const ResourceRecord *r = resource_root(e, ir);
            if (r == NULL || r->tail_stride == 0u || r->tail_stride > UINT32_MAX ||
                r->tail_offset > UINT32_MAX) {
                hlsl_error(e, "HLSL runtime array has no bounded resource root");
                return;
            }
            hlsl_format(e, "(bytes >= %" PRIu64 "u ? (bytes - %" PRIu64 "u) / %" PRIu64 "u : 0u)",
                        r->tail_offset, r->tail_offset, r->tail_stride);
        } else hlsl_format(e, "%uu", input->count);
        hlsl_text(e, ")");
    }
}
static void byte_offset(HlslEmitter *e, const HlslPath *path) {
    hlsl_text(e, "0u");
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        const TypeRecord *input = path_input(e, step);
        if (input == NULL) return;
        if (step->kind == ACCESS_MEMBER) {
            if (input->kind != TYPE_STRUCT || step->index >= input->count ||
                input->element >= e->module->member_count || step->index >= e->module->member_count - input->element) {
                hlsl_error(e, "Hlsl member access identity is invalid");
                return;
            }
            hlsl_format(e, " + %" PRIu64 "u", e->module->members[input->element + step->index].offset);
        } else if (step->kind == ACCESS_COMPONENT) {
            hlsl_format(e, " + %uu", step->index * 4u);
        } else {
            const uint64_t stride = input->kind == TYPE_VECTOR ? 4u : input->state.layout.stride;
            hlsl_text(e, " + uint(");
            hlsl_value(e, step->index);
            hlsl_format(e, ") * %" PRIu64 "u", stride);
        }
    }
}
static void object_path(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path) {
    const uint32_t root = ir->operands.access.root;
    switch (ir->operands.access.category) {
        case ROOT_LOCAL:
            if (root >= e->module->local_count) hlsl_error(e, "Hlsl local root identity is invalid");
            hlsl_format(e, "l%u", root);
            break;
        case ROOT_POINTER:
            hlsl_error(e, "HLSL pointer dereference is not implemented");
            break;
        case ROOT_VALUE: hlsl_value(e, root); break;
        case ROOT_WORKGROUP:
            if (root >= e->module->workgroup_count) hlsl_error(e, "Hlsl workgroup root identity is invalid");
            hlsl_format(e, "w%u", root);
            break;
        default: hlsl_error(e, "Hlsl object root category has no implemented semantics"); break;
    }
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        if (step->kind == ACCESS_MEMBER) hlsl_format(e, ".m%u", step->index);
        else if (step->kind == ACCESS_COMPONENT) hlsl_format(e, "[%u]", step->index);
        else {
            const TypeRecord *input = path_input(e, step);
            if (input == NULL) return;
            if (input->kind == TYPE_ARRAY) hlsl_text(e, ".elements");
            hlsl_text(e, "[uint(");
            hlsl_value(e, step->index);
            hlsl_text(e, ")]");
        }
    }
}
void hlsl_access(HlslEmitter *e, const IrInstruction *ir) {
    HlslPath path = {0};
    if (!collect_path(e, ir->operands.access.path, &path)) return;
    const bool store = ir->opcode == IR_ACCESS_STORE;
    const ResourceRecord *resource = resource_root(e, ir);
    hlsl_line(e, "{");
    ++e->indent;
    if (resource != NULL && !hlsl_entry_resource(e, ir->operands.access.root)) {
        hlsl_error(e, "HLSL resource access is absent from the selected static entry closure");
        return;
    }
    if (resource != NULL && resource->kind == DOE_COMPILER_STORAGE_BUFFER) {
        hlsl_indent(e);
        hlsl_format(e, "uint bytes = doe_r%u_bytes();\n", ir->operands.access.root);
    }
    if (!store) {
        hlsl_indent(e);
        hlsl_value(e, ir->result);
        hlsl_text(e, " = ");
        hlsl_zero(e, ir->type);
        hlsl_text(e, ";\n");
    }
    hlsl_indent(e);
    hlsl_text(e, "if (");
    bounds(e, ir, &path);
    hlsl_text(e, ") {\n");
    ++e->indent;
    hlsl_indent(e);
    if (resource != NULL) {
        hlsl_text(e, "uint offset = ");
        byte_offset(e, &path);
        hlsl_text(e, ";\n");
        if (resource->kind == DOE_COMPILER_STORAGE_BUFFER) {
            hlsl_indent(e);
            hlsl_format(e, "if (offset <= bytes && %" PRIu64 "u <= bytes - offset) {\n",
                        type_size(e->module, ir->type));
            ++e->indent;
        }
        hlsl_indent(e);
        if (store) {
            if (resource->kind != DOE_COMPILER_STORAGE_BUFFER || resource->access != DOE_COMPILER_READ_WRITE) {
                hlsl_error(e, "HLSL store targets a read-only resource");
                return;
            }
            hlsl_format(e, "doe_r%u_store%u(offset, ", ir->operands.access.root, ir->type);
            hlsl_value(e, ir->operands.access.value);
            hlsl_text(e, ");\n");
        } else {
            hlsl_value(e, ir->result);
            hlsl_format(e, " = doe_r%u_load%u(offset);\n", ir->operands.access.root, ir->type);
        }
        if (resource->kind == DOE_COMPILER_STORAGE_BUFFER) {
            --e->indent;
            hlsl_line(e, "}");
        }
    } else {
        if (store) object_path(e, ir, &path);
        else hlsl_value(e, ir->result);
        hlsl_text(e, " = ");
        if (store) hlsl_value(e, ir->operands.access.value);
        else object_path(e, ir, &path);
        hlsl_text(e, ";\n");
    }
    --e->indent;
    hlsl_line(e, "}");
    --e->indent;
    hlsl_line(e, "}");
}

void hlsl_array_length(HlslEmitter *e, const IrInstruction *ir) {
    const uint32_t index = ir->operands.scalar.binding;
    const ResourceRecord *r = target_runtime_array(e->module, index);
    if (r == NULL || ir->type != IR_U32 || r->tail_offset > UINT32_MAX || r->tail_stride > UINT32_MAX) {
        hlsl_error(e, "HLSL arrayLength requires a bounded storage runtime array");
        return;
    }
    hlsl_format(e, "(doe_r%u_bytes() >= %" PRIu64 "u ? (doe_r%u_bytes() - %" PRIu64
        "u) / %" PRIu64 "u : 0u)", index, r->tail_offset, index, r->tail_offset, r->tail_stride);
}
