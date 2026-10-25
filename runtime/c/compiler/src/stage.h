/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_STAGE_H
#define DOE_COMPILER_STAGE_H
#include "internal.h"
#include "../include/doe_compiler_stages.h"
enum { STAGE_ATTRIBUTE_MEMBER, STAGE_ATTRIBUTE_PARAMETER, STAGE_ATTRIBUTE_RESULT };
enum { STAGE_HAS_BUILTIN = 1u, STAGE_HAS_LOCATION = 2u, STAGE_HAS_INTERPOLATION = 4u,
    STAGE_HAS_INVARIANT = 8u, STAGE_HAS_BLEND_SRC = 16u };
typedef struct {
    uint32_t owner, index, offset, flags;
    uint32_t builtin, location_expression, interpolation, sampling, blend_expression, reserved;
} InterfaceAttribute;
typedef struct {
    uint32_t entry, direction, formal, member; TypeId type, target_type;
    uint32_t location, builtin, interpolation, sampling, flags, target;
    NameSpan name;
} StageInterface;
typedef struct { uint32_t formal_first, formal_count, interface_first, interface_count; } StageEntry;
struct StageWorkspace {
    InterfaceAttribute *attributes; StageInterface *interfaces; StageEntry *entries;
    uint32_t attribute_capacity, interface_capacity, attribute_count, interface_count;
};
bool stage_layout(const DoeCompilerStageCapacities *, CompilerLimits *, size_t *, size_t *);
void stage_bind(Source *, void *, const DoeCompilerStageCapacities *, size_t);
bool stage_graphics(const FunctionRecord *);
uint32_t stage_kind(const FunctionRecord *);
InterfaceAttribute *stage_attribute(Source *, uint32_t, uint32_t, uint32_t, bool);
uint32_t stage_parameter(const Source *, NameSpan);
uint64_t stage_name_bytes(const Source *);
void stage_reflect(const Source *, DoeCompilerStageInterfaceV7 *, char *, uint32_t);
bool stage_closure(Source *, uint32_t, SourceReference, uint32_t);
#endif
