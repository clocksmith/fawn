/* SPDX-License-Identifier: MIT */
#ifndef DOE_SHADER_INTERNAL_H
#define DOE_SHADER_INTERNAL_H
#include "doe_shader.h"
#include <limits.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct doe_shader {
    atomic_uint references;
    doe_shader_allocator allocator;
    size_t allocation_size, source_size, identity_size;
    char *source;
    unsigned char *identity;
    uint32_t selection_mode, semantic_flags;
    size_t selection_size;
    char *selection_name;
    enum { SHADER_LEGACY, SHADER_MODULE, SHADER_SPECIALIZED } kind;
    doe_shader *module; /* Specialized result retains immutable module source/policy. */
    doe_shader_options options;
    size_t override_capacity;
    DoeCompilerValidationOutputs validation;
    size_t override_count, module_entry_count, module_name_bytes;
    DoeCompilerNoticeV18 *notices;
    size_t notice_count;
    DoeCompilerOverrideValueV13 *values;
    size_t value_count;
    DoeCompilerStageOutputs outputs;
    doe_shader_view view;
};


typedef struct shader_input {
    void *allocation;
    size_t bytes;
    const char *source;
    const void *identity;
    const char *selection;
    size_t selection_size;
} shader_input;
doe_shader_status shader_input_allocate(const doe_shader_allocator *,
    const DoeCompilerWorkspaceRequirements *, const char *, size_t, const void *, size_t,
    bool, const char *, size_t, shader_input *);
bool shader_grow_pools(doe_shader_options *, DoeCompilerStageCapacities *, size_t *,
    DoeCompilerStatus, uint32_t, size_t);
doe_shader_status shader_input_resize(const doe_shader_allocator *,
    const DoeCompilerWorkspaceRequirements *, const DoeCompilerWorkspaceRequirements *,
    size_t, size_t, bool, shader_input *);
doe_shader_status shader_fail(doe_shader_error *, doe_shader_status, DoeCompilerStatus, const DoeCompilerDiagnostic *);
bool shader_reserve(size_t *, size_t, size_t, size_t, size_t *);
bool shader_layout(doe_shader_target, DoeCompilerModuleOutputCounts, size_t, size_t,
    size_t, size_t, size_t, size_t, size_t, size_t *, size_t [15]);
void shader_assign_outputs(doe_shader *, const size_t [15], DoeCompilerModuleOutputCounts, size_t, size_t);
bool shader_options_valid(const doe_shader_options *);
doe_shader_status shader_compile(const doe_shader_options *, const char *, size_t,
    const doe_shader *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *, doe_shader **, doe_shader_error *);
#endif
