/* SPDX-License-Identifier: MIT */
#include "override.h"
#include <string.h>

static void select_syntax(Source *s) {
    CompilerWorkspace *c = s->compiler;
    SpecializationContext *context = s->specialization;
    const uint32_t old_entry = context->selected;
    const uint32_t old_function = context->all_entries ? NO_ID : c->entries[old_entry].function;
    StageWorkspace *stages = s->stages;
    const StageEntry stage = context->all_entries ? (StageEntry){0} : stages->entries[old_entry];
    uint32_t functions = 0u;
    for (uint32_t i = 0u; i < c->function_count; ++i) {
        if (context->functions[i] == 0u) {
            context->functions[i] = NO_ID;
            continue;
        }
        context->functions[i] = functions;
        c->functions[functions++] = c->functions[i];
    }
    c->function_count = functions;
    if (context->all_entries) {
        for (uint32_t i = 0u; i < c->entry_count; ++i)
            c->entries[i].function = context->functions[c->entries[i].function];
    } else {
        EntryRecord entry = c->entries[old_entry];
        entry.function = context->functions[old_function];
        c->functions[entry.function].entry_index = 0u;
        memmove(c->entry_inputs, c->entry_inputs + entry.input_first, (size_t)entry.input_count * sizeof(*c->entry_inputs));
        c->entry_input_count = entry.input_count;
        entry.input_first = 0u;
        c->entries[0] = entry;
        c->entry_count = 1u;
        stages->entries[0] = stage;
    }
    uint32_t attributes = 0u;
    for (uint32_t i = 0u; i < stages->attribute_count; ++i) {
        InterfaceAttribute attribute = stages->attributes[i];
        if (attribute.owner == STAGE_ATTRIBUTE_RESULT) {
            if (context->functions[attribute.index] == NO_ID ||
                (!context->all_entries && attribute.index != old_function)) continue;
            attribute.index = context->functions[attribute.index];
        } else if (!context->all_entries && attribute.owner == STAGE_ATTRIBUTE_PARAMETER &&
            (attribute.index < stage.formal_first || attribute.index - stage.formal_first >= stage.formal_count)) continue;
        stages->attributes[attributes++] = attribute;
    }
    stages->attribute_count = attributes;
    uint32_t workgroups = 0u;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i)
        if (context->workgroups[i] != 0u) c->workgroups[workgroups++] = c->workgroups[i];
    c->workgroup_count = workgroups;
    if (!context->all_entries && old_entry != 0u) c->alias = c->alias_source = (NameSpan){0};
    context->selected = context->all_entries ? NO_ID : 0u;
}
bool override_reparse(Source *s) {
    SpecializationContext *context = s->specialization;
    CompilerWorkspace *c = s->compiler;
    const CompilerLimits limits = c->limits;
    const uint32_t module_flags = c->module_flags;
    compiler_job_bind(c, c, &limits);
    c->module_flags = module_flags;
    stage_bind(s, c, &context->capacities->target.stages, context->stage_base);
    context->specializing = true;
    context->count = 0u;
    if (!lex_source(s, c) || !parse_tokens(s, c)) return false;
    select_syntax(s);
    for (uint32_t i = 0u; i < c->expression_count; ++i) c->expression_references[i] = (SourceReference){SOURCE_NONE, NO_ID};
    for (uint32_t i = 0u; i < c->statement_count; ++i) c->statement_references[i] = (SourceReference){SOURCE_NONE, NO_ID};
    return validate_syntax(s, c);
}
