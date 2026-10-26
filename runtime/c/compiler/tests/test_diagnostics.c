/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_job.h"
#include "../include/doe_compiler_diagnostics.h"
#include "../include/doe_compiler_translation.h"
#include "../include/doe_compiler_metal_pipeline.h"
#include "../include/doe_compiler_module_specialization.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DoeCompilerStatus validate(const char *source, const DoeCompilerDiagnosticOutputsV18 *outputs,
    DoeCompilerDiagnosticResultV18 *result)
{
    DoeCompilerSpecializationCapacities caps = {.target=hlsl_capacities(),.overrides=32};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_specialization_requirements(13,0,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes);
    assert(workspace);
    DoeCompilerStatus status=doe_compiler_validate_module_diagnostics(18,&caps,sizeof(caps),workspace,
        requirements.size_bytes,source,strlen(source),DOE_COMPILER_CORE_BINDING_NUMBERS,
        outputs,outputs?sizeof(*outputs):0,result,sizeof(*result));
    free(workspace);
    return status;
}
static const char *derivative = "@fragment fn main(@location(0) x:f32)->@location(0) f32 {if(x>0.0){return dpdx(x);}return 0.0;}";
static const char *subgroup = "enable subgroups; @compute @workgroup_size(4) fn main(@builtin(local_invocation_index) x:u32){if(x>0u){let y=subgroupAdd(x);}}";
static void policy(const char *rule, const char *body)
{
    const char *severities[]={"off","error","warning","info"};
    for(size_t i=0;i<4;++i){
        char source[2048];
        assert(snprintf(source,sizeof(source),"diagnostic(%s,%s); %s",severities[i],rule,body)>0);
        DoeCompilerDiagnosticResultV18 result;
        DoeCompilerStatus status=validate(source,NULL,&result);
        if(status!=DOE_COMPILER_OK && i!=1) fprintf(stderr,"%s\n",result.validation.diagnostic.message);
        assert((status==DOE_COMPILER_OK)==(i!=1));
        if(i==1) continue;
        assert(result.required_notices==(i>=2?1u:0u));
        DoeCompilerModuleEntryV13 entries[8];char names[256];DoeCompilerNoticeV18 notices[8];
        DoeCompilerDiagnosticOutputsV18 outputs={.metadata={.entries=entries,.entry_capacity=8,
            .entry_record_size=sizeof(*entries),.override_record_size=sizeof(DoeCompilerOverrideV13),
            .names=names,.name_capacity=sizeof(names)},.notices=notices,.notice_capacity=8,.notice_record_size=sizeof(*notices)};
        assert(validate(source,&outputs,&result)==DOE_COMPILER_OK);
        assert(result.published_notices==result.required_notices);
        if(i>=2){assert(notices[0].severity==i);assert(notices[0].length>0);assert(notices[0].message[0]);}
    }
}
static void exact_locations_and_atomicity(void)
{
    const char *source="diagnostic(warning,derivative_uniformity); fn helper(x:f32)->f32{return dpdx(x);} "
        "@fragment fn main(@location(0)x:f32)->@location(0) f32{if(x>0.0){return helper(x);}return 0.0;}";
    DoeCompilerDiagnosticResultV18 result;
    DoeCompilerModuleEntryV13 entries[8];
    char names[256];
    DoeCompilerNoticeV18 notices[8];
    DoeCompilerDiagnosticOutputsV18 outputs={.metadata={.entries=entries,.entry_capacity=8,
        .entry_record_size=sizeof(*entries),.override_record_size=sizeof(DoeCompilerOverrideV13),
        .names=names,.name_capacity=sizeof(names)},.notices=notices,.notice_capacity=8,.notice_record_size=sizeof(*notices)};
    assert(validate(source,&outputs,&result)==DOE_COMPILER_OK);
    assert(result.published_notices==1);
    assert(notices[0].offset==(size_t)(strstr(source,"dpdx")-source));
    assert(notices[0].length==4 && notices[0].rule==DOE_COMPILER_RULE_DERIVATIVE_UNIFORMITY);
    memset(entries,0xa5,sizeof(entries));
    memset(names,0xa5,sizeof(names));
    memset(notices,0xa5,sizeof(notices));
    outputs.notice_capacity=0;
    assert(validate(source,&outputs,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.required_notices==1 && result.published_notices==0);
    assert(result.validation.required_entries==1 && result.validation.published_entries==0);
    assert(result.validation.required_is_exact);
    const unsigned char *entry_bytes=(const unsigned char *)entries;
    for(size_t i=0;i<sizeof(entries);++i) assert(entry_bytes[i]==0xa5);
    for(size_t i=0;i<sizeof(names);++i) assert((unsigned char)names[i]==0xa5);
    const unsigned char *notice_bytes=(const unsigned char *)notices;
    for(size_t i=0;i<sizeof(notices);++i) assert(notice_bytes[i]==0xa5);
    outputs.notice_capacity=8;
    outputs.metadata.name_capacity=0;
    assert(validate(source,&outputs,&result)==DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.required_notices==1 && result.published_notices==0);
    for(size_t i=0;i<sizeof(notices);++i) assert(notice_bytes[i]==0xa5);
    memset(&result,0xa5,sizeof(result));
    const DoeCompilerDiagnosticResultV18 before=result;
    assert(doe_compiler_validate_module_diagnostics(17,NULL,sizeof(DoeCompilerSpecializationCapacities),NULL,0,
        NULL,0,0,NULL,0,&result,sizeof(result))==DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&result,&before,sizeof(result)));
    outputs.notice_record_size=1;
    assert(doe_compiler_validate_module_diagnostics(18,NULL,sizeof(DoeCompilerSpecializationCapacities),NULL,0,
        NULL,0,0,&outputs,sizeof(outputs),&result,sizeof(result))==DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&result,&before,sizeof(result)));
}
static void mixed_and_pointer_rules(void)
{
    const char *source="diagnostic(warning,derivative_uniformity);diagnostic(info,subgroup_uniformity);enable subgroups; "
        "fn helper(p:ptr<function,f32>)->f32{return dpdx(*p);} "
        "@fragment fn frag(@location(0)x:f32)->@location(0) f32{var v=x;if(x>0.0){return helper(&v);}return 0.0;} "
        "@compute @workgroup_size(4) fn comp(@builtin(local_invocation_index)x:u32){if(x>0u){let y=subgroupAdd(x);}}";
    DoeCompilerModuleEntryV13 entries[8];
    char names[256];
    DoeCompilerNoticeV18 notices[8];
    DoeCompilerDiagnosticOutputsV18 outputs={.metadata={.entries=entries,.entry_capacity=8,
        .entry_record_size=sizeof(*entries),.override_record_size=sizeof(DoeCompilerOverrideV13),
        .names=names,.name_capacity=sizeof(names)},.notices=notices,.notice_capacity=8,.notice_record_size=sizeof(*notices)};
    DoeCompilerDiagnosticResultV18 result;
    DoeCompilerStatus status=validate(source,&outputs,&result);
    if(status!=DOE_COMPILER_OK) fprintf(stderr,"mixed rule failure: %s\n",result.validation.diagnostic.message);
    assert(status==DOE_COMPILER_OK && result.published_notices==2);
    unsigned mask=0;
    for(size_t i=0;i<2;++i){
        if(notices[i].rule==DOE_COMPILER_RULE_DERIVATIVE_UNIFORMITY){
            assert(notices[i].severity==DOE_COMPILER_DIAGNOSTIC_WARNING);
            assert(notices[i].offset==(size_t)(strstr(source,"dpdx")-source));
            mask|=1u;
        } else {
            assert(notices[i].rule==DOE_COMPILER_RULE_SUBGROUP_UNIFORMITY);
            assert(notices[i].severity==DOE_COMPILER_DIAGNOSTIC_INFO);
            assert(notices[i].offset==(size_t)(strstr(source,"subgroupAdd")-source));
            mask|=2u;
        }
    }
    assert(mask==3u);
    const char *unknown="diagnostic(off,not_known);diagnostic(info,another_rule);@compute @workgroup_size(1) fn main(){}";
    assert(validate(unknown,&outputs,&result)==DOE_COMPILER_OK && result.published_notices==1);
    assert(notices[0].severity==DOE_COMPILER_DIAGNOSTIC_WARNING && notices[0].rule==DOE_COMPILER_RULE_UNRECOGNIZED);
    assert(notices[0].offset==(size_t)(strstr(unknown,"not_known")-unknown));
}
static void previous_versions(void)
{
    const char source[]="@compute @workgroup_size(1) fn main(){}";
    uint32_t words[4096];
    DoeCompilerResult legacy;
    assert(doe_compiler_compile(1,source,sizeof(source)-1u,words,4096,&legacy,sizeof(legacy))==DOE_COMPILER_OK);
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_workspace_requirements(2,&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    DoeCompilerJobCapacities job={8192,2048,128,1024,8192,32,8};
    assert(doe_compiler_job_requirements(3,&job,sizeof(job),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    DoeCompilerSpecializationCapacities caps={.target=hlsl_capacities(),.overrides=32};
    caps.target.stages.module.typed.words=8192;
    assert(doe_compiler_typed_requirements(4,&caps.target.stages.module.typed,sizeof(caps.target.stages.module.typed),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_module_requirements(5,&caps.target.stages.module,sizeof(caps.target.stages.module),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_resource_requirements(6,&caps.target.stages.module,sizeof(caps.target.stages.module),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    for(uint32_t version=7;version<=9;++version)
        assert(doe_compiler_stage_requirements(version,&caps.target.stages,sizeof(caps.target.stages),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_text_requirements(10,DOE_COMPILER_TARGET_METAL,&caps.target,sizeof(caps.target),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_hlsl_requirements(11,&caps.target,sizeof(caps.target),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_metal_requirements(12,&caps.target,sizeof(caps.target),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_specialization_requirements(13,DOE_COMPILER_TARGET_SPIRV,&caps,sizeof(caps),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes);
    assert(workspace);
    DoeCompilerValidationResult validation;
    assert(doe_compiler_validate_module(13,&caps,sizeof(caps),workspace,requirements.size_bytes,
        source,sizeof(source)-1u,0,NULL,0,&validation,sizeof(validation))==DOE_COMPILER_OK);
    assert(validation.required_is_exact && validation.required_entries==1);
    free(workspace);
    assert(doe_compiler_module_specialization_requirements(14,DOE_COMPILER_TARGET_SPIRV,&caps,sizeof(caps),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    assert(doe_compiler_specialization_requirements(13,DOE_COMPILER_TARGET_METAL,&caps,sizeof(caps),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    workspace=malloc(requirements.size_bytes);
    assert(workspace);
    const char fragment[]="@fragment fn main()->@location(0) vec4f{return vec4f(1.0);}";
    const DoeCompilerMetalPipelineOptionsV15 options={UINT32_MAX};
    const DoeCompilerEntrySelectionV5 selection={.mode=DOE_COMPILER_SELECT_AUTO};
    DoeCompilerTextResult text;
    assert(doe_compiler_specialize_metal_pipeline(15,&options,sizeof(options),&caps,sizeof(caps),
        workspace,requirements.size_bytes,fragment,sizeof(fragment)-1u,&selection,sizeof(selection),
        NULL,0,sizeof(DoeCompilerOverrideValueV13),DOE_COMPILER_MODULE_WORDS|DOE_COMPILER_MODULE_MEASURE,
        NULL,0,&text,sizeof(text))==DOE_COMPILER_OK);
    free(workspace);
    DoeCompilerSamplingCapacitiesV16 sampling={.specialization=caps,.sampling_pairs=8};
    assert(doe_compiler_sampling_requirements(16,DOE_COMPILER_TARGET_SPIRV,&sampling,sizeof(sampling),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    DoeCompilerTranslationCapacitiesV17 translation={.sampling=sampling,.translation_entries=8};
    assert(doe_compiler_translation_requirements(17,DOE_COMPILER_TARGET_SPIRV,&translation,sizeof(translation),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    DoeCompilerWorkspaceRequirements sentinel={123,456};
    requirements=sentinel;
    assert(doe_compiler_translation_requirements(18,DOE_COMPILER_TARGET_SPIRV,&translation,sizeof(translation),&requirements,sizeof(requirements))==DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&requirements,&sentinel,sizeof(sentinel)));
}
static void grammar(void)
{
    const char *bad[]={
        "diagnostic(fatal,derivative_uniformity);",
        "diagnostic(off,derivative_uniformity);diagnostic(info,derivative_uniformity);",
        "diagnostic(off,vendor.rule);diagnostic(info,vendor . rule);",
        "diagnostic(off,vendor.rule.extra);",
        "diagnostic(off,);",
        "@compute @workgroup_size(1) fn first(){} diagnostic(off,derivative_uniformity);"};
    DoeCompilerDiagnosticResultV18 result;
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i){
        char source[1024];
        assert(snprintf(source,sizeof(source),"%s @compute @workgroup_size(1) fn main(){}",bad[i])>0);
        assert(validate(source,NULL,&result)!=DOE_COMPILER_OK);
    }
    assert(validate("diagnostic(off,new_rule,);diagnostic(off,new_rule);diagnostic(info,vendor.rule);"
        "@compute @workgroup_size(1) fn main(){}",NULL,&result)==DOE_COMPILER_OK);
    assert(result.required_notices==1);
}
int main(void)
{
    previous_versions();
    mixed_and_pointer_rules();
    exact_locations_and_atomicity();
    grammar();
    policy("derivative_uniformity",derivative);
    policy("subgroup_uniformity",subgroup);
    DoeCompilerDiagnosticResultV18 result;
    assert(validate("diagnostic(off,derivative_uniformity);diagnostic(off,subgroup_uniformity);@compute @workgroup_size(4) fn main(@builtin(local_invocation_index)x:u32){if(x>0u){workgroupBarrier();}}",NULL,&result)==DOE_COMPILER_SEMANTIC_ERROR);
    assert(validate("diagnostic(warning,new_rule); @compute @workgroup_size(1) fn main(){}",NULL,&result)==DOE_COMPILER_OK);
    assert(result.required_notices==1);
    assert(validate("diagnostic(off,new_rule); diagnostic(error,new_rule); @compute @workgroup_size(1) fn main(){}",NULL,&result)!=DOE_COMPILER_OK);
    assert(validate("diagnostic(off,derivative_uniformity); diagnostic(off,derivative_uniformity); @compute @workgroup_size(1) fn main(){}",NULL,&result)==DOE_COMPILER_OK);
    puts("diagnostic policy controls passed");
    return 0;
}
