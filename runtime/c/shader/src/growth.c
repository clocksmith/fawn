/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_compiler_pool_growth.h"

bool shader_grow_pools(doe_shader_options *options, DoeCompilerStageCapacities *stages,
    size_t *overrides, DoeCompilerStatus status, uint32_t pool, size_t minimum)
{
    if (!options->grow_pools ||
        (status != DOE_COMPILER_INPUT_ERROR && status != DOE_COMPILER_OUTPUT_TOO_SMALL)) return false;
    bool grew;
    if (pool == DOE_COMPILER_POOL_OVERRIDES)
        grew = overrides && doe_compiler_grow_pool_capacity(overrides, minimum);
    else if (pool == DOE_COMPILER_POOL_SAMPLING_PAIRS)
        grew = doe_compiler_grow_pool_capacity(&options->sampling_pairs, minimum);
    else if (pool == DOE_COMPILER_POOL_TRANSLATION_ENTRIES)
        grew = doe_compiler_grow_pool_capacity(&stages->module.entries, minimum);
    else grew = doe_compiler_grow_stage_pool(stages, status, pool, minimum);
    if (grew) {
        options->capacities = stages->module;
        options->interface_attributes = stages->interface_attributes;
        options->stage_interfaces = stages->stage_interfaces;
    }
    return grew;
}

doe_shader_status shader_input_resize(const doe_shader_allocator *allocator,
    const DoeCompilerWorkspaceRequirements *old, const DoeCompilerWorkspaceRequirements *next,
    size_t source_size, size_t identity_size, bool snapshot, shader_input *input)
{
    shader_input replacement;
    doe_shader_status status = shader_input_allocate(allocator, next, input->source,
        source_size, input->identity, identity_size, snapshot, input->selection, input->selection_size, &replacement);
    if (status != DOE_SHADER_OK) return status;
    /* Copy from the previous owned snapshot before releasing it. Retry never
     * rereads the caller's source or identity after allocator reentry. */
    allocator->deallocate(allocator->userdata, input->allocation, input->bytes, old->alignment_bytes);
    *input = replacement;
    return DOE_SHADER_OK;
}
