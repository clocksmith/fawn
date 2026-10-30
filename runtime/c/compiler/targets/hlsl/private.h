/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_HLSL_PRIVATE_H
#define DOE_HLSL_PRIVATE_H
#include "hlsl.h"
#include "../closure.h"
#include "../ir_view.h"
#include "../../include/doe_compiler_translation.h"
/* Definite assignment of one function's values over its structured regions
 * (assignment.c). Bounded storage: larger functions keep zero initialization. */
enum { HLSL_ASSIGNMENT_VALUES = 512 };
typedef struct {
    uint32_t first, count, sequence, regions, depth;
    bool valid;
    uint32_t stack[DOE_HLSL_MAX_DEPTH + 2u];
    uint32_t region[HLSL_ASSIGNMENT_VALUES], defined[HLSL_ASSIGNMENT_VALUES];
    bool unsafe[HLSL_ASSIGNMENT_VALUES];
} HlslAssignment;
typedef struct {
    Source *source;
    TargetClosure closure;
    const CompilerWorkspace *module;
    char *output;
    size_t capacity, position;
    uint64_t traversal_left;
    uint32_t selected_entry, entry, function_end, offset;
    unsigned indent;
    bool failed, needs_dispatch, uses_subgroups, uses_native16, uses_wave_counter, uses_array_views, uses_raw_storage;
    bool uses_draw_offsets, uses_storage_sizes;
    uint32_t shader_model;
    DoeHlslMetadata metadata;
    HlslAssignment *assignment; /* Non-NULL only during a function's analysis walk. */
} HlslEmitter;
/* Records value id's first assignment, written unconditionally where it stands. */
void hlsl_define(HlslEmitter *, uint32_t id);
void hlsl_assignment_use(HlslEmitter *, uint32_t id);
bool hlsl_assignment_enter(HlslEmitter *);
void hlsl_assignment_leave(HlslEmitter *);
/* Walks the function body without output; true when value id needs no zero initializer. */
void hlsl_assignment_analyze(HlslEmitter *, uint32_t function, HlslAssignment *);
bool hlsl_assigned(const HlslAssignment *, uint32_t id);
void hlsl_text(HlslEmitter *, const char *);
void hlsl_format(HlslEmitter *, const char *, ...);
void hlsl_line(HlslEmitter *, const char *);
void hlsl_indent(HlslEmitter *);
void hlsl_error(HlslEmitter *, const char *);
void hlsl_type(HlslEmitter *, TypeId);
void hlsl_zero(HlslEmitter *, TypeId);
void hlsl_value(HlslEmitter *, uint32_t);
/* A scalar (non-composite) IR constant is written at each use, never as a local. */
bool hlsl_inline_constant(const IrInstruction *);
TypeId hlsl_value_type(HlslEmitter *, uint32_t);
void hlsl_constant(HlslEmitter *, TypeId, uint64_t, bool);
void hlsl_types(HlslEmitter *);
void hlsl_constants(HlslEmitter *);
void hlsl_resources(HlslEmitter *);
bool hlsl_raw_storage(const CompilerWorkspace *, uint32_t);
/* 32-bit scalar storage/uniform accesses are written at the access site, not
 * through a doe_rN_load/store helper; aggregate helpers still call scalar ones. */
bool hlsl_inline_scalar(const CompilerWorkspace *, TypeId);
void hlsl_inline_load(HlslEmitter *, uint32_t resource, TypeId type);
void hlsl_inline_store_open(HlslEmitter *, uint32_t resource);
void hlsl_inline_store_close(HlslEmitter *, uint32_t resource);
void hlsl_register(HlslEmitter *, uint32_t resource, uint32_t kind, uint32_t stride);
void hlsl_handle_type(HlslEmitter *, TypeId);
void hlsl_image_resource(HlslEmitter *, uint32_t);
void hlsl_image(HlslEmitter *, const IrInstruction *);
void hlsl_handle(HlslEmitter *, const IrInstruction *);
bool hlsl_image_uav(const TypeRecord *);
uint32_t hlsl_image_components(HlslEmitter *, const TypeRecord *);
void hlsl_numeric_helpers(HlslEmitter *);
void hlsl_expression(HlslEmitter *, const IrInstruction *);
void hlsl_composite(HlslEmitter *, const IrInstruction *);
void hlsl_array_length(HlslEmitter *, const IrInstruction *);
void hlsl_access(HlslEmitter *, const IrInstruction *);
void hlsl_private_initialization(HlslEmitter *);
/* Storage-buffer byte sizes come from the runtime-supplied DoeStorageSizes
 * table; true when the selected entry reads this resource's size. */
bool hlsl_storage_size_used(HlslEmitter *, uint32_t resource);
void hlsl_functions(HlslEmitter *);
void hlsl_entry_name(HlslEmitter *, uint32_t);
bool hlsl_graphics_admit(HlslEmitter *);
void hlsl_graphics_context(HlslEmitter *);
void hlsl_graphics_wrapper(HlslEmitter *);
void hlsl_stage_input(HlslEmitter *, const IrInstruction *);
void hlsl_block(HlslEmitter *, uint32_t *, unsigned, uint32_t, bool);
bool hlsl_entry_resource(HlslEmitter *, uint32_t);
bool hlsl_entry_workgroup(HlslEmitter *, uint32_t);
bool hlsl_function_used(HlslEmitter *, uint32_t);
void hlsl_float_pack_helpers(HlslEmitter *);
void hlsl_float_pack_expression(HlslEmitter *, const IrInstruction *, NumericBuiltinKind);
void hlsl_subgroup(HlslEmitter *, const IrInstruction *, NumericBuiltinKind);
uint32_t hlsl_profile(HlslEmitter *);
#endif
