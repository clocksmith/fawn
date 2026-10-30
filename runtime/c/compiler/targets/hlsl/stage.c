/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../entry_name.h"
#include "../closure.h"
#include <string.h>

static bool reference(HlslEmitter *e, uint32_t first, uint32_t count, uint32_t item) {
    const CompilerWorkspace *c = e->module;
    if (first > c->entry_reference_count || count > c->entry_reference_count - first) {
        hlsl_error(e, "HLSL entry closure reference range is invalid");
        return false;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if (c->entry_references[first + i] == item) return true;
    }
    return false;
}
bool hlsl_entry_resource(HlslEmitter *e, uint32_t item) {
    const EntryRecord *entry = &e->module->entries[e->entry];
    return reference(e, entry->state.resolved.resource_first, entry->state.resolved.resource_count, item);
}
bool hlsl_entry_workgroup(HlslEmitter *e, uint32_t item) {
    const EntryRecord *entry = &e->module->entries[e->entry];
    return reference(e, entry->state.resolved.workgroup_first, entry->state.resolved.workgroup_count, item);
}
bool hlsl_function_used(HlslEmitter *e, uint32_t wanted) {
    return target_function_used(&e->closure, wanted);
}
static void signature(HlslEmitter *e, uint32_t index) {
    const CompilerWorkspace *c = e->module;
    const FunctionRecord *f = &c->functions[index];
    hlsl_type(e, f->result_type);
    hlsl_format(e, " doe_fn%u(inout DoeContext ctx", index);
    if ((f->flags & FUNCTION_ENTRY) == 0u) {
        if (f->formal_start > c->formal_count || f->formal_count > c->formal_count - f->formal_start) {
            hlsl_error(e, "HLSL formal range is invalid");
            return;
        }
        for (uint32_t i = 0u; i < f->formal_count; ++i) {
            const uint32_t formal = f->formal_start + i;
            hlsl_text(e, ", ");
            if (type_pointer(c, c->formals[formal].type.resolved.type)) hlsl_text(e, "inout ");
            hlsl_type(e, c->formals[formal].type.resolved.type);
            hlsl_format(e, " v%u", c->limits.instructions + 1u + formal);
        }
    }
    hlsl_text(e, ")");
}
static void function_body(HlslEmitter *e, uint32_t index) {
    const CompilerWorkspace *c = e->module;
    const FunctionRecord *f = &c->functions[index];
    if (f->instruction_start > f->instruction_end || f->instruction_end > c->instruction_count ||
        f->local_start > c->local_count || f->local_count > c->local_count - f->local_start) {
        hlsl_error(e, "HLSL function IR or local range is invalid");
        return;
    }
    signature(e, index);
    hlsl_text(e, " {\n");
    e->indent = 1u;
    for (uint32_t i = f->local_start; i < f->local_start + f->local_count; ++i) {
        hlsl_indent(e);
        hlsl_type(e, c->locals[i].type);
        hlsl_format(e, " l%u = ", i);
        hlsl_zero(e, c->locals[i].type);
        hlsl_text(e, ";\n");
    }
    for (uint32_t i = f->instruction_start; i < f->instruction_end; ++i) {
        const IrInstruction *ir = &c->instructions[i];
        if (ir->result == 0u || ir->opcode == IR_HANDLE || ir->opcode == IR_ADDRESS ||
            hlsl_inline_constant(ir)) continue;
        if (ir->result != i + 1u) {
            hlsl_error(e, "HLSL value identity differs from its IR slot");
            return;
        }
        hlsl_indent(e);
        if (type_leaf(c, ir->type) == IR_F32) hlsl_text(e, "precise ");
        hlsl_type(e, ir->type);
        hlsl_format(e, " v%u = ", ir->result);
        hlsl_zero(e, ir->type);
        hlsl_text(e, ";\n");
    }
    e->function_end = f->instruction_end;
    uint32_t cursor = f->instruction_start;
    hlsl_block(e, &cursor, 0u, NO_ID, false);
    if (cursor != f->instruction_end) hlsl_error(e, "HLSL function has an unmatched structured boundary");
    e->indent = 0u;
    hlsl_text(e, "}\n");
}
static bool entry_private_name(HlslEmitter *e, NameSpan name) {
    static const char *const prefixes[] = {"doe_", "Doe"};
    if (name.offset > e->source->length || name.length > e->source->length - name.offset) return true;
    const char *text = e->source->source + name.offset;
    for (unsigned i = 0u; i < 2u; ++i) {
        unsigned j = 0u;
        while (prefixes[i][j] != '\0' && j < name.length && prefixes[i][j] == text[j]) ++j;
        if (prefixes[i][j] == '\0') return true;
    }
    if (name.length >= 2u && (text[0] == 'r' || text[0] == 'w') && text[1] >= '0' && text[1] <= '9') {
        unsigned i = 2u;
        while (i < name.length && text[i] >= '0' && text[i] <= '9') ++i;
        if (i == name.length) return true;
        if (name.length - i == 5u && text[i] == '_' && text[i + 1u] == 'd' &&
            text[i + 2u] == 'a' && text[i + 3u] == 't' && text[i + 4u] == 'a') return true;
    }
    return false;
}
static bool entry_admit(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    const EntryRecord *entry = &c->entries[e->entry];
    if (entry->function >= c->function_count || entry->input_first > c->entry_input_count ||
        entry->input_count > c->entry_input_count - entry->input_first) {
        hlsl_error(e, "HLSL entry function or input range is invalid");
        return false;
    }
    e->offset = c->functions[entry->function].name.offset;
    const bool graphics = (c->functions[entry->function].flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) != 0u;
    if (graphics && !hlsl_graphics_admit(e)) return false;
    if (!target_entry_short_name(e->source, c->functions[entry->function].name) &&
        entry_private_name(e, c->functions[entry->function].name)) {
        hlsl_error(e, "HLSL entry name collides with the private target symbol namespace");
        return false;
    }
    uint64_t invocations = 1u;
    for (unsigned i = 0u; !graphics && i < 3u; ++i) {
        const uint32_t dimension = entry->state.resolved.workgroup_size[i];
        const uint32_t maximum = i == 2u ? 64u : 1024u;
        if (dimension == 0u || dimension > maximum || invocations > 1024u / dimension) {
            hlsl_error(e, "HLSL compute shape exceeds Shader Model 6.0 limits");
            return false;
        }
        invocations *= dimension;
    }
    if (entry->state.resolved.workgroup_storage_bytes > 32768u) {
        hlsl_error(e, "HLSL workgroup storage exceeds 32 KiB");
        return false;
    }
    const bool group_waves = (entry->source_input_mask & ((1u << INPUT_NUM_SUBGROUPS) | (1u << INPUT_SUBGROUP_ID))) != 0u;
    if (group_waves && entry->state.resolved.workgroup_storage_bytes > 32768u - DOE_COMPILER_HLSL_WAVE_COUNTER_BYTES) {
        hlsl_error(e, "HLSL wave enumeration requires four additional shared bytes");
        return false;
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        if (!hlsl_entry_resource(e, i)) continue;
        for (uint32_t j = 0u; j < i; ++j) {
            if (hlsl_entry_resource(e, j) && c->resources[i].group == c->resources[j].group &&
                c->resources[i].binding == c->resources[j].binding) {
                hlsl_error(e, "HLSL selected entry has duplicate resource binding identity");
                return false;
            }
        }
    }
    return !e->failed;
}
void hlsl_entry_name(HlslEmitter *e, uint32_t function) {
    if (function >= e->module->function_count) {
        hlsl_error(e, "entry function identity is invalid");
        return;
    }
    const NameSpan name = e->module->functions[function].name;
    if (name.length == 0u || name.offset > e->source->length ||
        name.length > e->source->length - name.offset) {
        hlsl_error(e, "HLSL entry name span is invalid");
        return;
    }
    if (!target_entry_ascii(e->source, name)) {
        hlsl_error(e, "HLSL entry name is not an ASCII shader identifier");
        return;
    }
    /* Generated compile-time controls must never expand a legal WGSL entry
     * identifier. Private lowercase prefixes are already rejected by entry_admit,
     * so these distinct target-only spellings cannot alias another user entry.
     * This same owner also emits the public actual-entry metadata. */
    static const struct { const char *source, *target; size_t length; } controls[] = {
        {DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE, "doe_hlsl_projected_entry",
            sizeof(DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE) - 1u},
        {"DOE_HLSL_ENTRY", "doe_hlsl_bundle_entry", sizeof("DOE_HLSL_ENTRY") - 1u}
    };
    for (size_t i = 0u; i < sizeof(controls) / sizeof(*controls); ++i) {
        if (name.length == controls[i].length &&
            memcmp(e->source->source + name.offset, controls[i].source, name.length) == 0) {
            hlsl_text(e, controls[i].target);
            return;
        }
    }
    /* The reserved target prefix also remaps short source collisions. Exact
     * module function identity makes the actual names injective without hashes. */
    if (target_entry_short_name(e->source, name)) {
        hlsl_format(e, "doe_long_entry_%u", function);
        return;
    }
    for (uint32_t i = 0u; i < name.length; ++i) {
        const char character = e->source->source[name.offset + i];
        const char text[] = {character, '\0'};
        hlsl_text(e, text);
    }
}
static void entry_wrapper(HlslEmitter *e) {
    static const char *const semantics[] = {"SV_DispatchThreadID", "SV_GroupThreadID", "SV_GroupID",
        NULL, "SV_GroupIndex"};
    const CompilerWorkspace *c = e->module;
    const EntryRecord *entry = &c->entries[e->entry];
    uint32_t mask = entry->source_input_mask;
    const bool group_waves = (mask & ((1u << INPUT_NUM_SUBGROUPS) | (1u << INPUT_SUBGROUP_ID))) != 0u;
    if (entry->state.resolved.workgroup_storage_bytes != 0u || group_waves) mask |= 1u << INPUT_LOCAL_INDEX;
    hlsl_format(e, "[numthreads(%u, %u, %u)]\nvoid ", entry->state.resolved.workgroup_size[0],
                entry->state.resolved.workgroup_size[1], entry->state.resolved.workgroup_size[2]);
    hlsl_entry_name(e, entry->function);
    hlsl_text(e, "(");
    bool comma = false;
    for (unsigned i = 0u; i <= INPUT_LOCAL_INDEX; ++i) {
        if ((mask & (1u << i)) == 0u || i == INPUT_NUM_WORKGROUPS) continue;
        if (comma) hlsl_text(e, ", ");
        hlsl_format(e, "%s input%u : %s", i == INPUT_LOCAL_INDEX ? "uint" : "uint3", i, semantics[i]);
        comma = true;
    }
    hlsl_text(e, ") {\n");
    e->indent = 1u;
    hlsl_line(e, "DoeContext ctx = (DoeContext)0;");
    hlsl_private_initialization(e);
    for (unsigned i = 0u; i <= INPUT_SUBGROUP_INVOCATION; ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        hlsl_indent(e);
        if (i == INPUT_NUM_WORKGROUPS) hlsl_format(e, "ctx.input%u = doe_num_workgroups;\n", i);
        else if (i == INPUT_SUBGROUP_SIZE || i == INPUT_SUBGROUP_INVOCATION) {
            e->uses_subgroups = true;
            hlsl_format(e, "ctx.input%u = %s();\n", i,
                        i == INPUT_SUBGROUP_SIZE ? "WaveGetLaneCount" : "WaveGetLaneIndex");
        } else hlsl_format(e, "ctx.input%u = input%u;\n", i, i);
    }
    if (group_waves) {
        e->uses_wave_counter = true;
        e->uses_subgroups = true;
        hlsl_line(e, "if (input4 == 0u) doe_wave_count = 0u;");
        hlsl_line(e, "GroupMemoryBarrierWithGroupSync();");
        hlsl_line(e, "uint doe_wave_id = 0u;");
        hlsl_line(e, "if (WaveIsFirstLane()) InterlockedAdd(doe_wave_count, 1u, doe_wave_id);");
        hlsl_format(e, "    ctx.input%u = WaveReadLaneFirst(doe_wave_id);\n", INPUT_SUBGROUP_ID);
        hlsl_line(e, "GroupMemoryBarrierWithGroupSync();");
        hlsl_format(e, "    ctx.input%u = doe_wave_count;\n", INPUT_NUM_SUBGROUPS);
    }
    if (entry->state.resolved.workgroup_storage_bytes != 0u) {
        hlsl_line(e, "if (input4 == 0u) {");
        ++e->indent;
        for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
            if (!hlsl_entry_workgroup(e, i) || c->workgroups[i].state.resolved.address_space != MODULE_WORKGROUP) continue;
            hlsl_indent(e);
            hlsl_format(e, "w%u = ", i);
            hlsl_zero(e, c->workgroups[i].type);
            hlsl_text(e, ";\n");
        }
        --e->indent;
        hlsl_line(e, "}");
        hlsl_line(e, "GroupMemoryBarrierWithGroupSync();");
    }
    hlsl_indent(e);
    hlsl_format(e, "doe_fn%u(ctx);\n", entry->function);
    e->indent = 0u;
    hlsl_text(e, "}\n");
}
/* Entry inputs a context must carry: those the wrapper sources plus any input
 * a reachable function reads. Unread builtin fields are not declared. */
