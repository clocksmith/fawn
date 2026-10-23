/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_HLSL_PRIVATE_H
#define DOE_HLSL_PRIVATE_H
#include "hlsl.h"
#include "../closure.h"
typedef struct {
    Source *source;
    TargetClosure closure;
    const CompilerWorkspace *module;
    char *output;
    size_t capacity, position;
    uint64_t traversal_left;
    uint32_t selected_entry, entry, function_end, offset;
    unsigned indent;
    bool failed, needs_dispatch, uses_subgroups;
} HlslEmitter;
void hlsl_text(HlslEmitter *, const char *);
void hlsl_format(HlslEmitter *, const char *, ...);
void hlsl_line(HlslEmitter *, const char *);
void hlsl_indent(HlslEmitter *);
void hlsl_error(HlslEmitter *, const char *);
void hlsl_type(HlslEmitter *, TypeId);
void hlsl_zero(HlslEmitter *, TypeId);
void hlsl_value(HlslEmitter *, uint32_t);
TypeId hlsl_value_type(HlslEmitter *, uint32_t);
void hlsl_constant(HlslEmitter *, TypeId, uint64_t, bool);
void hlsl_types(HlslEmitter *);
void hlsl_constants(HlslEmitter *);
void hlsl_resources(HlslEmitter *);
void hlsl_handle_type(HlslEmitter *, TypeId);
void hlsl_image_resource(HlslEmitter *, uint32_t);
void hlsl_image(HlslEmitter *, const IrInstruction *);
void hlsl_handle(HlslEmitter *, const IrInstruction *);
uint32_t hlsl_image_components(HlslEmitter *, const TypeRecord *);
void hlsl_numeric_helpers(HlslEmitter *);
void hlsl_expression(HlslEmitter *, const IrInstruction *);
void hlsl_composite(HlslEmitter *, const IrInstruction *);
void hlsl_array_length(HlslEmitter *, const IrInstruction *);
void hlsl_access(HlslEmitter *, const IrInstruction *);
void hlsl_functions(HlslEmitter *);
void hlsl_entry_name(HlslEmitter *, NameSpan);
bool hlsl_graphics_admit(HlslEmitter *);
void hlsl_graphics_context(HlslEmitter *);
void hlsl_graphics_wrapper(HlslEmitter *);
void hlsl_stage_input(HlslEmitter *, const IrInstruction *);
void hlsl_block(HlslEmitter *, uint32_t *, unsigned, uint32_t, bool);
bool hlsl_entry_resource(HlslEmitter *, uint32_t);
bool hlsl_entry_workgroup(HlslEmitter *, uint32_t);
bool hlsl_function_used(HlslEmitter *, uint32_t);
#endif
