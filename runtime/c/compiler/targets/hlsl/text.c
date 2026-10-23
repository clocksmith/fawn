/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
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
    const CompilerWorkspace *c = e->module;
    if (id > c->limits.instructions) {
        const uint32_t formal = id - c->limits.instructions - 1u;
        if (formal < c->formal_count) return c->formals[formal].type.resolved.type;
    } else if (id != 0u && id <= c->instruction_count && c->instructions[id - 1u].result == id) {
        return c->instructions[id - 1u].type;
    }
    hlsl_error(e, "Hlsl operand has no typed value identity");
    return IR_VOID;
}
void hlsl_value(HlslEmitter *e, uint32_t id) {
    (void)hlsl_value_type(e, id);
    if (id != 0u && id <= e->module->instruction_count &&
        e->module->instructions[id - 1u].opcode == IR_HANDLE) {
        const IrInstruction *ir = &e->module->instructions[id - 1u];
        hlsl_handle(e, ir);
        hlsl_format(e, "r%u", ir->operands.scalar.binding);
    } else hlsl_format(e, "v%u", id);
}
static void emit_entry(HlslEmitter *e, uint32_t entry) {
    e->entry = entry;
    e->traversal_left = 4096u * ((uint64_t)e->module->type_count +
        e->module->member_count + e->module->instruction_count + 1u);
    hlsl_types(e);
    hlsl_constants(e);
    hlsl_resources(e);
    hlsl_numeric_helpers(e);
    hlsl_functions(e);
}
static void emit_module(HlslEmitter *e) {
    hlsl_text(e, "// Experimental Doe typed-IR HLSL target, Shader Model 6.0.\n");
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
                                 uint32_t selected_entry, char *output,
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
    HlslEmitter measure = {.source = source, .module = module, .selected_entry = selected_entry};
    emit_module(&measure);
    if (measure.failed) {
        errno = saved_errno;
        return source->diagnostic->status;
    }
    result->required_bytes = measure.position;
    result->entry_count = selected_entry == NO_ID ? module->entry_count : 1u;
    result->needs_dispatch_info = measure.needs_dispatch;
    result->uses_subgroups = measure.uses_subgroups;
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
        .capacity = capacity, .selected_entry = selected_entry};
    emit_module(&writer);
    if (!writer.failed) result->written_bytes = writer.position;
    errno = saved_errno;
    return source->diagnostic->status;
}
