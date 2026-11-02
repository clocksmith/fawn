/* SPDX-License-Identifier: MIT
 * Retained analysis matches legacy output and survives rejected publication. */
#include "../include/doe_compiler_prepared.h"
#include "metal12_fixture.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DOE_PREPARED_COUNT_ANALYSES
#include "../src/compile_internal.h"
static size_t analysis_calls;
static bool fail_restore;
int __real_fesetenv(const fenv_t *);
int __wrap_fesetenv(const fenv_t *);
int __wrap_fesetenv(const fenv_t *environment)
{
    if (fail_restore) { fail_restore = false; return 1; }
    return __real_fesetenv(environment);
}
bool __real_compiler_analyze(Source *, CompilerWorkspace *, NumericEnvironment *);
bool __wrap_compiler_analyze(Source *, CompilerWorkspace *, NumericEnvironment *);
bool __wrap_compiler_analyze(Source *source, CompilerWorkspace *compiler, NumericEnvironment *environment)
{
    ++analysis_calls;
    return __real_compiler_analyze(source, compiler, environment);
}
#endif
static void reset_analysis(void)
{
#ifdef DOE_PREPARED_COUNT_ANALYSES
    analysis_calls = 0u;
#endif
}
static void analyses(size_t expected)
{
#ifdef DOE_PREPARED_COUNT_ANALYSES
    assert(analysis_calls == expected);
#else
    (void)expected;
#endif
}
typedef struct {
    Metal12Fixture target;
    uint32_t words[32768];
    DoeCompilerSamplingPairV16 pairs[16];
    DoeCompilerTranslationEntryV17 translations[8];
} Fixture;
static DoeCompilerTranslationOutputsV17 outputs(uint32_t target, Fixture *fixture)
{
    DoeCompilerTranslationOutputsV17 out = {.sampling={.pairs=fixture->pairs,.pair_capacity=16u,
        .pair_record_size=sizeof(DoeCompilerSamplingPairV16)},
        .entries=fixture->translations,.entry_capacity=8u,.entry_record_size=sizeof(DoeCompilerTranslationEntryV17)};
    if (target == DOE_COMPILER_TARGET_METAL) out.sampling.target.metal = metal12_outputs(&fixture->target);
    else if (target == DOE_COMPILER_TARGET_HLSL) out.sampling.target.hlsl = hlsl_outputs(&fixture->target.results);
    else {
        out.sampling.target.spirv = hlsl_outputs(&fixture->target.results).stages;
        out.sampling.target.spirv.resources.module.words = fixture->words;
        out.sampling.target.spirv.resources.module.word_capacity = 32768u;
    }
    return out;
}
static DoeCompilerPrepareOptionsV20 options(uint32_t target, uint32_t kind)
{
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.text_bytes = 0u;
    caps.stages.module.typed.words = 32768u;
    caps.stages.module.typed.target_declarations = 8192u;
    return (DoeCompilerPrepareOptionsV20){.target=target,.kind=kind,
        .capacities={.sampling={.specialization={.target=caps,.overrides=8u},.sampling_pairs=16u},
            .translation_entries=8u},
        .sampling={.metal_sample_mask=UINT32_MAX},.value_record_size=sizeof(DoeCompilerOverrideValueV13),
        .selection={.mode=DOE_COMPILER_SELECT_NAME,.name="main",.name_length=4u}};
}
static void module_case(void)
{
    static const char source[] = "diagnostic(warning,unknown_rule);override n:u32;@compute @workgroup_size(n) fn main(){}";
    DoeCompilerPrepareOptionsV20 policy = options(DOE_COMPILER_TARGET_METAL, DOE_COMPILER_PREPARE_MODULE);
    policy.selection = (DoeCompilerEntrySelectionV5){0};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_prepare_requirements(20u,&policy,sizeof(policy),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace);
    DoeCompilerPreparedResultV20 prepared;
    memset(&prepared,0x5a,sizeof(prepared));
    const DoeCompilerPreparedResultV20 untouched = prepared;
    assert(doe_compiler_prepare(19u,&policy,sizeof(policy),workspace,requirements.size_bytes,
        source,sizeof(source)-1u,&prepared,sizeof(prepared)) == DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&prepared,&untouched,sizeof(prepared)));
    reset_analysis();
    assert(doe_compiler_prepare(20u,&policy,sizeof(policy),workspace,requirements.size_bytes,
        source,sizeof(source)-1u,&prepared,sizeof(prepared)) == DOE_COMPILER_OK);
    analyses(1u);
    assert(prepared.validation.validation.required_overrides == 1u &&
        prepared.validation.validation.required_entries == 1u && prepared.validation.required_notices == 1u);
    struct { DoeCompilerOverrideV13 overrides[8]; DoeCompilerModuleEntryV13 entries[8];
        char names[128]; DoeCompilerNoticeV18 notices[8]; } data, saved;
    memset(&data,0xa5,sizeof(data)); saved = data;
    DoeCompilerDiagnosticOutputsV18 out = {.metadata={.overrides=data.overrides,.override_capacity=8u,
        .override_record_size=sizeof(data.overrides[0]),.entries=data.entries,.entry_capacity=8u,
        .entry_record_size=sizeof(data.entries[0]),.names=data.names,.name_capacity=sizeof(data.names)},
        .notices=data.notices,.notice_capacity=0u,.notice_record_size=sizeof(data.notices[0])};
    assert(doe_compiler_publish_module(20u,workspace,requirements.size_bytes,&out,sizeof(out),
        &prepared,sizeof(prepared)) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(!memcmp(&data,&saved,sizeof(data)) && !prepared.validation.published_notices);
    analyses(1u);
    out.notice_capacity = 8u;
    assert(doe_compiler_publish_module(20u,workspace,requirements.size_bytes,&out,sizeof(out),
        &prepared,sizeof(prepared)) == DOE_COMPILER_OK);
    assert(prepared.validation.published_notices == 1u && prepared.validation.validation.published_entries == 1u);
    assert(!strcmp(data.names + data.entries[0].name.offset,"main"));
    assert(data.notices[0].rule == DOE_COMPILER_RULE_UNRECOGNIZED);
    analyses(1u);
    saved = data;
    assert(doe_compiler_publish_module(20u,workspace,requirements.size_bytes,&out,sizeof(out),
        &prepared,sizeof(prepared)) == DOE_COMPILER_OK);
    assert(!memcmp(&data,&saved,sizeof(data)));
    analyses(1u);
    free(workspace);
}
static DoeCompilerStatus reference(const DoeCompilerPrepareOptionsV20 *policy,
    const char *source, const DoeCompilerTranslationOutputsV17 *out, DoeCompilerPreparedResultV20 *result)
{
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerTextCapacities caps = policy->capacities.sampling.specialization.target;
    caps.text_bytes = 65536u;
    if (policy->kind == DOE_COMPILER_PREPARE_SPECIALIZED) {
        DoeCompilerTranslationCapacitiesV17 all = policy->capacities;
        all.sampling.specialization.target = caps;
        assert(doe_compiler_translation_requirements(17u,policy->target,&all,sizeof(all),
            &requirements,sizeof(requirements)) == DOE_COMPILER_OK);
        void *workspace = malloc(requirements.size_bytes);
        assert(workspace);
        DoeCompilerStatus status;
        if (policy->target == DOE_COMPILER_TARGET_HLSL)
            status = doe_compiler_specialize_hlsl_profile(19u,&policy->sampling,sizeof(policy->sampling),
                &all,sizeof(all),workspace,requirements.size_bytes,source,strlen(source),&policy->selection,sizeof(policy->selection),
                policy->values,policy->value_count,policy->value_record_size,3u,out,sizeof(*out),&result->compiled,sizeof(result->compiled));
        else status = doe_compiler_specialize_translation(17u,policy->target,&policy->sampling,sizeof(policy->sampling),
            &all,sizeof(all),workspace,requirements.size_bytes,source,strlen(source),&policy->selection,sizeof(policy->selection),
            policy->values,policy->value_count,policy->value_record_size,3u,out,sizeof(*out),
            &result->compiled.translation,sizeof(result->compiled.translation));
        free(workspace);
        return status;
    }
    if (policy->target == DOE_COMPILER_TARGET_SPIRV)
        assert(doe_compiler_stage_requirements(9u,&caps.stages,sizeof(caps.stages),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    else if (policy->target == DOE_COMPILER_TARGET_METAL)
        assert(doe_compiler_metal_requirements(12u,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    else assert(doe_compiler_hlsl_requirements(11u,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace);
    DoeCompilerTextResult *target = &result->compiled.translation.sampling.target;
    DoeCompilerStatus status;
    if (policy->target == DOE_COMPILER_TARGET_SPIRV)
        status = doe_compiler_compile_stages(9u,&caps.stages,sizeof(caps.stages),workspace,requirements.size_bytes,
            source,strlen(source),&policy->selection,sizeof(policy->selection),3u,&out->sampling.target.spirv,sizeof(out->sampling.target.spirv),
            &target->stages,sizeof(target->stages));
    else if (policy->target == DOE_COMPILER_TARGET_METAL)
        status = doe_compiler_compile_metal(12u,&caps,sizeof(caps),workspace,requirements.size_bytes,
            source,strlen(source),&policy->selection,sizeof(policy->selection),3u,&out->sampling.target.metal,sizeof(out->sampling.target.metal),
            target,sizeof(*target));
    else status = doe_compiler_compile_hlsl(11u,&caps,sizeof(caps),workspace,requirements.size_bytes,
            source,strlen(source),&policy->selection,sizeof(policy->selection),3u,&out->sampling.target.hlsl,sizeof(out->sampling.target.hlsl),
            target,sizeof(*target));
    free(workspace);
    return status;
}
static void target_case(uint32_t target, bool specialized)
{
    const char *source = specialized ?
        "override n:u32;@group(0) @binding(0) var<storage,read_write> out:array<u32>;@compute @workgroup_size(1) fn main(){out[0]=n;}" :
        "@group(0) @binding(0) var<storage,read_write> out:array<u32>;@compute @workgroup_size(1) fn main(){out[0]=5u;}";
    const DoeCompilerOverrideValueV13 value = {"n",1u,5.0};
    DoeCompilerPrepareOptionsV20 policy = options(target, specialized ? DOE_COMPILER_PREPARE_SPECIALIZED : DOE_COMPILER_PREPARE_LEGACY);
    if (specialized) { policy.values = &value; policy.value_count = 1u; }
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_prepare_requirements(20u,&policy,sizeof(policy),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    Fixture *actual = malloc(sizeof(*actual)), *expected = malloc(sizeof(*expected));
    assert(workspace && actual && expected);
    memset(actual,0xa5,sizeof(*actual)); memset(expected,0xa5,sizeof(*expected));
    DoeCompilerPreparedResultV20 measured, result, legacy = {0};
    reset_analysis();
    assert(doe_compiler_prepare(20u,&policy,sizeof(policy),workspace,requirements.size_bytes,
        source,strlen(source),&measured,sizeof(measured)) == DOE_COMPILER_OK);
    analyses(1u);
    void *scratch = measured.publication_bytes ? malloc(measured.publication_bytes) : NULL;
    assert(!measured.publication_bytes || scratch);
    DoeCompilerTranslationOutputsV17 out = outputs(target,actual), wrong = out;
    memset(&result,0x5a,sizeof(result));
    const DoeCompilerPreparedResultV20 untouched = result;
    --wrong.entry_record_size;
    assert(doe_compiler_publish(20u,workspace,requirements.size_bytes,scratch,measured.publication_bytes,
        &wrong,sizeof(wrong),&result,sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&result,&untouched,sizeof(result)) && !memcmp(actual,expected,sizeof(*actual)));
    wrong = out;
    if (target == DOE_COMPILER_TARGET_SPIRV) wrong.sampling.target.spirv.resources.module.word_capacity = 0u;
    else if (target == DOE_COMPILER_TARGET_METAL) wrong.sampling.target.metal.text_capacity = 0u;
    else wrong.sampling.target.hlsl.text_capacity = 0u;
    assert(doe_compiler_publish(20u,workspace,requirements.size_bytes,scratch,measured.publication_bytes,
        &wrong,sizeof(wrong),&result,sizeof(result)) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(!memcmp(actual,expected,sizeof(*actual)));
    analyses(1u);
    if (measured.publication_bytes) {
        assert(doe_compiler_publish(20u,workspace,requirements.size_bytes,scratch,measured.publication_bytes-1u,
            &out,sizeof(out),&result,sizeof(result)) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(!memcmp(actual,expected,sizeof(*actual)));
#ifdef DOE_PREPARED_COUNT_ANALYSES
        fail_restore = true;
        assert(doe_compiler_publish(20u,workspace,requirements.size_bytes,scratch,measured.publication_bytes,
            &out,sizeof(out),&result,sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
        assert(!fail_restore && strstr(result.compiled.translation.sampling.target.stages.resources.module.diagnostic.message,
            "restoration failed"));
        assert(!memcmp(actual,expected,sizeof(*actual)));
#endif
        analyses(1u);
    }
    assert(doe_compiler_publish(20u,workspace,requirements.size_bytes,scratch,measured.publication_bytes,
        &out,sizeof(out),&result,sizeof(result)) == DOE_COMPILER_OK);
    analyses(1u);
    DoeCompilerTranslationOutputsV17 oracle = outputs(target,expected);
    assert(reference(&policy,source,&oracle,&legacy) == DOE_COMPILER_OK);
    analyses(2u);
    assert(!memcmp(actual,expected,sizeof(*actual)));
    assert(!memcmp(&result.compiled,&legacy.compiled,sizeof(result.compiled)));
    assert(doe_compiler_publish(20u,workspace,requirements.size_bytes,scratch,measured.publication_bytes,
        &out,sizeof(out),&result,sizeof(result)) == DOE_COMPILER_OK);
    analyses(2u);
    assert(!memcmp(actual,expected,sizeof(*actual)));
    free(scratch); free(expected); free(actual); free(workspace);
}
int main(void)
{
    errno = ERANGE;
    assert(!fesetround(FE_DOWNWARD));
    module_case();
    for (uint32_t target = 0u; target <= DOE_COMPILER_TARGET_HLSL; ++target) {
        target_case(target,false);
        target_case(target,true);
    }
    assert(errno == ERANGE && fegetround() == FE_DOWNWARD);
    assert(!fesetround(FE_TONEAREST));
    puts("prepared compiler: legacy/module/specialized output parity, retained retry, exact publication and FP preservation passed");
    return 0;
}
