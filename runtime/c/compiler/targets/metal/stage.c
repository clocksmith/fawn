/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../closure.h"

static bool entry_reference(const CompilerWorkspace *c, uint32_t first, uint32_t count, uint32_t item) {
    if (first > c->entry_reference_count || count > c->entry_reference_count - first) return false;
    for (uint32_t i = 0u; i < count; ++i) {
        if (c->entry_references[first + i] == item) return true;
    }
    return false;
}
bool metal_entry_resource(const CompilerWorkspace *c, const EntryRecord *entry, uint32_t item) {
    return entry_reference(c, entry->state.resolved.resource_first, entry->state.resolved.resource_count, item);
}
bool metal_entry_workgroup(const CompilerWorkspace *c, const EntryRecord *entry, uint32_t item) {
    return entry_reference(c, entry->state.resolved.workgroup_first, entry->state.resolved.workgroup_count, item);
}
bool metal_entry_sizes(const CompilerWorkspace *c, const EntryRecord *entry) {
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        if (metal_entry_resource(c, entry, i) && (c->resources[i].flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0u)
            return true;
    }
    return false;
}
bool metal_function_used(MetalEmitter *e, uint32_t wanted) {
    return target_function_used(&e->closure, wanted);
}
bool metal_selected_resource(MetalEmitter *e, uint32_t resource) {
    if (e->metadata != NULL) return metal_dense_selected_resource(e, resource);
    for (uint32_t i = 0u; i < e->module->entry_count; ++i) {
        if ((e->selected_entry == NO_ID || e->selected_entry == i) &&
            metal_entry_resource(e->module, &e->module->entries[i], resource)) return true;
    }
    return false;
}
bool metal_selected_workgroup(MetalEmitter *e, uint32_t workgroup) {
    for (uint32_t i = 0u; i < e->module->entry_count; ++i) {
        if ((e->selected_entry == NO_ID || e->selected_entry == i) &&
            metal_entry_workgroup(e->module, &e->module->entries[i], workgroup)) return true;
    }
    return false;
}
void metal_private_initialization(MetalEmitter *e, const EntryRecord *entry) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[i];
        if (!metal_entry_workgroup(c, entry, i) || root->state.resolved.address_space != MODULE_PRIVATE) continue;
        metal_indent(e);
        metal_type(e, root->type);
        metal_format(e, " w%u = ", i);
        if (root->state.resolved.initializer == NO_ID) metal_zero(e, root->type);
        else {
            const ConstantValue value = c->constant_edges[root->state.resolved.initializer];
            metal_constant(e, value.type, value.bits, (value.flags & CONSTANT_AGGREGATE) != 0u, 0u);
        }
        metal_text(e, ";\n");
        metal_indent(e);
        metal_format(e, "ctx.w%u = &w%u;\n", i, i);
    }
}
static void context_type(MetalEmitter *e) {
    metal_text(e, "struct DoeContext {\n");
    for (uint32_t i = 0u; i < e->module->resource_count; ++i) {
        const bool selected = metal_selected_resource(e, i);
        if (selected && !type_handle(e->module, e->module->resources[i].type))
            metal_format(e, "    %s uchar* r%u;\n", metal_buffer_qualifier(&e->module->resources[i]), i);
        if (e->metadata != NULL && selected &&
            (e->module->resources[i].flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0u)
            metal_format(e, "    uint r%u_bytes;\n", i);
    }
    for (uint32_t i = 0u; i < e->module->workgroup_count; ++i) {
        if (!metal_selected_workgroup(e, i)) continue;
        if (e->module->workgroups[i].state.resolved.address_space == MODULE_IMMEDIATE) {
            metal_format(e,"    constant uchar* w%u;\n",i);
            continue;
        }
        metal_text(e, e->module->workgroups[i].state.resolved.address_space == MODULE_PRIVATE ? "    thread " : "    threadgroup ");
        metal_type(e, e->module->workgroups[i].type);
        metal_format(e, "* w%u;\n", i);
    }
    metal_text(e, "    constant uint* sizes;\n");
    for (unsigned i = 0u; i <= INPUT_SUBGROUP_ID; ++i) {
        metal_format(e, "    %s input%u;\n", input_scalar((InputKind)i) ? "uint" : "uint3", i);
    }
    metal_graphics_context(e);
    metal_text(e, "};\n");
}
static void signature(MetalEmitter *e, uint32_t index) {
    const CompilerWorkspace *c = e->module;
    const FunctionRecord *f = &c->functions[index];
    metal_text(e, "inline ");
    metal_type(e, f->result_type);
    metal_format(e, " doe_fn%u(thread DoeContext& ctx", index);
    metal_handle_arguments(e, index, true);
    if ((f->flags & FUNCTION_ENTRY) == 0u) {
        for (uint32_t i = 0u; i < f->formal_count; ++i) {
            const uint32_t formal = f->formal_start + i;
            if (formal >= c->formal_count) {
                metal_error(e, "Metal formal parameter range is invalid");
                return;
            }
            metal_text(e, ", ");
            metal_type(e, c->formals[formal].type.resolved.type);
            metal_format(e, " v%u", c->limits.instructions + 1u + formal);
        }
    }
    metal_text(e, ")");
}
static void function_body(MetalEmitter *e, uint32_t index) {
    const CompilerWorkspace *c = e->module;
    const FunctionRecord *f = &c->functions[index];
    if (f->instruction_start > f->instruction_end || f->instruction_end > c->instruction_count ||
        f->local_start > c->local_count || f->local_count > c->local_count - f->local_start) {
        metal_error(e, "Metal function IR or local range is invalid");
        return;
    }
    signature(e, index);
    metal_text(e, " {\n");
    e->indent = 1u;
    for (uint32_t i = f->local_start; i < f->local_start + f->local_count; ++i) {
        metal_indent(e);
        metal_type(e, c->locals[i].type);
        metal_format(e, " l%u = ", i);
        metal_zero(e, c->locals[i].type);
        metal_text(e, ";\n");
    }
    for (uint32_t i = f->instruction_start; i < f->instruction_end; ++i) {
        const IrInstruction *ir = &c->instructions[i];
        if (ir->result == 0u || ir->opcode == IR_HANDLE) continue;
        if (ir->result != i + 1u) {
            metal_error(e, "Metal value identity differs from its IR instruction slot");
            return;
        }
        metal_indent(e);
        metal_type(e, ir->type);
        metal_format(e, " v%u = ", ir->result);
        metal_zero(e, ir->type);
        metal_text(e, ";\n");
    }
    e->current_function = index;
    e->function_end = f->instruction_end;
    uint32_t cursor = f->instruction_start;
    metal_block(e, &cursor, 0u, NO_ID, false);
    if (cursor != f->instruction_end) metal_error(e, "Metal function has an unmatched structured boundary");
    e->indent = 0u;
    metal_text(e, "}\n");
}
void metal_entry_name(MetalEmitter *e, NameSpan name) {
    if (name.length == 0u || name.length > 63u || name.offset > e->source->length ||
        name.length > e->source->length - name.offset) {
        metal_error(e, "Metal entry name span is invalid");
        return;
    }
    for (uint32_t i = 0u; i < name.length; ++i) {
        const char character = e->source->source[name.offset + i];
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (i != 0u && character >= '0' && character <= '9') || character == '_')) {
            metal_error(e, "Metal entry name is not an ASCII shader identifier");
            return;
        }
        const char text[] = {character, '\0'};
        metal_text(e, text);
    }
}
static bool entry_admit(MetalEmitter *e, const EntryRecord *entry) {
    const CompilerWorkspace *c = e->module;
    if (entry->function >= c->function_count ||
        entry->state.resolved.resource_first > c->entry_reference_count ||
        entry->state.resolved.resource_count > c->entry_reference_count - entry->state.resolved.resource_first ||
        entry->state.resolved.workgroup_first > c->entry_reference_count ||
        entry->state.resolved.workgroup_count > c->entry_reference_count - entry->state.resolved.workgroup_first) {
        metal_error(e, "Metal entry closure range is invalid");
        return false;
    }
    e->offset = c->functions[entry->function].name.offset;
    const uint32_t supported_inputs = (1u << (INPUT_SUBGROUP_ID + 1u)) - 1u;
    if ((entry->source_input_mask & ~supported_inputs) != 0u) {
        metal_error(e, "Metal builtin input has no implemented lowering");
        return false;
    }
    if ((c->functions[entry->function].flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) == 0u) {
        uint64_t invocations = 1u;
        for (unsigned i = 0u; i < 3u; ++i) {
            const uint32_t dimension = entry->state.resolved.workgroup_size[i];
            if (dimension == 0u || dimension > 1024u || invocations > 1024u / dimension) {
                metal_error(e, "Metal baseline supports at most 1024 workgroup invocations");
                return false;
            }
            invocations *= dimension;
        }
        if (entry->state.resolved.workgroup_storage_bytes > 16u * 1024u) {
            metal_error(e, "Metal baseline workgroup storage exceeds 16 KiB");
            return false;
        }
    }
    if (e->metadata != NULL) return true; /* Typed dense plan already validated every row. */
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        if (!metal_entry_resource(c, entry, i)) continue;
        const ResourceRecord *r = &c->resources[i];
        uint32_t slot;
        if (!metal_resource_slot(e, r, &slot)) return false;
        if (!type_handle(c,r->type) && target_immediate_root(c,entry) != NO_ID && slot == DOE_COMPILER_METAL_IMMEDIATE_SLOT) {
            metal_error(e,"Metal buffer binding collides with immediate data slot29"); return false;
        }
        for (uint32_t j = 0u; j < i; ++j) {
            if (metal_entry_resource(c, entry, j) && c->resources[j].group == r->group &&
                c->resources[j].binding == r->binding) {
                metal_error(e, "Metal entry resources have colliding binding identities");
                return false;
            }
        }
    }
    return true;
}
void metal_resource_parameters(MetalEmitter *e, const EntryRecord *entry, bool *comma) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        if (!metal_entry_resource(c, entry, i)) continue;
        if (*comma) metal_text(e, ", ");
        metal_resource_parameter(e, i, true);
        *comma = true;
    }
    if (target_immediate_root(c,entry) != NO_ID) {
        if (*comma) metal_text(e,", ");
        metal_format(e,"constant uchar* doe_immediate [[buffer(%u)]]",DOE_COMPILER_METAL_IMMEDIATE_SLOT);
        *comma = true;
    }
    if (metal_entry_sizes(c, entry)) {
        e->needs_sizes = true;
        if (*comma) metal_text(e, ", ");
        metal_format(e, "constant uint* sizes [[buffer(%u)]]", DOE_METAL_SIZES_SLOT);
        *comma = true;
    }
}
void metal_resource_context(MetalEmitter *e, const EntryRecord *entry) {
    const CompilerWorkspace *c = e->module;
    metal_line(e, "DoeContext ctx{};");
    const uint32_t immediate = target_immediate_root(c,entry);
    if (immediate != NO_ID) metal_format(e,"    ctx.w%u = doe_immediate;\n",immediate);
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        if (metal_entry_resource(c, entry, i) && !type_handle(c, c->resources[i].type)) {
            metal_indent(e);
            metal_format(e, "ctx.r%u = r%u;\n", i, i);
            if (e->metadata != NULL && (c->resources[i].flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) != 0u) {
                const DoeCompilerMetalBindingV12 *binding = metal_dense_binding(e, e->current_entry, i);
                if (binding == NULL) return;
                metal_indent(e);
                metal_format(e, "ctx.r%u_bytes = sizes[%u];\n", i, binding->size_index);
            }
        }
    }
    if (metal_entry_sizes(c, entry)) metal_line(e, "ctx.sizes = sizes;");
}
static void entry_wrapper(MetalEmitter *e, uint32_t index) {
    static const char *const attributes[] = {"thread_position_in_grid", "thread_position_in_threadgroup",
        "threadgroup_position_in_grid", "threadgroups_per_grid", "thread_index_in_threadgroup",
        "threads_per_simdgroup", "thread_index_in_simdgroup", "simdgroups_per_threadgroup", "simdgroup_index_in_threadgroup"};
    const CompilerWorkspace *c = e->module;
    const EntryRecord *entry = &c->entries[index];
    e->current_entry = index;
    if (!entry_admit(e, entry)) return;
    if ((c->functions[entry->function].flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) != 0u) {
        metal_graphics_wrapper(e, index);
        return;
    }
    const bool sizes = metal_entry_sizes(c, entry);
    e->needs_sizes = e->needs_sizes || sizes;
    uint32_t input_mask = entry->source_input_mask;
    if (entry->state.resolved.workgroup_storage_bytes != 0u) input_mask |= 1u << INPUT_LOCAL_INDEX;
    metal_text(e, "[[host_name(\"");
    metal_entry_name(e, c->functions[entry->function].name);
    metal_format(e, "\")]] kernel void doe_entry%u(", index);
    bool comma = false;
    metal_resource_parameters(e, entry, &comma);
    for (unsigned i = 0u; i <= INPUT_SUBGROUP_ID; ++i) {
        if ((input_mask & (1u << i)) == 0u) continue;
        if (comma) metal_text(e, ", ");
        metal_format(e, "%s input%u [[%s]]", input_scalar((InputKind)i) ? "uint" : "uint3", i, attributes[i]);
        comma = true;
    }
    metal_text(e, ") {\n");
    e->indent = 1u;
    metal_resource_context(e, entry);
    metal_private_initialization(e, entry);
    for (unsigned i = 0u; i <= INPUT_SUBGROUP_ID; ++i) {
        if ((input_mask & (1u << i)) != 0u) {
            metal_indent(e);
            metal_format(e, "ctx.input%u = input%u;\n", i, i);
        }
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        if (!metal_entry_workgroup(c, entry, i) || c->workgroups[i].state.resolved.address_space != MODULE_WORKGROUP) continue;
        metal_indent(e);
        metal_text(e, "threadgroup ");
        metal_type(e, c->workgroups[i].type);
        metal_format(e, " w%u;\n", i);
        metal_indent(e);
        metal_format(e, "ctx.w%u = &w%u;\n", i, i);
    }
    if (entry->state.resolved.workgroup_storage_bytes != 0u) {
        metal_line(e, "if (input4 == 0u) {");
        ++e->indent;
        for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
            if (!metal_entry_workgroup(c, entry, i) || c->workgroups[i].state.resolved.address_space != MODULE_WORKGROUP) continue;
            metal_indent(e);
            const TypeId type = c->workgroups[i].type;
            if ((type_flags(c, type) & TYPE_ATOMIC_CONTENT) != 0u)
                metal_format(e, "doe_wzero%u(&w%u);\n", type, i);
            else {
                metal_format(e, "doe_wstore%u(&w%u, ", type, i);
                metal_zero(e, type);
                metal_text(e, ");\n");
            }
        }
        --e->indent;
        metal_line(e, "}");
        metal_line(e, "threadgroup_barrier(mem_flags::mem_threadgroup);");
    }
    metal_indent(e);
    metal_format(e, "doe_fn%u(ctx", entry->function);
    metal_handle_arguments(e, entry->function, false);
    metal_text(e, ");\n");
    e->indent = 0u;
    metal_text(e, "}\n");
}
void metal_functions(MetalEmitter *e) {
    context_type(e);
    for (uint32_t i = 0u; i < e->module->function_count && !e->failed; ++i) {
        if (!metal_function_used(e, i)) continue;
        signature(e, i);
        metal_text(e, ";\n");
    }
    for (uint32_t i = 0u; i < e->module->function_count && !e->failed; ++i) {
        if (metal_function_used(e, i)) function_body(e, i);
    }
    for (uint32_t i = 0u; i < e->module->entry_count && !e->failed; ++i) {
        if (e->selected_entry == NO_ID || e->selected_entry == i) entry_wrapper(e, i);
    }
}
