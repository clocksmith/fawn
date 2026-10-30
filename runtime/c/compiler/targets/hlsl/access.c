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
    if (step->input_type < IR_PRIMITIVE_COUNT || step->input_type - IR_PRIMITIVE_COUNT >= e->module->type_count) {
        hlsl_error(e, "Hlsl access requires an aggregate input type");
        return NULL;
    }
    return &e->module->types[step->input_type - IR_PRIMITIVE_COUNT];
}
static const ResourceRecord *resource_root(HlslEmitter *e, const IrInstruction *ir) {
    if (ir->operands.access.category != ROOT_RESOURCE) return NULL;
    if (ir->operands.access.root >= e->module->resource_count) {
        hlsl_error(e, "Hlsl resource root identity is invalid");
        return NULL;
    }
    return &e->module->resources[ir->operands.access.root];
}
/* Indexed steps each need a guard; member/component steps are statically in range. */
static bool path_guarded(HlslEmitter *e, const HlslPath *path, bool *runtime_out) {
    bool any = false;
    *runtime_out = false;
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        if (step->kind == ACCESS_MEMBER || step->kind == ACCESS_COMPONENT) continue;
        uint32_t extent;
        bool runtime;
        if (path_input(e, step) == NULL) return false;
        if (!target_index_extent(e->module, step->input_type, &extent, &runtime)) {
            hlsl_error(e, "HLSL indexed access has no typed extent");
            return false;
        }
        any = true;
        if (runtime) *runtime_out = true;
    }
    return any;
}
/* Emits the conjunction of every indexed step's guard, joined by " && ". */
static void bounds(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path) {
    bool first = true;
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        if (step->kind == ACCESS_MEMBER || step->kind == ACCESS_COMPONENT) continue;
        const TypeRecord *input = path_input(e, step);
        if (input == NULL) return;
        uint32_t extent;
        bool runtime;
        if (!target_index_extent(e->module, step->input_type, &extent, &runtime)) {
            hlsl_error(e, "HLSL indexed access has no typed extent");
            return;
        }
        if (!first) hlsl_text(e, " && ");
        first = false;
        if (step->kind == ACCESS_INDEX_I32) {
            hlsl_text(e, "(");
            hlsl_value(e, step->index);
            hlsl_text(e, " >= 0) && ");
        } else if (step->kind != ACCESS_INDEX_U32) {
            hlsl_error(e, "Hlsl dynamic access kind is unsupported");
            return;
        }
        if (runtime) {
            (void)ir;
            hlsl_error(e, "HLSL runtime array has no bounded resource root");
            return;
        }
        hlsl_text(e, "(uint(");
        hlsl_value(e, step->index);
        hlsl_format(e, ") < %uu)", extent);
    }
}
/* Largest in-range index of one indexed step of a resource path. A runtime
 * tail's count is (bytes - tail_offset) / stride from the runtime-supplied
 * binding size; a count of zero still yields index zero, whose element begins
 * at or beyond the bound range, where D3D12 view bounds read zero and drop
 * writes. Fixed arrays and vectors use their static extent. */
static void last_index(HlslEmitter *e, const IrInstruction *ir, const AccessStep *step) {
    uint32_t extent;
    bool runtime;
    if (!target_index_extent(e->module, step->input_type, &extent, &runtime)) {
        hlsl_error(e, "HLSL indexed access has no typed extent");
        return;
    }
    if (!runtime) {
        if (extent == 0u) hlsl_error(e, "HLSL indexed access has an empty extent");
        hlsl_format(e, "%uu", extent - 1u);
        return;
    }
    const uint32_t root = ir->operands.access.root;
    const ResourceRecord *r = resource_root(e, ir);
    if (r == NULL || r->tail_stride == 0u || r->tail_stride > UINT32_MAX ||
        r->tail_offset > UINT32_MAX - r->tail_stride) {
        hlsl_error(e, "HLSL runtime array has no bounded resource root");
        return;
    }
    /* max(count, 1) - 1 equals count - 1 for count >= 1 and 0 for count 0;
     * min(bytes, tail) keeps the tail subtraction from wrapping. Same bound as
     * the conditional form, which FXC compiles measurably slower. */
    if (r->tail_offset == 0u)
        hlsl_format(e, "(max(doe_r%u_bytes() / %" PRIu64 "u, 1u) - 1u)", root, r->tail_stride);
    else hlsl_format(e, "(max((doe_r%u_bytes() - min(doe_r%u_bytes(), %" PRIu64 "u)) / %" PRIu64 "u, 1u) - 1u)",
            root, root, r->tail_offset, r->tail_stride);
}
/* Resource paths (ir non-NULL) clamp each index to its last valid element. */
static void byte_offset(HlslEmitter *e, const HlslPath *path, const IrInstruction *ir) {
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
            hlsl_format(e, " + %uu", step->index * (uint32_t)type_size(e->module, input->element));
        } else {
            const uint64_t stride = input->kind == TYPE_VECTOR ? type_size(e->module, input->element) : input->state.layout.stride;
            if (ir != NULL) {
                hlsl_text(e, " + min(uint(");
                hlsl_value(e, step->index);
                hlsl_text(e, "), ");
                last_index(e, ir, step);
                hlsl_format(e, ") * %" PRIu64 "u", stride);
            } else {
                hlsl_text(e, " + uint(");
                hlsl_value(e, step->index);
                hlsl_format(e, ") * %" PRIu64 "u", stride);
            }
        }
    }
}
/* The clamped byte offset of a 4-byte-aligned scalar, divided by four term by
 * term: the same index the helper body forms as (offset + 0u) / 4u, without the
 * offset temporary. Returns false (emitting nothing) when a term is not a whole
 * number of words; that access keeps the byte-offset form. */
