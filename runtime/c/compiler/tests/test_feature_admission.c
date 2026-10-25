/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_specialization.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    static const struct { const char *name; uint32_t flag; } features[] = {
        {"clip_distances",DOE_COMPILER_DISABLE_CLIP_DISTANCES},
        {"primitive_index",DOE_COMPILER_DISABLE_PRIMITIVE_INDEX},
        {"dual_source_blending",DOE_COMPILER_DISABLE_DUAL_SOURCE_BLENDING}
    };
    DoeCompilerSpecializationCapacities caps = {.target=hlsl_capacities(),.overrides=16u};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_specialization_requirements(13u,0u,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    for (size_t i=0u;i<sizeof(features)/sizeof(features[0]);++i) {
        char source[160];
        const int length = snprintf(source,sizeof(source),"enable %s; @compute @workgroup_size(1) fn main(){}",features[i].name);
        assert(length > 0 && (size_t)length < sizeof(source));
        for (uint32_t disabled=0u;disabled<2u;++disabled) {
            DoeCompilerValidationResult result;
            errno = ERANGE;
            const DoeCompilerStatus status = doe_compiler_validate_module(13u,&caps,sizeof(caps),workspace,requirements.size_bytes,
                source,(size_t)length,disabled != 0u ? features[i].flag : 0u,NULL,0u,&result,sizeof(result));
            assert(errno == ERANGE);
            assert(status == (disabled != 0u ? DOE_COMPILER_UNSUPPORTED : DOE_COMPILER_OK));
            assert(result.published_entries == 0u);
            if (disabled != 0u) assert(strstr(result.diagnostic.message,"not enabled by the compiler consumer") != NULL);
        }
    }
    free(workspace);
    puts("optional graphics features require explicit compiler-consumer authorization");
    return 0;
}
