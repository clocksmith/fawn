/* SPDX-License-Identifier: MIT
 * Metal graphics attributes over the one shared typed stage interface. */
#include "private.h"
#include "../../src/override.h"
#include "../stage_view.h"

static const StageEntry *stage_entry(MetalEmitter *e, uint32_t index) {
    const char *error = NULL;
    const StageEntry *entry = target_stage_entry(e->source, e->module, index, &error);
    if (error != NULL) metal_error(e, error);
    return entry;
}
void metal_graphics_context(MetalEmitter *e) {
    for (uint32_t i = 0u; i < e->module->entry_count; ++i) {
        if (e->selected_entry != NO_ID && e->selected_entry != i) continue;
        const uint32_t function = e->module->entries[i].function;
        if (function >= e->module->function_count) {
            metal_error(e, "Metal graphics entry function is invalid");
            return;
        }
        const FunctionRecord *f = &e->module->functions[function];
        if ((f->flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) == 0u) continue;
        const StageEntry *entry = stage_entry(e, i);
        if (entry == NULL) return;
        for (uint32_t j = 0u; j < entry->formal_count; ++j) {
            const uint32_t formal = entry->formal_first + j;
            metal_text(e, "    ");
            metal_type(e, e->module->formals[formal].type.resolved.type);
            metal_format(e, " stage%u;\n", formal);
        }
    }
}
void metal_stage_input(MetalEmitter *e, const IrInstruction *ir) {
    for (uint32_t i = 0u; i < e->module->entry_count; ++i) {
        if (e->module->entries[i].function != e->current_function) continue;
        const StageEntry *entry = stage_entry(e, i);
        const uint32_t formal = ir->operands.scalar.binding;
        if (entry == NULL) return;
        if (formal < entry->formal_first || formal - entry->formal_first >= entry->formal_count ||
            ir->type != e->module->formals[formal].type.resolved.type) break;
        metal_format(e, "ctx.stage%u", formal);
        return;
    }
    metal_error(e, "Metal stage input has inconsistent function, formal or type identity");
}
static const char *builtin(uint32_t value) {
    switch (value) {
        case DOE_COMPILER_BUILTIN_POSITION: return "position";
        case DOE_COMPILER_BUILTIN_VERTEX_INDEX: return "vertex_id";
        case DOE_COMPILER_BUILTIN_INSTANCE_INDEX: return "instance_id";
        case DOE_COMPILER_BUILTIN_FRONT_FACING: return "front_facing";
        case DOE_COMPILER_BUILTIN_FRAG_DEPTH: return "depth(any)";
        case DOE_COMPILER_BUILTIN_SAMPLE_INDEX: return "sample_id";
        case DOE_COMPILER_BUILTIN_SAMPLE_MASK: return "sample_mask";
        case DOE_COMPILER_BUILTIN_SUBGROUP_SIZE: return "threads_per_simdgroup";
        case DOE_COMPILER_BUILTIN_SUBGROUP_INVOCATION_ID: return "thread_index_in_simdgroup";
        default: return NULL;
    }
}
static void attributes(MetalEmitter *e, const StageInterface *leaf, bool vertex) {
    if (leaf->location == NO_ID) {
        const char *name = builtin(leaf->builtin);
        if (name == NULL) metal_error(e, "Metal stage builtin has no implemented attribute");
        else metal_format(e, " [[%s]]", name);
        return;
    }
    if (vertex && leaf->direction == DOE_COMPILER_INTERFACE_INPUT)
        metal_format(e, " [[attribute(%u)]]", leaf->location);
    else if (!vertex && leaf->direction == DOE_COMPILER_INTERFACE_OUTPUT)
        metal_format(e, " [[color(%u)]]", leaf->location);
    else {
        metal_format(e, " [[user(locn%u)]]", leaf->location);
        if (leaf->interpolation == DOE_COMPILER_INTERPOLATION_FLAT) metal_text(e, " [[flat]]");
        else {
            const char *sampling = leaf->sampling == DOE_COMPILER_SAMPLING_CENTROID ? "centroid" :
                leaf->sampling == DOE_COMPILER_SAMPLING_SAMPLE ? "sample" : "center";
            metal_format(e, " [[%s_%sperspective]]", sampling,
                leaf->interpolation == DOE_COMPILER_INTERPOLATION_LINEAR ? "no_" : "");
        }
    }
}
/* Vertex builtins are individual parameters, not vertex attribute-buffer fields.
 * Using the same rule for fragments leaves stage_in solely for location inputs. */
