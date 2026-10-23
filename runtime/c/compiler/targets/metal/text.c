/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../ir_view.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void metal_error(MetalEmitter *e, const char *message) {
    if (e->failed) return;
    e->failed = true;
    (void)source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->offset, message);
}
void metal_text(MetalEmitter *e, const char *text) {
    if (e->failed) return;
    const size_t count = strlen(text);
    if (count > DOE_METAL_MAX_OUTPUT - e->position) {
        metal_error(e, "Metal target text exceeds the one MiB format budget");
        return;
    }
    if (e->output != NULL) {
        if (count > e->capacity - e->position) {
            metal_error(e, "Metal serialization changed after successful measurement");
            return;
        }
        memcpy(e->output + e->position, text, count);
    }
    e->position += count;
}
void metal_format(MetalEmitter *e, const char *format, ...) {
    if (e->failed) return;
    char buffer[192];
    va_list arguments;
    va_start(arguments, format);
    const int count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= sizeof(buffer)) {
        metal_error(e, "Metal formatting operand exceeds the bounded formatter");
        return;
    }
    metal_text(e, buffer);
}
void metal_indent(MetalEmitter *e) {
    for (unsigned i = 0u; i < e->indent; ++i) metal_text(e, "    ");
}
void metal_line(MetalEmitter *e, const char *text) {
    metal_indent(e);
    metal_text(e, text);
    metal_text(e, "\n");
}
bool doe_metal_binding_slot(uint32_t group, uint32_t binding, uint32_t *slot) {
    if (slot == NULL || binding >= DOE_METAL_BINDINGS_PER_GROUP ||
        group > (DOE_METAL_SIZES_SLOT - 1u) / DOE_METAL_BINDINGS_PER_GROUP) return false;
    const uint32_t candidate = group * DOE_METAL_BINDINGS_PER_GROUP + binding;
    if (candidate >= DOE_METAL_SIZES_SLOT) return false;
    *slot = candidate;
    return true;
}
TypeId metal_value_type(MetalEmitter *e, uint32_t id) {
    TypeId type;
    if (target_value_type(e->module, id, &type)) return type;
    metal_error(e, "operand has no typed value identity");
    return IR_VOID;
}
void metal_value(MetalEmitter *e, uint32_t id) {
    (void)metal_value_type(e, id);
    if (id != 0u && id <= e->module->instruction_count &&
        e->module->instructions[id - 1u].opcode == IR_HANDLE) {
        const IrInstruction *handle = &e->module->instructions[id - 1u];
        metal_handle(e, handle);
        metal_format(e, "r%u", handle->operands.scalar.binding);
    } else metal_format(e, "v%u", id);
}
static void emit_module(MetalEmitter *e) {
    e->traversal_left = 4096u * ((uint64_t)e->module->type_count +
        e->module->member_count + e->module->instruction_count + e->module->function_count + 1u);
    e->handle_work_left = 4096u * ((uint64_t)e->module->instruction_count +
        e->module->function_count + e->module->resource_count + 1u);
    const char *closure_error = NULL;
    if (!target_closure_build(e->module, e->selected_entry, &e->closure,
            &e->traversal_left, &closure_error)) {
        metal_error(e, closure_error);
        return;
    }
    metal_text(e, "#include <metal_stdlib>\nusing namespace metal;\n");
    metal_types(e);
    metal_memory_helpers(e);
    metal_workgroup_helpers(e);
    metal_numeric_helpers(e);
    metal_functions(e);
}
DoeCompilerStatus doe_metal_emit(Source *source, const CompilerWorkspace *module,
                                 uint32_t selected_entry, DoeTargetScratch scratch, char *output,
                                 size_t capacity, DoeMetalResult *result) {
    const int saved_errno = errno;
    if (source == NULL || source->diagnostic == NULL || module == NULL || result == NULL) {
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    *result = (DoeMetalResult){0};
    *source->diagnostic = (DoeCompilerDiagnostic){0};
    if ((output == NULL && capacity != 0u) ||
        (selected_entry != NO_ID && selected_entry >= module->entry_count)) {
        (void)source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "invalid Metal output span or entry selection");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    TargetClosure closure;
    if (!target_closure_bind(module, scratch, &closure)) {
        (void)source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "invalid target closure scratch span");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    MetalEmitter measure = {.source = source, .closure = closure, .module = module, .selected_entry = selected_entry};
    emit_module(&measure);
    if (measure.failed) {
        errno = saved_errno;
        return source->diagnostic->status;
    }
    result->required_bytes = measure.position;
    result->entry_count = selected_entry == NO_ID ? module->entry_count : 1u;
    result->needs_sizes_buffer = measure.needs_sizes;
    if (output == NULL) {
        errno = saved_errno;
        return DOE_COMPILER_OK;
    }
    if (capacity < measure.position) {
        (void)source_error(source, DOE_COMPILER_OUTPUT_TOO_SMALL, source->length,
                           "Metal output capacity is smaller than measured source");
        errno = saved_errno;
        return DOE_COMPILER_OUTPUT_TOO_SMALL;
    }
    MetalEmitter writer = {.source = source, .module = module, .output = output,
        .capacity = capacity, .selected_entry = selected_entry, .closure = closure};
    emit_module(&writer);
    if (!writer.failed) result->written_bytes = writer.position;
    errno = saved_errno;
    return source->diagnostic->status;
}