static bool word_terms(HlslEmitter *e, const HlslPath *path, bool emit, const IrInstruction *ir) {
    bool any = false;
    for (uint32_t i = path->count; i != 0u && !e->failed; --i) {
        const AccessStep *step = &e->module->accesses[path->steps[i - 1u]];
        const TypeRecord *input = path_input(e, step);
        if (input == NULL) return false;
        uint64_t words = 0u;
        if (step->kind == ACCESS_MEMBER) {
            if (input->kind != TYPE_STRUCT || step->index >= input->count ||
                input->element >= e->module->member_count || step->index >= e->module->member_count - input->element) {
                hlsl_error(e, "Hlsl member access identity is invalid");
                return false;
            }
            const uint64_t offset = e->module->members[input->element + step->index].offset;
            if (offset % 4u != 0u) return false;
            words = offset / 4u;
        } else if (step->kind == ACCESS_COMPONENT) {
            const uint64_t size = type_size(e->module, input->element);
            if (size % 4u != 0u) return false;
            words = (uint64_t)step->index * (size / 4u);
        } else {
            const uint64_t stride = input->kind == TYPE_VECTOR ? type_size(e->module, input->element) : input->state.layout.stride;
            if (stride % 4u != 0u) return false;
            if (emit) {
                hlsl_text(e, any ? " + min(uint(" : "min(uint(");
                hlsl_value(e, step->index);
                hlsl_text(e, "), ");
                last_index(e, ir, step);
                hlsl_text(e, ")");
                if (stride != 4u) hlsl_format(e, " * %" PRIu64 "u", stride / 4u);
            }
            any = true;
            continue;
        }
        if (words == 0u) continue;
        if (emit) hlsl_format(e, any ? " + %" PRIu64 "u" : "%" PRIu64 "u", words);
        any = true;
    }
    if (emit && !any) hlsl_text(e, "0u");
    return !e->failed;
}
/* Word-addressed 32-bit scalar storage/uniform access, written as one statement. */
static bool direct_scalar(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path, bool store) {
    const uint32_t root = ir->operands.access.root;
    const ResourceRecord *resource = resource_root(e, ir);
    if (resource == NULL || !hlsl_inline_scalar(e->module, ir->type) || hlsl_raw_storage(e->module, root) ||
        !word_terms(e, path, false, ir)) return false;
    const bool uniform = resource->kind == DOE_COMPILER_UNIFORM_BUFFER;
    hlsl_indent(e);
    if (!store) {
        hlsl_define(e, ir->result);
        hlsl_value(e, ir->result);
        hlsl_text(e, ir->type == IR_F32 ? " = asfloat(" : ir->type == IR_I32 ? " = asint(" : " = uint(");
    }
    if (uniform) {
        hlsl_format(e, "r%u_data[(", root);
        (void)word_terms(e, path, true, ir);
        hlsl_text(e, ") / 4u][(");
        (void)word_terms(e, path, true, ir);
        hlsl_text(e, ") % 4u]");
    } else {
        hlsl_format(e, "r%u[", root);
        (void)word_terms(e, path, true, ir);
        hlsl_text(e, "]");
    }
    if (store) {
        hlsl_text(e, " = asuint(");
        hlsl_value(e, ir->operands.access.value);
        hlsl_text(e, ");\n");
    } else hlsl_text(e, ");\n");
    return true;
}
static void object_path(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path) {
    const uint32_t root = ir->operands.access.root;
    switch (ir->operands.access.category) {
        case ROOT_LOCAL:
            if (root >= e->module->local_count) hlsl_error(e, "Hlsl local root identity is invalid");
            hlsl_format(e, "l%u", root);
            break;
        case ROOT_POINTER:
            hlsl_format(e, "v%u", e->module->limits.instructions + 1u + root);
            break;
        case ROOT_VALUE: hlsl_value(e, root); break;
        case ROOT_WORKGROUP:
            if (root >= e->module->workgroup_count) hlsl_error(e, "Hlsl workgroup root identity is invalid");
            if (root < e->module->workgroup_count &&
                e->module->workgroups[root].state.resolved.address_space == MODULE_PRIVATE) hlsl_text(e, "ctx.");
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
/* Resource offsets are clamped in range; other roots' enclosing guard proves
 * bounds before this lvalue is formed.
 * Storage and groupshared atomic<i32/u32> are native uint words. No target
 * pointer is fabricated, and ordered operands already name immutable IR values. */
static void atomic_word(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path) {
    if (ir->operands.access.category == ROOT_RESOURCE)
        hlsl_format(e, "r%u[offset / 4u]", ir->operands.access.root);
    else object_path(e, ir, path);
}
static void atomic_begin(HlslEmitter *e, const IrInstruction *ir,
                         const HlslPath *path, const char *operation) {
    if (ir->operands.access.category == ROOT_RESOURCE &&
        hlsl_raw_storage(e->module,ir->operands.access.root))
        hlsl_format(e,"r%u.%s(offset",ir->operands.access.root,operation);
    else {
        hlsl_format(e,"%s(",operation);
        atomic_word(e,ir,path);
    }
}
static void atomic_input(HlslEmitter *e, const IrInstruction *ir, uint32_t index) {
    hlsl_text(e, "asuint(");
    hlsl_value(e, e->module->typed_operands[ir->operands.access.value + index]);
    hlsl_text(e, ")");
}
static void atomic_operation(HlslEmitter *e, const IrInstruction *ir, const HlslPath *path, TypeId scalar) {
    const uint32_t count = ir->opcode == IR_ATOMIC_LOAD ? 0u : ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE ? 2u : 1u;
    if (ir->operands.access.value > e->module->operand_count ||
        count > e->module->operand_count - ir->operands.access.value) {
        hlsl_error(e, "HLSL atomic typed operand range is invalid");
        return;
    }
    hlsl_line(e, "uint old = 0u;");
    if (scalar == IR_I32 && (ir->opcode == IR_ATOMIC_MIN || ir->opcode == IR_ATOMIC_MAX)) {
        /* Raw uint storage must not select unsigned min/max. A strong CAS loop
         * linearizes the signed comparison and preserves the original old word. */
        hlsl_indent(e);
        atomic_begin(e,ir,path,"InterlockedOr");
        hlsl_text(e, ", 0u, old);\n");
        hlsl_line(e, "[allow_uav_condition] while (true) {");
        ++e->indent;
        hlsl_line(e, "uint seen = old;");
        hlsl_indent(e);
        hlsl_text(e, "uint desired = asint(seen)");
        hlsl_text(e, ir->opcode == IR_ATOMIC_MIN ? " < asint(" : " > asint(");
        atomic_input(e, ir, 0u);
        hlsl_text(e, ") ? seen : ");
        atomic_input(e, ir, 0u);
        hlsl_text(e, ";\n");
        hlsl_indent(e);
        atomic_begin(e,ir,path,"InterlockedCompareExchange");
        hlsl_text(e, ", seen, desired, old);\n");
        hlsl_line(e, "if (old == seen) break;");
        --e->indent;
        hlsl_line(e, "}");
    } else {
        const char *operation = NULL;
        switch ((uint32_t)ir->opcode) {
            case IR_ATOMIC_LOAD: case IR_ATOMIC_OR: operation = "InterlockedOr"; break;
            case IR_ATOMIC_STORE: case IR_ATOMIC_EXCHANGE: operation = "InterlockedExchange"; break;
            case IR_ATOMIC_ADD: case IR_ATOMIC_SUBTRACT: operation = "InterlockedAdd"; break;
            case IR_ATOMIC_MAX: operation = "InterlockedMax"; break;
            case IR_ATOMIC_MIN: operation = "InterlockedMin"; break;
            case IR_ATOMIC_AND: operation = "InterlockedAnd"; break;
            case IR_ATOMIC_XOR: operation = "InterlockedXor"; break;
            case IR_ATOMIC_COMPARE_EXCHANGE: operation = "InterlockedCompareExchange"; break;
            default: hlsl_error(e, "HLSL atomic operation has no native mapping"); return;
        }
        hlsl_indent(e);
        atomic_begin(e,ir,path,operation);
        hlsl_text(e, ", ");
        if (ir->opcode == IR_ATOMIC_LOAD) hlsl_text(e, "0u");
        else {
            if (ir->opcode == IR_ATOMIC_SUBTRACT) hlsl_text(e, "0u - ");
            atomic_input(e, ir, 0u);
        }
        if (ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE) {
            hlsl_text(e, ", ");
            atomic_input(e, ir, 1u);
        }
        hlsl_text(e, ", old);\n");
    }
    if (ir->opcode != IR_ATOMIC_STORE) {
        hlsl_indent(e);
        hlsl_value(e, ir->result);
        if (ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE) hlsl_text(e, ".m0");
        hlsl_text(e, scalar == IR_I32 ? " = asint(old);\n" : " = old;\n");
        if (ir->opcode == IR_ATOMIC_COMPARE_EXCHANGE) {
            hlsl_indent(e);
            hlsl_value(e, ir->result);
            hlsl_text(e, ".m1 = old == ");
            atomic_input(e, ir, 0u);
            hlsl_text(e, ";\n");
        }
    }
}
void hlsl_access(HlslEmitter *e, const IrInstruction *ir) {
    HlslPath path = {0};
    if (!collect_path(e, ir->operands.access.path, &path)) return;
    const bool atomic = atomic_opcode(ir->opcode);
    const bool store = ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_ATOMIC_STORE;
    const ResourceRecord *resource = resource_root(e, ir);
    TypeId scalar = IR_VOID;
    if (atomic) {
        TypeId type;
        if (resource != NULL) {
            if (resource->kind != DOE_COMPILER_STORAGE_BUFFER || resource->access != DOE_COMPILER_READ_WRITE) {
                hlsl_error(e, "HLSL atomics require writable storage or workgroup memory");
                return;
            }
            type = resource->type;
        } else if (ir->operands.access.category == ROOT_WORKGROUP &&
                   ir->operands.access.root < e->module->workgroup_count &&
                   hlsl_entry_workgroup(e, ir->operands.access.root)) {
            type = e->module->workgroups[ir->operands.access.root].type;
        } else {
            hlsl_error(e, "HLSL atomic root is absent or unsupported");
            return;
        }
        if (path.count) type = e->module->accesses[path.steps[0]].output_type;
        if (type < IR_PRIMITIVE_COUNT || type - IR_PRIMITIVE_COUNT >= e->module->type_count || e->module->types[type - IR_PRIMITIVE_COUNT].kind != TYPE_ATOMIC ||
            ((scalar = e->module->types[type - IR_PRIMITIVE_COUNT].element) != IR_I32 && scalar != IR_U32)) {
            hlsl_error(e, "HLSL atomic access does not resolve an i32/u32 atomic");
            return;
        }
    }
    if (resource != NULL && !hlsl_entry_resource(e, ir->operands.access.root)) {
        hlsl_error(e, "HLSL resource access is absent from the selected static entry closure");
        return;
    }
    if (!atomic && resource != NULL && store &&
        (resource->kind != DOE_COMPILER_STORAGE_BUFFER || resource->access != DOE_COMPILER_READ_WRITE)) {
        hlsl_error(e, "HLSL store targets a read-only resource");
        return;
    }
    if (!atomic && direct_scalar(e, ir, &path, store)) return;
    hlsl_line(e, "{");
    ++e->indent;
    /* Storage and uniform resources clamp every index to its last valid
     * element (WGSL robustness permits any in-bounds element), so the access
     * executes unconditionally inside the declared extent: a runtime tail is
     * bounded by its binding's size, a fixed path by its type, whose size the
     * binding covers. Other roots keep one guard per access: invalid loads stay
     * zero and invalid stores drop. */
    bool runtime = false;
    const bool guarded = resource == NULL && path_guarded(e, &path, &runtime);
    if (guarded && !store) {
        hlsl_indent(e);
        hlsl_define(e, ir->result); /* The zero load precedes the guard. */
        hlsl_value(e, ir->result);
        hlsl_text(e, " = ");
        hlsl_zero(e, ir->type);
        hlsl_text(e, ";\n");
    }
    if (resource != NULL) {
        hlsl_indent(e);
        hlsl_text(e, "uint offset = ");
        byte_offset(e, &path, ir);
        hlsl_text(e, ";\n");
    }
    if (guarded) {
        hlsl_indent(e);
        hlsl_text(e, "if (");
        bounds(e, ir, &path);
        hlsl_text(e, ") {\n");
        ++e->indent;
    }
    hlsl_indent(e);
    if (resource != NULL) {
        if (atomic) atomic_operation(e, ir, &path, scalar);
        else if (store) {
            if (resource->kind != DOE_COMPILER_STORAGE_BUFFER || resource->access != DOE_COMPILER_READ_WRITE) {
                hlsl_error(e, "HLSL store targets a read-only resource");
                return;
            }
            if (hlsl_inline_scalar(e->module, ir->type)) {
                hlsl_inline_store_open(e, ir->operands.access.root);
                hlsl_value(e, ir->operands.access.value);
                hlsl_inline_store_close(e, ir->operands.access.root);
            } else {
                hlsl_format(e, "doe_r%u_store%u(offset, ", ir->operands.access.root, ir->type);
                hlsl_value(e, ir->operands.access.value);
                hlsl_text(e, ");\n");
            }
        } else if (hlsl_inline_scalar(e->module, ir->type)) {
            hlsl_define(e, ir->result); /* Resource accesses are never guarded. */
            hlsl_value(e, ir->result);
            hlsl_text(e, " = ");
            hlsl_inline_load(e, ir->operands.access.root, ir->type);
            hlsl_text(e, ";\n");
        } else {
            hlsl_define(e, ir->result);
            hlsl_value(e, ir->result);
            hlsl_format(e, " = doe_r%u_load%u(offset);\n", ir->operands.access.root, ir->type);
        }
    } else if (ir->operands.access.category == ROOT_WORKGROUP &&
        e->module->workgroups[ir->operands.access.root].state.resolved.address_space == MODULE_IMMEDIATE) {
        if (store) { hlsl_error(e,"immediate data cannot be written"); return; }
        if (!guarded) hlsl_define(e,ir->result);
        hlsl_value(e,ir->result);
        hlsl_format(e," = doe_r%u_load%u(",e->module->resource_count + ir->operands.access.root,ir->type);
        byte_offset(e,&path,NULL);
        hlsl_text(e,");\n");
    } else if (atomic) atomic_operation(e, ir, &path, scalar);
    else {
        if (store) object_path(e, ir, &path);
        else {
            if (!guarded) hlsl_define(e, ir->result);
            hlsl_value(e, ir->result);
        }
        hlsl_text(e, " = ");
        if (store) hlsl_value(e, ir->operands.access.value);
        else object_path(e, ir, &path);
        hlsl_text(e, ";\n");
    }
    if (guarded) {
        --e->indent;
        hlsl_line(e, "}");
    }
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
    if (r->tail_offset == 0u)
        hlsl_format(e, "(doe_r%u_bytes() / %" PRIu64 "u)", index, r->tail_stride);
    else hlsl_format(e, "(doe_r%u_bytes() >= %" PRIu64 "u ? (doe_r%u_bytes() - %" PRIu64
        "u) / %" PRIu64 "u : 0u)", index, r->tail_offset, index, r->tail_offset, r->tail_stride);
}
/* True when a reachable function of the selected entry reads this storage
 * buffer's binding size: a runtime-tail index or arrayLength. */
bool hlsl_storage_size_used(HlslEmitter *e, uint32_t resource) {
    const CompilerWorkspace *c = e->module;
    if (resource >= c->resource_count || c->resources[resource].kind != DOE_COMPILER_STORAGE_BUFFER ||
        !hlsl_entry_resource(e, resource)) return false;
    for (uint32_t f = 0u; f < c->function_count && !e->failed; ++f) {
        if (!hlsl_function_used(e, f)) continue;
        const FunctionRecord *function = &c->functions[f];
        const char *error = NULL;
        if (function->instruction_end > c->instruction_count || function->instruction_start > function->instruction_end ||
            !target_spend_count(&e->traversal_left, function->instruction_end - function->instruction_start, &error)) {
            hlsl_error(e, error != NULL ? error : "HLSL function instruction range is invalid");
            return false;
        }
        for (uint32_t i = function->instruction_start; i < function->instruction_end; ++i) {
            const IrInstruction *ir = &c->instructions[i];
            if (ir->opcode == IR_ARRAY_LENGTH) {
                if (ir->operands.scalar.binding == resource) return true;
                continue;
            }
            if ((ir->opcode != IR_ACCESS_LOAD && ir->opcode != IR_ACCESS_STORE && !atomic_opcode(ir->opcode)) ||
                ir->operands.access.category != ROOT_RESOURCE || ir->operands.access.root != resource) continue;
            HlslPath path = {0};
            bool runtime = false;
            if (!collect_path(e, ir->operands.access.path, &path)) return false;
            if (path_guarded(e, &path, &runtime) && runtime) return true;
        }
    }
    return false;
}
