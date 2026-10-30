/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../ir_view.h"
#include "../../src/stage.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void hlsl_error(HlslEmitter *e, const char *message) {
    if (e->failed) return;
    e->failed = true;
    (void)source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->offset, message);
}
void hlsl_text(HlslEmitter *e, const char *text) {
    if (e->failed) return;
    const size_t count = strlen(text);
    if (count > DOE_HLSL_MAX_OUTPUT - e->position) {
        hlsl_error(e, "Hlsl target text exceeds the one MiB format budget");
        return;
    }
    if (e->output != NULL) {
        if (count > e->capacity - e->position) {
            hlsl_error(e, "Hlsl serialization changed after successful measurement");
            return;
        }
        memcpy(e->output + e->position, text, count);
    }
    e->position += count;
}
void hlsl_format(HlslEmitter *e, const char *format, ...) {
    if (e->failed) return;
    char buffer[192];
    va_list arguments;
    va_start(arguments, format);
    const int count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= sizeof(buffer)) {
        hlsl_error(e, "Hlsl formatting operand exceeds the bounded formatter");
        return;
    }
    hlsl_text(e, buffer);
}
void hlsl_indent(HlslEmitter *e) {
    for (unsigned i = 0u; i < e->indent; ++i) hlsl_text(e, "    ");
}
void hlsl_line(HlslEmitter *e, const char *text) {
    hlsl_indent(e);
    hlsl_text(e, text);
    hlsl_text(e, "\n");
}
TypeId hlsl_value_type(HlslEmitter *e, uint32_t id) {
    TypeId type;
    if (target_value_type(e->module, id, &type)) return type;
    hlsl_error(e, "operand has no typed value identity");
    return IR_VOID;
}
bool hlsl_inline_constant(const IrInstruction *ir) {
    return ir->opcode == IR_CONSTANT && ir->operands.scalar.left == 0u;
}
void hlsl_value(HlslEmitter *e, uint32_t id) {
    (void)hlsl_value_type(e, id);
    hlsl_assignment_use(e, id);
    if (id != 0u && id <= e->module->instruction_count &&
        hlsl_inline_constant(&e->module->instructions[id - 1u])) {
        /* An immutable literal: inlining preserves every value snapshot. */
        const IrInstruction *ir = &e->module->instructions[id - 1u];
        hlsl_constant(e, ir->type, ir->operands.scalar.literal, false);
        return;
    }
    if (id != 0u && id <= e->module->instruction_count &&
        e->module->instructions[id - 1u].opcode == IR_ADDRESS) {
        const IrInstruction *ir = &e->module->instructions[id - 1u];
        if (ir->operands.access.path != NO_ID) {
            hlsl_error(e,"HLSL pointer call requires a validated whole-root reference");
            return;
        }
        if (ir->operands.access.category == ROOT_LOCAL) hlsl_format(e,"l%u",ir->operands.access.root);
        else if (ir->operands.access.category == ROOT_POINTER)
            hlsl_format(e,"v%u",e->module->limits.instructions + 1u + ir->operands.access.root);
        else hlsl_error(e,"HLSL pointer reference has an invalid address space");
        return;
    }
    if (id != 0u && id <= e->module->instruction_count &&
        e->module->instructions[id - 1u].opcode == IR_HANDLE) {
        const IrInstruction *ir = &e->module->instructions[id - 1u];
        hlsl_handle(e, ir);
        hlsl_format(e, "r%u", ir->operands.scalar.binding);
    } else hlsl_format(e, "v%u", id);
}
static void emit_entry(HlslEmitter *e, uint32_t entry) {
    const bool previous_dispatch = e->needs_dispatch, previous_subgroups = e->uses_subgroups;
    e->needs_dispatch = false;
    e->uses_subgroups = false;
    e->uses_native16 = false;
    e->uses_wave_counter = false;
    e->uses_array_views = false;
    e->uses_raw_storage = false;
    e->uses_draw_offsets = false;
    e->uses_storage_sizes = false;
    e->entry = entry;
    e->traversal_left = 4096u * ((uint64_t)e->module->type_count +
        e->module->member_count + e->module->instruction_count + 1u);
    const char *closure_error = NULL;
    if (!target_closure_build(e->module, entry, &e->closure,
            &e->traversal_left, &closure_error)) {
        hlsl_error(e, closure_error);
        return;
    }
    hlsl_types(e);
    hlsl_constants(e);
    hlsl_resources(e);
    hlsl_numeric_helpers(e);
    hlsl_functions(e);
    if (!e->failed && e->metadata.profile_requirements) {
        const uint32_t profile = hlsl_profile(e);
        if (profile > e->shader_model) e->shader_model = profile;
    }
    if (!e->failed && e->metadata.entries != NULL) {
        const FunctionRecord *function = &e->module->functions[e->module->entries[entry].function];
        DoeCompilerHlslEntryV11 *record = &e->metadata.entries[entry];
        *record = (DoeCompilerHlslEntryV11){.entry_index=entry,.stage=stage_kind(function),
            .flags=(e->uses_subgroups ? DOE_COMPILER_HLSL_ENTRY_WAVES : 0u) |
                (e->uses_native16 ? DOE_COMPILER_HLSL_ENTRY_NATIVE16 : 0u) |
                (e->uses_wave_counter ? DOE_COMPILER_HLSL_ENTRY_WAVE_COUNTER : 0u) |
                (e->uses_array_views ? DOE_COMPILER_HLSL_ENTRY_ARRAY_VIEWS : 0u) |
                (e->uses_raw_storage ? DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE : 0u) |
                (e->uses_draw_offsets ? DOE_COMPILER_HLSL_ENTRY_DRAW_OFFSETS : 0u) |
                (e->uses_storage_sizes ? DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES : 0u),
            .dispatch_register=e->needs_dispatch ? DOE_HLSL_DISPATCH_SLOT : NO_ID,
            .dispatch_space=e->needs_dispatch ? DOE_HLSL_DISPATCH_SPACE : NO_ID,
            .dispatch_bytes=e->needs_dispatch ? 16u : 0u};
        HlslEmitter name = {.source=e->source,.module=e->module,.output=record->name,
            .capacity=sizeof(record->name)-1u};
        hlsl_entry_name(&name, e->module->entries[entry].function);
        if (name.failed) e->failed = true;
    }
    e->needs_dispatch = previous_dispatch || e->needs_dispatch;
    e->uses_subgroups = previous_subgroups || e->uses_subgroups;
}
static void emit_module(HlslEmitter *e) {
    hlsl_text(e, "// Experimental Doe typed-IR HLSL target, Shader Model 6.0 baseline; entry flags carry additional requirements.\n");
    if (e->selected_entry != NO_ID) {
        emit_entry(e, e->selected_entry);
        return;
    }
    if (e->module->entry_count == 0u) {
        hlsl_error(e, "HLSL emission requires a compute entry");
        return;
    }
    hlsl_format(e, "#if !defined(DOE_HLSL_ENTRY) || DOE_HLSL_ENTRY < 0 || DOE_HLSL_ENTRY >= %u\n",
                e->module->entry_count);
    hlsl_text(e, "#error Select exactly one entry with DOE_HLSL_ENTRY\n#endif\n");
    for (uint32_t i = 0u; i < e->module->entry_count && !e->failed; ++i) {
        hlsl_format(e, "#if DOE_HLSL_ENTRY == %u\n", i);
        emit_entry(e, i);
        hlsl_text(e, "#endif\n");
    }
}
DoeCompilerStatus doe_hlsl_emit(Source *source, const CompilerWorkspace *module,
                                 uint32_t selected_entry, DoeTargetScratch scratch, char *output,
                                 size_t capacity, DoeHlslResult *result) {
    const int saved_errno = errno;
    if (source == NULL || source->diagnostic == NULL || module == NULL || result == NULL) {
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    *result = (DoeHlslResult){0};
    *source->diagnostic = (DoeCompilerDiagnostic){0};
    if ((output == NULL && capacity != 0u) ||
        (selected_entry != NO_ID && selected_entry >= module->entry_count)) {
        (void)source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "invalid Hlsl output span or entry selection");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    TargetClosure closure;
    if (!target_closure_bind(module, scratch, &closure)) {
        (void)source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "invalid target closure scratch span");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    HlslEmitter measure = {.source = source, .closure = closure, .module = module, .selected_entry = selected_entry};
    emit_module(&measure);
    if (measure.failed) {
        errno = saved_errno;
        return source->diagnostic->status;
    }
    result->required_bytes = measure.position;
    result->entry_count = selected_entry == NO_ID ? module->entry_count : 1u;
    result->needs_dispatch_info = measure.needs_dispatch;
    result->uses_subgroups = measure.uses_subgroups;
    result->shader_model = measure.shader_model;
    if (output == NULL) {
        errno = saved_errno;
        return DOE_COMPILER_OK;
    }
    if (capacity < measure.position) {
        (void)source_error(source, DOE_COMPILER_OUTPUT_TOO_SMALL, source->length,
                           "Hlsl output capacity is smaller than measured source");
        errno = saved_errno;
        return DOE_COMPILER_OUTPUT_TOO_SMALL;
    }
    HlslEmitter writer = {.source = source, .module = module, .output = output,
        .capacity = capacity, .selected_entry = selected_entry, .closure = closure};
    emit_module(&writer);
    if (!writer.failed) result->written_bytes = writer.position;
    errno = saved_errno;
    return source->diagnostic->status;
}

DoeCompilerStatus doe_hlsl_emit_metadata(Source *source, const CompilerWorkspace *module,
    uint32_t selected_entry, DoeTargetScratch scratch, DoeHlslMetadata metadata, DoeHlslResult *result) {
    const int saved_errno = errno;
    if (source == NULL || source->diagnostic == NULL || module == NULL || result == NULL ||
        (module->resource_count != 0u && metadata.bindings == NULL) ||
        (module->entry_count != 0u && metadata.entries == NULL)) {
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    *result = (DoeHlslResult){0};
    TargetClosure closure;
    if ((selected_entry != NO_ID && selected_entry >= module->entry_count) ||
        !target_closure_bind(module, scratch, &closure)) {
        (void)source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "invalid HLSL metadata entry or scratch");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    if (module->resource_count != 0u)
        memset(metadata.bindings, 0, (size_t)module->resource_count * sizeof(*metadata.bindings));
    HlslEmitter measure = {.source=source,.closure=closure,.module=module,
        .selected_entry=selected_entry,.metadata=metadata};
    emit_module(&measure);
    if (!measure.failed) {
        result->required_bytes = measure.position;
        result->entry_count = selected_entry == NO_ID ? module->entry_count : 1u;
        result->needs_dispatch_info = measure.needs_dispatch;
        result->uses_subgroups = measure.uses_subgroups;
        result->shader_model = measure.shader_model;
    }
    errno = saved_errno;
    return source->diagnostic->status;
}
