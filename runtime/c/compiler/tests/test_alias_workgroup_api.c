/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerJobCapacities capacities = {
    .source_bytes = 8192u, .tokens = 2048u, .statements = 64u,
    .expressions = 256u, .words = 8192u, .expression_depth = 32u, .control_depth = 8u
};
static const char source[] =
    "alias Rows=array<vec2u,2>;alias Floating=f32;"
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "var<workgroup> cache:Rows;"
    "@compute @workgroup_size(1) fn main(){"
    "cache=Rows(vec2u(1,2),vec2u(3,4));let values=cache;out[0]=values[1].y;"
    "out[1]=bitcast<u32>(pow(Floating(2),Floating(3)));"
    "out[2]=bitcast<u32>(atan2(Floating(0),Floating(1)));}";
static DoeCompilerStatus compile(const DoeCompilerJobCapacities *caps, void *workspace,
                                size_t bytes, const char *text, uint32_t *words,
                                size_t count, DoeCompilerResultV2 *result) {
    return doe_compiler_compile_job(3u, caps, sizeof(*caps), workspace, bytes, text,
        strlen(text), words, count, result, sizeof(*result));
}
static void untouched(const uint32_t *words, size_t count) {
    for (size_t i = 0u; i < count; ++i) assert(words[i] == UINT32_C(0xa5a5a5a5));
}
static void failed(const DoeCompilerResultV2 *result, const uint32_t *words) {
    assert(result->word_count == 0u);
    const DoeCompilerReflectionV2 empty = {0};
    assert(memcmp(&result->reflection, &empty, sizeof(empty)) == 0);
    untouched(words, capacities.words);
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_job_requirements(3u, &capacities, sizeof(capacities),
        &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    uint32_t *words = malloc(capacities.words * sizeof(*words));
    uint32_t *baseline = malloc(capacities.words * sizeof(*baseline));
    assert(workspace != NULL && words != NULL && baseline != NULL);
    DoeCompilerResultV2 result;
    assert(compile(&capacities, workspace, requirements.size_bytes, source, baseline,
        capacities.words, &result) == DOE_COMPILER_OK);
    const size_t count = result.word_count;
    fenv_t saved;
    assert(fegetenv(&saved) == 0);
    const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (size_t i = 0u; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        assert(fesetround(modes[i]) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);
        assert(feraiseexcept(FE_INVALID | FE_INEXACT) == 0);
        const int exceptions = fetestexcept(FE_ALL_EXCEPT);
        errno = EBUSY;
        assert(compile(&capacities, workspace, requirements.size_bytes, source, words,
            capacities.words, &result) == DOE_COMPILER_OK);
        assert(result.word_count == count && memcmp(words, baseline, count * sizeof(*words)) == 0);
        assert(fegetround() == modes[i] && fetestexcept(FE_ALL_EXCEPT) == exceptions && errno == EBUSY);
        memset(words, 0xa5, capacities.words * sizeof(*words));
        assert(compile(&capacities, workspace, requirements.size_bytes, source, words,
            count - 1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        failed(&result, words);
        const char *bad = "const bad=pow(-2.0,2.0);@compute @workgroup_size(1) fn main(){}";
        assert(compile(&capacities, workspace, requirements.size_bytes, bad, words,
            capacities.words, &result) == DOE_COMPILER_SEMANTIC_ERROR);
        failed(&result, words);
        assert(fegetround() == modes[i] && fetestexcept(FE_ALL_EXCEPT) == exceptions && errno == EBUSY);
    }
    assert(fesetenv(&saved) == 0);
    const char *cycle = "alias A=B;alias B=A;@compute @workgroup_size(1) fn main(){}";
    assert(compile(&capacities, workspace, requirements.size_bytes, cycle, words,
        capacities.words, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    failed(&result, words);
    DoeCompilerJobCapacities small = capacities;
    small.expressions = 1u;
    DoeCompilerWorkspaceRequirements limited;
    assert(doe_compiler_job_requirements(3u, &small, sizeof(small),
        &limited, sizeof(limited)) == DOE_COMPILER_OK);
    const char *aliases = "alias A=u32;alias B=A;alias C=B;@compute @workgroup_size(1) fn main(){}";
    assert(compile(&small, workspace, requirements.size_bytes, aliases, words,
        capacities.words, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(strstr(result.diagnostic.message, "type alias capacity") != NULL);
    failed(&result, words);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, words,
        capacities.words, &result) == DOE_COMPILER_OK);
    assert(result.word_count == count && memcmp(words, baseline, count * sizeof(*words)) == 0);
    free(baseline);
    free(words);
    free(workspace);
    puts("Alias/Workgroup API: bounded declarations, math invariance, FP restoration, atomic failure and reuse passed.");
    return 0;
}
