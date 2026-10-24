/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../ir_view.h"
#include <inttypes.h>

typedef struct {
    uint32_t steps[DOE_METAL_MAX_DEPTH];
    uint32_t count;
} MetalPath;
static bool collect_path(MetalEmitter *e, uint32_t path, MetalPath *out) {
    if (e->failed) return false;
    const char *error = target_access_path(e->module, path, out->steps,
        DOE_METAL_MAX_DEPTH, &out->count);
    if (error != NULL) metal_error(e, error);
    return error == NULL;
}
static const TypeRecord *path_input(MetalEmitter *e, const AccessStep *step) {
    if (step->input_type < 7u || step->input_type - 7u >= e->module->type_count) {
        metal_error(e, "Metal access requires an aggregate input type");
        return NULL;
    }
    return &e->module->types[step->input_type - 7u];
}
static const ResourceRecord *resource_root(MetalEmitter *e, const IrInstruction *ir) {
    if (ir->operands.access.category != ROOT_RESOURCE) return NULL;
    if (ir->operands.access.root >= e->module->resource_count) {
        metal_error(e, "Metal resource root identity is invalid");
        return NULL;
    }
    return &e->module->resources[ir->operands.access.root];
}
static void bounds(MetalEmitter *e, const IrInstruction *ir, const MetalPath *path) {
    metal_text(e, "true");
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        if (step->kind == ACCESS_MEMBER || step->kind == ACCESS_COMPONENT) continue;
        const TypeRecord *input = path_input(e, step);
        if (input == NULL) return;
        if (step->kind == ACCESS_INDEX_I32) {
            metal_text(e, " && (");
            metal_value(e, step->index);
            metal_text(e, " >= 0)");
        } else if (step->kind != ACCESS_INDEX_U32) {
            metal_error(e, "Metal dynamic access kind is unsupported");
            return;
        }
        metal_text(e, " && (ulong(uint(");
        metal_value(e, step->index);
        metal_text(e, ")) < ");
        if (input->kind == TYPE_RUNTIME_ARRAY) {
            const ResourceRecord *r = resource_root(e, ir);
            if (r == NULL || r->tail_stride == 0u) {
                metal_error(e, "Metal runtime array has no valid sized resource root");
                return;
            }
            if (e->metadata != NULL) {
                const uint32_t resource = ir->operands.access.root;
                metal_format(e, "(ulong(ctx.r%u_bytes) >= %" PRIu64 "ul ? "
                    "(ulong(ctx.r%u_bytes) - %" PRIu64 "ul) / %" PRIu64 "ul : 0ul)",
                    resource, r->tail_offset, resource, r->tail_offset, r->tail_stride);
            } else {
                uint32_t slot;
                if (!doe_metal_binding_slot(r->group, r->binding, &slot)) {
                    metal_error(e, "Metal runtime array has no valid sized resource root"); return;
                }
                metal_format(e, "(ulong(ctx.sizes[%u]) >= %" PRIu64 "ul ? "
                    "(ulong(ctx.sizes[%u]) - %" PRIu64 "ul) / %" PRIu64 "ul : 0ul)",
                    slot, r->tail_offset, slot, r->tail_offset, r->tail_stride);
            }
        } else metal_format(e, "%uul", input->count);
        metal_text(e, ")");
    }
}
static void byte_offset(MetalEmitter *e, const MetalPath *path) {
    metal_text(e, "0ul");
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        const TypeRecord *input = path_input(e, step);
        if (input == NULL) return;
        if (step->kind == ACCESS_MEMBER) {
            if (input->kind != TYPE_STRUCT || step->index >= input->count ||
                input->element >= e->module->member_count || step->index >= e->module->member_count - input->element) {
                metal_error(e, "Metal member access identity is invalid");
                return;
            }
            metal_format(e, " + %" PRIu64 "ul", e->module->members[input->element + step->index].offset);
        } else if (step->kind == ACCESS_COMPONENT) {
            metal_format(e, " + %uul", step->index * 4u);
        } else {
            const uint64_t stride = input->kind == TYPE_VECTOR ? 4u : input->state.layout.stride;
            metal_text(e, " + ulong(uint(");
            metal_value(e, step->index);
            metal_format(e, ")) * %" PRIu64 "ul", stride);
        }
    }
}
static void object_path(MetalEmitter *e, const IrInstruction *ir, const MetalPath *path) {
    const uint32_t root = ir->operands.access.root;
    switch (ir->operands.access.category) {
        case ROOT_LOCAL:
            if (root >= e->module->local_count) metal_error(e, "Metal local root identity is invalid");
            metal_format(e, "l%u", root);
            break;
        case ROOT_POINTER:
            metal_error(e, "METAL pointer dereference is not implemented");
            break;
        case ROOT_VALUE: metal_value(e, root); break;
        case ROOT_WORKGROUP:
            if (root >= e->module->workgroup_count) metal_error(e, "Metal workgroup root identity is invalid");
            metal_format(e, "(*ctx.w%u)", root);
            break;
        default: metal_error(e, "Metal object root category has no implemented semantics"); break;
    }
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        if (step->kind == ACCESS_MEMBER) metal_format(e, ".m%u", step->index);
        else if (step->kind == ACCESS_COMPONENT) metal_format(e, "[%u]", step->index);
        else {
            metal_text(e, "[uint(");
            metal_value(e, step->index);
            metal_text(e, ")]");
        }
    }
}
static TypeId atomic_scalar(MetalEmitter *e, const IrInstruction *ir, const MetalPath *path,
                            const ResourceRecord *resource) {
    TypeId type;
    if (resource != NULL && resource->kind == DOE_COMPILER_STORAGE_BUFFER &&
        resource->access == DOE_COMPILER_READ_WRITE && metal_selected_resource(e, ir->operands.access.root))
        type = resource->type;
    else if (ir->operands.access.category == ROOT_WORKGROUP &&
             ir->operands.access.root < e->module->workgroup_count &&
             metal_selected_workgroup(e, ir->operands.access.root))
        type = e->module->workgroups[ir->operands.access.root].type;
    else {
        metal_error(e, "Metal atomics require selected writable storage or workgroup memory");
        return IR_VOID;
    }
    if (path->count) type = e->module->accesses[path->steps[0]].output_type;
    if (type < 7u || type - 7u >= e->module->type_count || e->module->types[type - 7u].kind != TYPE_ATOMIC ||
        (e->module->types[type - 7u].element != IR_I32 && e->module->types[type - 7u].element != IR_U32)) {
        metal_error(e, "Metal atomic path does not resolve an i32/u32 atomic");
        return IR_VOID;
    }
    const uint32_t count = ir->opcode == IR_ATOMIC_LOAD ? 0u : ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE ? 2u : 1u;
    if (ir->operands.access.value > e->module->operand_count ||
        count > e->module->operand_count - ir->operands.access.value) {
        metal_error(e, "Metal atomic typed operand span is invalid");
        return IR_VOID;
    }
    return e->module->types[type - 7u].element;
}
static void atomic_input(MetalEmitter *e, const IrInstruction *ir, uint32_t index) {
    metal_value(e, e->module->typed_operands[ir->operands.access.value + index]);
}
static void atomic_operation(MetalEmitter *e, const IrInstruction *ir, const MetalPath *path,
                             const ResourceRecord *resource, TypeId scalar) {
    if (resource != NULL) {
        metal_indent(e);
        metal_text(e, "ulong offset = ");
        byte_offset(e, path);
        metal_text(e, ";\n");
        metal_indent(e);
        metal_text(e, "ulong bytes = ");
        if ((resource->flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0u) {
            if (e->metadata != NULL) metal_format(e, "ulong(ctx.r%u_bytes)", ir->operands.access.root);
            else {
                uint32_t slot;
                if (!metal_resource_slot(e, resource, &slot)) return;
                metal_format(e, "ulong(ctx.sizes[%u])", slot);
            }
        } else metal_format(e, "%" PRIu64 "ul", resource->minimum);
        metal_text(e, ";\n");
        metal_line(e, "if (offset % 4ul == 0ul && offset <= bytes && 4ul <= bytes - offset) {");
        ++e->indent;
    }
    metal_indent(e);
    metal_format(e, "%s atomic_%s* target = ", resource != NULL ? "device" : "threadgroup",
                 scalar == IR_I32 ? "int" : "uint");
    if (resource != NULL) metal_format(e, "reinterpret_cast<device atomic_%s*>(ctx.r%u + offset)",
                                      scalar == IR_I32 ? "int" : "uint", ir->operands.access.root);
    else {
        metal_text(e, "&(");
        object_path(e, ir, path);
        metal_text(e, ")");
    }
    metal_text(e, ";\n");
    if (ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE) {
        metal_indent(e);
        metal_type(e, scalar);
        metal_text(e, " comparison = ");
        atomic_input(e, ir, 0u);
        metal_text(e, ";\n");
        metal_indent(e);
        metal_type(e, scalar);
        metal_text(e, " expected = comparison;\n");
        /* Metal exposes weak exchange. Retry only a spurious failure whose
         * observed value still equals comparison, yielding permitted strong
         * WGSL behavior without reevaluating either ordered operand. */
        metal_line(e, "do {");
        ++e->indent;
        metal_indent(e);
        metal_value(e, ir->result);
        metal_text(e, ".m1 = atomic_compare_exchange_weak_explicit(target, &expected, ");
        atomic_input(e, ir, 1u);
        metal_text(e, ", memory_order_relaxed, memory_order_relaxed);\n");
        --e->indent;
        metal_indent(e);
        metal_text(e, "} while (!");
        metal_value(e, ir->result);
        metal_text(e, ".m1 && expected == comparison);\n");
        metal_indent(e);
        metal_value(e, ir->result);
        metal_text(e, ".m0 = expected;\n");
    } else {
        const char *operation = NULL;
        switch ((uint32_t)ir->opcode) {
            case IR_ATOMIC_LOAD: operation = "load"; break;
            case IR_ATOMIC_STORE: operation = "store"; break;
            case IR_ATOMIC_ADD: operation = "fetch_add"; break;
            case IR_ATOMIC_SUBTRACT: operation = "fetch_sub"; break;
            case IR_ATOMIC_MIN: operation = "fetch_min"; break;
            case IR_ATOMIC_MAX: operation = "fetch_max"; break;
            case IR_ATOMIC_AND: operation = "fetch_and"; break;
            case IR_ATOMIC_OR: operation = "fetch_or"; break;
            case IR_ATOMIC_XOR: operation = "fetch_xor"; break;
            case IR_ATOMIC_EXCHANGE: operation = "exchange"; break;
            default: metal_error(e, "Metal atomic opcode has no native mapping"); return;
        }
        metal_indent(e);
        if (ir->opcode != IR_ATOMIC_STORE) {
            metal_value(e, ir->result);
            metal_text(e, " = ");
        }
        metal_format(e, "atomic_%s_explicit(target", operation);
        if (ir->opcode != IR_ATOMIC_LOAD) {
            metal_text(e, ", ");
            atomic_input(e, ir, 0u);
        }
        metal_text(e, ", memory_order_relaxed);\n");
    }
    if (resource != NULL) {
        --e->indent;
        metal_line(e, "}");
    }
}
void metal_access(MetalEmitter *e, const IrInstruction *ir) {
    MetalPath path = {0};
    if (!collect_path(e, ir->operands.access.path, &path)) return;
    const bool atomic = atomic_opcode(ir->opcode);
    const bool store = ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_ATOMIC_STORE;
    const ResourceRecord *resource = resource_root(e, ir);
    const TypeId scalar = atomic ? atomic_scalar(e, ir, &path, resource) : IR_VOID;
    if (e->failed) return;
    if (!store) {
        metal_indent(e);
        metal_value(e, ir->result);
        metal_text(e, " = ");
        metal_zero(e, ir->type);
        metal_text(e, ";\n");
    }
    metal_indent(e);
    metal_text(e, "if (");
    bounds(e, ir, &path);
    metal_text(e, ") {\n");
    ++e->indent;
    metal_indent(e);
    if (atomic) atomic_operation(e, ir, &path, resource, scalar);
    else if (resource != NULL) {
        if (store) {
            if (resource->kind != DOE_COMPILER_STORAGE_BUFFER || resource->access != DOE_COMPILER_READ_WRITE) {
                metal_error(e, "Metal store targets a read-only resource");
                return;
            }
            metal_format(e, "doe_store%u(", ir->type);
        } else {
            metal_value(e, ir->result);
            metal_format(e, " = doe_%s%u(", resource->kind == DOE_COMPILER_UNIFORM_BUFFER ? "uniform" : "load", ir->type);
        }
        metal_format(e, "ctx.r%u + (", ir->operands.access.root);
        byte_offset(e, &path);
        metal_text(e, ")");
        if (store) {
            metal_text(e, ", ");
            metal_value(e, ir->operands.access.value);
        }
        metal_text(e, ");\n");
    } else if (ir->operands.access.category == ROOT_WORKGROUP) {
        if (store) metal_format(e, "doe_wstore%u(&(", ir->type);
        else {
            metal_value(e, ir->result);
            metal_format(e, " = doe_wload%u(&(", ir->type);
        }
        object_path(e, ir, &path);
        metal_text(e, ")");
        if (store) {
            metal_text(e, ", ");
            metal_value(e, ir->operands.access.value);
        }
        metal_text(e, ");\n");
    } else {
        if (store) object_path(e, ir, &path);
        else metal_value(e, ir->result);
        metal_text(e, " = ");
        if (store) metal_value(e, ir->operands.access.value);
        else object_path(e, ir, &path);
        metal_text(e, ";\n");
    }
    --e->indent;
    metal_line(e, "}");
}

void metal_array_length(MetalEmitter *e, const IrInstruction *ir) {
    const ResourceRecord *r = target_runtime_array(e->module, ir->operands.scalar.binding);
    if (r == NULL || ir->type != IR_U32) {
        metal_error(e, "Metal arrayLength requires a sized storage runtime array");
        return;
    }
    if (e->metadata != NULL) {
        const uint32_t resource = ir->operands.scalar.binding;
        metal_format(e, "uint(ulong(ctx.r%u_bytes) >= %" PRIu64 "ul ? "
            "(ulong(ctx.r%u_bytes) - %" PRIu64 "ul) / %" PRIu64 "ul : 0ul)",
            resource, r->tail_offset, resource, r->tail_offset, r->tail_stride);
    } else {
        uint32_t slot;
        if (!metal_resource_slot(e, r, &slot)) return;
        metal_format(e, "uint(ulong(ctx.sizes[%u]) >= %" PRIu64 "ul ? "
            "(ulong(ctx.sizes[%u]) - %" PRIu64 "ul) / %" PRIu64 "ul : 0ul)",
            slot, r->tail_offset, slot, r->tail_offset, r->tail_stride);
    }
}