static uint32_t sample_mask(const MetalEmitter *e) {
    return e->source->specialization != NULL ? e->source->specialization->metal_sample_mask : UINT32_MAX;
}
static bool generated_mask(MetalEmitter *e, const StageEntry *entry, bool vertex) {
    if (vertex || sample_mask(e) == UINT32_MAX) return false;
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction == DOE_COMPILER_INTERFACE_OUTPUT && leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK)
            return false;
    }
    return true;
}
static uint32_t interface_type(MetalEmitter *e, uint32_t index, const StageEntry *entry,
        uint32_t direction, bool vertex) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction == direction && (direction == DOE_COMPILER_INTERFACE_OUTPUT || leaf->location != NO_ID)) ++count;
    }
    const bool coverage = direction == DOE_COMPILER_INTERFACE_OUTPUT && generated_mask(e, entry, vertex);
    if (coverage) ++count;
    if (count == 0u) return 0u;
    metal_format(e, "struct DoeStage%u_%u {\n", index, direction);
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction != direction || (direction == DOE_COMPILER_INTERFACE_INPUT && leaf->location == NO_ID)) continue;
        metal_text(e, "    ");
        metal_type(e, leaf->type);
        metal_format(e, " field%u", i);
        attributes(e, leaf, vertex);
        metal_text(e, ";\n");
    }
    if (coverage) metal_text(e, "    uint fixed_sample_mask [[sample_mask]];\n");
    metal_text(e, "};\n");
    return count;
}
void metal_graphics_wrapper(MetalEmitter *e, uint32_t index) {
    const StageEntry *entry = stage_entry(e, index);
    if (entry == NULL) return;
    const EntryRecord *root = &e->module->entries[index];
    const FunctionRecord *f = &e->module->functions[root->function];
    const bool vertex = (f->flags & FUNCTION_VERTEX) != 0u;
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->entry != index || (leaf->direction != DOE_COMPILER_INTERFACE_INPUT &&
            leaf->direction != DOE_COMPILER_INTERFACE_OUTPUT)) {
            metal_error(e, "Metal graphics leaf has inconsistent entry identity");
            return;
        }
        if ((leaf->flags & DOE_COMPILER_INTERFACE_INVARIANT) != 0u) {
            metal_error(e, "Metal invariant output requires a qualified preserve-invariance compilation policy");
            return;
        }
    }
    const uint32_t inputs = interface_type(e, index, entry, DOE_COMPILER_INTERFACE_INPUT, vertex);
    const uint32_t outputs = interface_type(e, index, entry, DOE_COMPILER_INTERFACE_OUTPUT, vertex);
    metal_text(e, "[[host_name(\"");
    metal_entry_name(e, f->name);
    metal_format(e, "\")]] %s ", vertex ? "vertex" : "fragment");
    if (outputs != 0u) metal_format(e, "DoeStage%u_2 ", index);
    else metal_text(e, "void ");
    metal_format(e, "doe_entry%u(", index);
    bool comma = false;
    metal_resource_parameters(e, root, &comma);
    if (inputs != 0u) {
        if (comma) metal_text(e, ", ");
        metal_format(e, "DoeStage%u_1 input [[stage_in]]", index);
        comma = true;
    }
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction != DOE_COMPILER_INTERFACE_INPUT || leaf->location != NO_ID) continue;
        if (comma) metal_text(e, ", ");
        metal_type(e, leaf->type);
        metal_format(e, " builtin%u", i);
        attributes(e, leaf, vertex);
        comma = true;
    }
    metal_text(e, ") {\n");
    e->indent = 1u;
    metal_resource_context(e, root);
    metal_private_initialization(e, root);
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction != DOE_COMPILER_INTERFACE_INPUT) continue;
        metal_format(e, "    ctx.stage%u", leaf->formal);
        if (leaf->member != NO_ID) metal_format(e, ".m%u", leaf->member);
        metal_format(e, " = %s%u;\n", leaf->location == NO_ID ? "builtin" : "input.field", i);
    }
    metal_text(e, "    ");
    if (f->result_type != IR_VOID) {
        metal_type(e, f->result_type);
        metal_text(e, " value = ");
    }
    metal_format(e, "doe_fn%u(ctx", root->function);
    metal_handle_arguments(e, root->function, false);
    metal_text(e, ");\n");
    if (outputs != 0u) {
        metal_format(e, "    DoeStage%u_2 output{};\n", index);
        for (uint32_t i = 0u; i < entry->interface_count; ++i) {
            const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
            if (leaf->direction != DOE_COMPILER_INTERFACE_OUTPUT) continue;
            metal_format(e, "    output.field%u = value", i);
            if (leaf->member != NO_ID) metal_format(e, ".m%u", leaf->member);
            if (!vertex && leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK && sample_mask(e) != UINT32_MAX)
                metal_format(e, " & 0x%08xu", sample_mask(e));
            metal_text(e, ";\n");
        }
        if (generated_mask(e, entry, vertex))
            metal_format(e, "    output.fixed_sample_mask = 0x%08xu;\n", sample_mask(e));
        metal_text(e, "    return output;\n");
    }
    e->indent = 0u;
    metal_text(e, "}\n");
}
