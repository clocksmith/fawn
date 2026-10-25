/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_OVERRIDE_H
#define DOE_COMPILER_OVERRIDE_H
#include "sema.h"
#include "sampling_internal.h"
#include "../include/doe_compiler_module_specialization.h"
#include "../include/doe_compiler_metal_pipeline.h"
enum { DECLARATION_OVERRIDE = 32u, DECLARATION_KNOWN = 64u };
#define OVERRIDE_NO_INITIALIZER (UINT32_MAX - 1u)
typedef struct {
    uint32_t declaration, id_expression, id, flags;
    uint32_t supplied, required, state, parent, scan;
    TypeId type;
} OverrideRecord;
struct SpecializationContext {
    SamplingContext *sampling;
    OverrideRecord *records;
    uint32_t count, capacity;
    const DoeCompilerOverrideValueV13 *values;
    size_t value_count;
    uint32_t *functions;
    unsigned char *workgroups;
    uint32_t *entry_expressions;
    uint32_t selected;
    bool specializing, all_entries, metal_pipeline;
    uint32_t metal_sample_mask;
    const DoeCompilerSpecializationCapacities *capacities;
    const DoeCompilerEntrySelectionV5 *selection;
    size_t stage_base;
    uint64_t work;
};
bool override_charge(Source *, uint64_t);
OverrideRecord *override_declaration(Source *, uint32_t);
bool override_finish(Source *, uint32_t, const LocalScope *);
bool override_integer(Source *, uint32_t, const LocalScope *, uint32_t *, bool *);
bool override_array_type(Source *, TypeId, uint32_t, TypeId *);
bool override_workgroup_size(Source *, EntryRecord *, const LocalScope *);
bool specialization_layout(uint32_t, const DoeCompilerSpecializationCapacities *,
                           size_t *, DoeCompilerWorkspaceRequirements *);
void specialization_bind(SpecializationContext *, void *, size_t,
                         const DoeCompilerSpecializationCapacities *);
bool override_plan(Source *);
bool override_reparse(Source *);
DoeCompilerStatus compiler_specialized_text(uint32_t, const DoeCompilerTextCapacities *,
    void *, size_t, const char *, size_t, const DoeCompilerEntrySelectionV5 *, uint32_t,
    const DoeCompilerSpecializationOutputs *, DoeCompilerTextResult *, SpecializationContext *);
#endif