static uint32_t context_inputs(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    const EntryRecord *entry = &c->entries[e->entry];
    uint32_t mask = entry->source_input_mask;
    const bool group_waves = (mask & ((1u << INPUT_NUM_SUBGROUPS) | (1u << INPUT_SUBGROUP_ID))) != 0u;
    if (entry->state.resolved.workgroup_storage_bytes != 0u || group_waves) mask |= 1u << INPUT_LOCAL_INDEX;
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        if (!hlsl_function_used(e, f)) continue;
        const FunctionRecord *function = &c->functions[f];
        for (uint32_t i = function->instruction_start; i < function->instruction_end && i < c->instruction_count; ++i) {
            const IrInstruction *ir = &c->instructions[i];
            if (ir->opcode == IR_INPUT && ir->operands.scalar.binding < c->entry_input_count)
                mask |= 1u << (unsigned)c->entry_inputs[ir->operands.scalar.binding].kind;
        }
    }
    return mask;
}
void hlsl_private_initialization(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[i];
        if (!hlsl_entry_workgroup(e, i) || root->state.resolved.address_space != MODULE_PRIVATE ||
            root->state.resolved.initializer == NO_ID) continue;
        const ConstantValue value = c->constant_edges[root->state.resolved.initializer];
        hlsl_indent(e);
        hlsl_format(e, "ctx.w%u = ", i);
        hlsl_constant(e, value.type, value.bits, (value.flags & CONSTANT_AGGREGATE) != 0u);
        hlsl_text(e, ";\n");
    }
}
void hlsl_functions(HlslEmitter *e) {
    if (!entry_admit(e)) return;
    const CompilerWorkspace *c = e->module;
    const EntryRecord *entry = &c->entries[e->entry];
    if ((entry->source_input_mask & (1u << INPUT_NUM_WORKGROUPS)) != 0u) {
        e->needs_dispatch = true;
        hlsl_format(e, "cbuffer DoeDispatchInfo : register(b%u, space%u) {\n"
            "    uint3 doe_num_workgroups;\n    uint _doe_num_workgroups_pad;\n};\n",
            DOE_HLSL_DISPATCH_SLOT, DOE_HLSL_DISPATCH_SPACE);
    }
    if ((entry->source_input_mask & ((1u << INPUT_NUM_SUBGROUPS) | (1u << INPUT_SUBGROUP_ID))) != 0u)
        hlsl_text(e, "groupshared uint doe_wave_count;\n");
    hlsl_text(e, "struct DoeContext {\n");
    const uint32_t inputs = context_inputs(e);
    uint32_t fields = 0u;
    for (unsigned i = 0u; i <= INPUT_SUBGROUP_ID; ++i)
        if ((inputs & (1u << i)) != 0u) {
            hlsl_format(e, "    %s input%u;\n", input_scalar((InputKind)i) ? "uint" : "uint3", i);
            ++fields;
        }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        if (!hlsl_entry_workgroup(e, i) || c->workgroups[i].state.resolved.address_space != MODULE_PRIVATE) continue;
        hlsl_text(e, "    ");
        hlsl_type(e, c->workgroups[i].type);
        hlsl_format(e, " w%u;\n", i);
        ++fields;
    }
    /* Keep the context a nonempty aggregate for every HLSL compiler. */
    if (!fields) hlsl_text(e, "    uint doe_unused;\n");
    hlsl_graphics_context(e);
    hlsl_text(e, "};\n");
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        if (!hlsl_entry_workgroup(e, i) || c->workgroups[i].state.resolved.address_space != MODULE_WORKGROUP) continue;
        hlsl_text(e, "groupshared ");
        hlsl_type(e, c->workgroups[i].type);
        hlsl_format(e, " w%u;\n", i);
    }
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (uint32_t i = 0u; i < c->function_count && !e->failed; ++i) {
            if (!hlsl_function_used(e, i)) continue;
            if (pass == 0u) {
                signature(e, i);
                hlsl_text(e, ";\n");
            } else function_body(e, i);
        }
    }
    if ((c->functions[entry->function].flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) != 0u)
        hlsl_graphics_wrapper(e);
    else entry_wrapper(e);
}
