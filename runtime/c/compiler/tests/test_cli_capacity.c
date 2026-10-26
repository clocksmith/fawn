/* SPDX-License-Identifier: MIT */
#include "../include/doe_compiler_pool_growth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    DoeCompilerStageCapacities capacities = {0};
    for (uint32_t pool = DOE_COMPILER_POOL_TOKENS; pool <= DOE_COMPILER_POOL_STAGE_INTERFACES; ++pool) {
        DoeCompilerStageCapacities c = capacities;
        assert(doe_compiler_grow_stage_pool(&c, DOE_COMPILER_INPUT_ERROR, pool, 1u));
        assert(!doe_compiler_grow_stage_pool(&c, DOE_COMPILER_INPUT_ERROR, pool, 1u));
    }
    const DoeCompilerStatus errors[] = {DOE_COMPILER_OK, DOE_COMPILER_PARSE_ERROR,
        DOE_COMPILER_SEMANTIC_ERROR, DOE_COMPILER_UNSUPPORTED, DOE_COMPILER_ABI_MISMATCH};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        DoeCompilerStageCapacities c = capacities;
        assert(!doe_compiler_grow_stage_pool(&c, errors[i], DOE_COMPILER_POOL_STATEMENTS, 4096u));
        assert(memcmp(&c, &capacities, sizeof(c)) == 0);
    }
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_NONE, 1u));
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_SOURCE, 1u));
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, UINT32_MAX, 1u));
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_TYPES, SIZE_MAX));
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_TYPE_SYNTAX, UINT32_MAX));
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_CONTROL_DEPTH, 33u));
    capacities.module.typed.statements = 1024u;
    assert(doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_STATEMENTS, 1025u));
    assert(capacities.module.typed.statements == 2048u);
    assert(doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_INPUT_ERROR, DOE_COMPILER_POOL_STATEMENTS, 8193u));
    assert(capacities.module.typed.statements == 8193u);
    capacities.module.typed.types = UINT32_MAX - 2u;
    assert(doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_OUTPUT_TOO_SMALL, DOE_COMPILER_POOL_TYPES, UINT32_MAX - 1u));
    assert(!doe_compiler_grow_stage_pool(&capacities, DOE_COMPILER_OUTPUT_TOO_SMALL, DOE_COMPILER_POOL_TYPES, UINT32_MAX));
    puts("CLI capacity: named growth, no semantic retries, depth/overflow/source boundaries passed");
    return 0;
}
