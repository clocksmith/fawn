/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_METAL_PRIVATE_H
#define DOE_METAL_PRIVATE_H
#include "metal.h"
#include "../closure.h"
#include "../ir_view.h"
#include "../../include/doe_compiler_translation.h"
typedef struct {
    Source *source;
    TargetClosure closure;
    const CompilerWorkspace *module;
    char *output;
    size_t capacity, position;
    uint64_t traversal_left, handle_work_left;
    uint32_t selected_entry, current_function, function_end, offset;
    const DoeMetalMetadata *metadata;
    uint32_t current_entry;
    unsigned indent;
    bool failed, needs_sizes;
} MetalEmitter;
void metal_text(MetalEmitter *, const char *);
void metal_format(MetalEmitter *, const char *, ...);
void metal_line(MetalEmitter *, const char *);
void metal_indent(MetalEmitter *);
void metal_error(MetalEmitter *, const char *);
void metal_type(MetalEmitter *, TypeId);
void metal_value(MetalEmitter *, uint32_t);
TypeId metal_value_type(MetalEmitter *, uint32_t);
void metal_constant(MetalEmitter *, TypeId, uint64_t, bool, unsigned);
void metal_types(MetalEmitter *);
void metal_zero(MetalEmitter *, TypeId);
void metal_zero_helpers(MetalEmitter *);
void metal_workgroup_zero_helpers(MetalEmitter *);
void metal_memory_helpers(MetalEmitter *);
void metal_workgroup_helpers(MetalEmitter *);
void metal_numeric_helpers(MetalEmitter *);
void metal_expression(MetalEmitter *, const IrInstruction *);
void metal_array_length(MetalEmitter *, const IrInstruction *);
void metal_address(MetalEmitter *, const IrInstruction *);
void metal_access(MetalEmitter *, const IrInstruction *);
void metal_private_initialization(MetalEmitter *, const EntryRecord *);
void metal_functions(MetalEmitter *);
void metal_graphics_context(MetalEmitter *);
void metal_graphics_wrapper(MetalEmitter *, uint32_t);
void metal_stage_input(MetalEmitter *, const IrInstruction *);
void metal_entry_name(MetalEmitter *, NameSpan);
void metal_resource_parameters(MetalEmitter *, const EntryRecord *, bool *);
void metal_resource_context(MetalEmitter *, const EntryRecord *);
bool metal_function_used(MetalEmitter *, uint32_t);
bool metal_selected_workgroup(MetalEmitter *, uint32_t);
bool metal_selected_resource(MetalEmitter *, uint32_t);
void metal_handle_type(MetalEmitter *, TypeId);
void metal_handle(MetalEmitter *, const IrInstruction *);
void metal_handle_arguments(MetalEmitter *, uint32_t, bool);
void metal_image(MetalEmitter *, const IrInstruction *);
const char *metal_buffer_qualifier(const ResourceRecord *);
bool metal_resource_table(MetalEmitter *, const ResourceRecord *, uint32_t *);
bool metal_dense_plan(MetalEmitter *, DoeMetalMetadata *);
bool metal_dense_selected_resource(MetalEmitter *, uint32_t);
const DoeCompilerMetalBindingV12 *metal_dense_binding(MetalEmitter *, uint32_t, uint32_t);
bool metal_resource_slot(MetalEmitter *, const ResourceRecord *, uint32_t *);
void metal_resource_parameter(MetalEmitter *, uint32_t, bool);
void metal_block(MetalEmitter *, uint32_t *, unsigned, uint32_t, bool);
bool metal_entry_resource(const CompilerWorkspace *, const EntryRecord *, uint32_t);
bool metal_entry_workgroup(const CompilerWorkspace *, const EntryRecord *, uint32_t);
bool metal_entry_sizes(const CompilerWorkspace *, const EntryRecord *);
void metal_float_pack_helpers(MetalEmitter *);
void metal_float_pack_expression(MetalEmitter *, const IrInstruction *, NumericBuiltinKind);
void metal_subgroup(MetalEmitter *, const IrInstruction *, NumericBuiltinKind);
#endif
