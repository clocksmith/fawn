/* SPDX-License-Identifier: MIT
 * Typed stage IO adapter. Shared stage validation owns WGSL rules; this owner
 * maps the validated leaves to HLSL semantics without replaying source syntax. */
#include "private.h"
#include "../stage_view.h"

static const StageEntry *stage_entry(HlslEmitter *e) {
    const char *error = NULL;
    const StageEntry *entry = target_stage_entry(e->source, e->module, e->entry, &error);
    if (error != NULL) hlsl_error(e, error);
    return entry;
}
static bool graphics(HlslEmitter *e) {
    const uint32_t function = e->module->entries[e->entry].function;
    return (e->module->functions[function].flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) != 0u;
}
bool hlsl_graphics_admit(HlslEmitter *e) {
    const StageEntry *entry = stage_entry(e);
    if (entry == NULL) return false;
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->entry != e->entry || (leaf->direction != DOE_COMPILER_INTERFACE_INPUT &&
            leaf->direction != DOE_COMPILER_INTERFACE_OUTPUT)) {
            hlsl_error(e, "HLSL graphics leaf has inconsistent entry identity");
            return false;
        }
        if ((leaf->flags & DOE_COMPILER_INTERFACE_INVARIANT) != 0u) {
            /* 'precise' alone does not establish cross-program WGSL invariant
             * output semantics without a qualified DXC compilation policy. */
            hlsl_error(e, "HLSL invariant stage output requires a qualified precise compilation policy");
            return false;
        }
    }
    return true;
}
void hlsl_graphics_context(HlslEmitter *e) {
    if (!graphics(e)) return;
    const StageEntry *entry = stage_entry(e);
    if (entry == NULL) return;
    for (uint32_t i = 0u; i < entry->formal_count; ++i) {
        const uint32_t formal = entry->formal_first + i;
        hlsl_text(e, "    ");
        hlsl_type(e, e->module->formals[formal].type.resolved.type);
        hlsl_format(e, " stage%u;\n", formal);
    }
}
void hlsl_stage_input(HlslEmitter *e, const IrInstruction *ir) {
    const StageEntry *entry = stage_entry(e);
    const uint32_t formal = ir->operands.scalar.binding;
    if (entry == NULL) return;
    if (formal < entry->formal_first || formal - entry->formal_first >= entry->formal_count ||
        ir->type != e->module->formals[formal].type.resolved.type) {
        hlsl_error(e, "HLSL stage input has inconsistent formal identity or type");
        return;
    }
    hlsl_format(e, "ctx.stage%u", formal);
}
static void semantic(HlslEmitter *e, const StageInterface *leaf) {
    const FunctionRecord *function = &e->module->functions[e->module->entries[e->entry].function];
    if (leaf->location != NO_ID) {
        hlsl_format(e, "%s%u", (function->flags & FUNCTION_FRAGMENT) != 0u &&
                    leaf->direction == DOE_COMPILER_INTERFACE_OUTPUT ? "SV_Target" : "TEXCOORD", leaf->location);
        return;
    }
    switch (leaf->builtin) {
        case DOE_COMPILER_BUILTIN_POSITION: hlsl_text(e, "SV_Position"); break;
        case DOE_COMPILER_BUILTIN_VERTEX_INDEX: hlsl_text(e, "SV_VertexID"); break;
        case DOE_COMPILER_BUILTIN_INSTANCE_INDEX: hlsl_text(e, "SV_InstanceID"); break;
        case DOE_COMPILER_BUILTIN_FRONT_FACING: hlsl_text(e, "SV_IsFrontFace"); break;
        case DOE_COMPILER_BUILTIN_FRAG_DEPTH: hlsl_text(e, "SV_Depth"); break;
        case DOE_COMPILER_BUILTIN_SAMPLE_INDEX: hlsl_text(e, "SV_SampleIndex"); break;
        case DOE_COMPILER_BUILTIN_SAMPLE_MASK: hlsl_text(e, "SV_Coverage"); break;
        default: hlsl_error(e, "HLSL stage builtin has no implemented semantic"); break;
    }
}
static void interpolation(HlslEmitter *e, const StageInterface *leaf) {
    if (leaf->interpolation == DOE_COMPILER_INTERPOLATION_FLAT) {
        hlsl_text(e, "nointerpolation ");
        return;
    }
    if (leaf->interpolation == DOE_COMPILER_INTERPOLATION_LINEAR) hlsl_text(e, "noperspective ");
    else if (leaf->interpolation == DOE_COMPILER_INTERPOLATION_PERSPECTIVE) hlsl_text(e, "linear ");
    if (leaf->sampling == DOE_COMPILER_SAMPLING_CENTROID) hlsl_text(e, "centroid ");
    else if (leaf->sampling == DOE_COMPILER_SAMPLING_SAMPLE) hlsl_text(e, "sample ");
}
static uint32_t interface_type(HlslEmitter *e, const StageEntry *entry, uint32_t direction) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < entry->interface_count; ++i)
        if (e->source->stages->interfaces[entry->interface_first + i].direction == direction) ++count;
    if (count == 0u) return 0u;
    hlsl_format(e, "struct DoeStage%u {\n", direction);
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction != direction) continue;
        hlsl_text(e, "    ");
        interpolation(e, leaf);
        hlsl_type(e, leaf->type);
        hlsl_format(e, " field%u : ", i);
        semantic(e, leaf);
        hlsl_text(e, ";\n");
    }
    hlsl_text(e, "};\n");
    return count;
}
void hlsl_graphics_wrapper(HlslEmitter *e) {
    const StageEntry *entry = stage_entry(e);
    if (entry == NULL) return;
    const FunctionRecord *function = &e->module->functions[e->module->entries[e->entry].function];
    const uint32_t inputs = interface_type(e, entry, DOE_COMPILER_INTERFACE_INPUT);
    const uint32_t outputs = interface_type(e, entry, DOE_COMPILER_INTERFACE_OUTPUT);
    hlsl_text(e, outputs != 0u ? "DoeStage2 " : "void ");
    hlsl_entry_name(e, function->name);
    hlsl_text(e, inputs != 0u ? "(DoeStage1 input) {\n" : "() {\n");
    hlsl_line(e, "    DoeContext ctx = (DoeContext)0;");
    for (uint32_t i = 0u; i < entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
        if (leaf->direction != DOE_COMPILER_INTERFACE_INPUT) continue;
        hlsl_format(e, "    ctx.stage%u", leaf->formal);
        if (leaf->member != NO_ID) hlsl_format(e, ".m%u", leaf->member);
        hlsl_format(e, " = input.field%u;\n", i);
    }
    hlsl_text(e, "    ");
    if (function->result_type != IR_VOID) {
        hlsl_type(e, function->result_type);
        hlsl_text(e, " value = ");
    }
    hlsl_format(e, "doe_fn%u(ctx);\n", e->module->entries[e->entry].function);
    if (outputs != 0u) {
        hlsl_text(e, "    DoeStage2 output = (DoeStage2)0;\n");
        for (uint32_t i = 0u; i < entry->interface_count; ++i) {
            const StageInterface *leaf = &e->source->stages->interfaces[entry->interface_first + i];
            if (leaf->direction != DOE_COMPILER_INTERFACE_OUTPUT) continue;
            hlsl_format(e, "    output.field%u = value", i);
            if (leaf->member != NO_ID) hlsl_format(e, ".m%u", leaf->member);
            hlsl_text(e, ";\n");
        }
        hlsl_text(e, "    return output;\n");
    }
    hlsl_text(e, "}\n");
}
