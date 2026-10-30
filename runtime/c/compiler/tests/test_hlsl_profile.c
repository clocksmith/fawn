/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_hlsl_profile.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void dump_certified(const char *source, const HlslFixture *fixture, const DoeCompilerTextResult *result)
{
    const char *directory = getenv("DOE_C_HLSL_PROFILE_DUMP_DIR");
    if (!directory || !directory[0]) return;
    /* Only a fixture filename; the SDK receipt hashes the exact emitted bytes.
     * Refuse collisions/existing files rather than overwrite another case. */
    uint64_t id = UINT64_C(14695981039346656037);
    for (const unsigned char *p=(const unsigned char *)source;*p;++p)
        id = (id^*p)*UINT64_C(1099511628211);
    char path[1024];
    int n = snprintf(path,sizeof(path),"%s/%016" PRIx64 ".hlsl",directory,id);
    assert(n > 0 && (size_t)n < sizeof(path));
    FILE *prior = fopen(path,"rb");
    if (prior) { fclose(prior); fputs("profile fixture filename already exists\n",stderr); abort(); }
    FILE *file = fopen(path,"wb");
    assert(file && fwrite(fixture->text,1,result->published_text_bytes,file) == result->published_text_bytes);
    assert(fclose(file) == 0);
    n = snprintf(path,sizeof(path),"%s/%016" PRIx64 ".entry",directory,id);
    assert(n > 0 && (size_t)n < sizeof(path));
    file = fopen(path,"wb");
    assert(file && fputs(fixture->target_entries[0].name,file) >= 0 && fclose(file) == 0);
}

static void run(const char *source, const char *name, uint32_t model, uint32_t stage)
{
    const DoeCompilerTranslationCapacitiesV17 caps = {{{hlsl_capacities(),32u},0u},8u};
    DoeCompilerWorkspaceRequirements legacy, required;
    assert(doe_compiler_translation_requirements(17u,DOE_COMPILER_TARGET_HLSL,
        &caps,sizeof(caps),&legacy,sizeof(legacy)) == DOE_COMPILER_OK);
    assert(doe_compiler_hlsl_profile_requirements(19u,&caps,sizeof(caps),
        &required,sizeof(required)) == DOE_COMPILER_OK);
    assert(required.size_bytes == legacy.size_bytes && required.alignment_bytes == legacy.alignment_bytes);
    void *workspace = malloc(required.size_bytes);
    HlslFixture *first = calloc(1,sizeof(*first)), *second = calloc(1,sizeof(*second));
    assert(workspace && first && second);
    DoeCompilerTranslationEntryV17 entries[8];
    DoeCompilerTranslationOutputsV17 outputs = {.entries=entries,.entry_capacity=8u,
        .entry_record_size=sizeof(*entries),.sampling={.pair_record_size=sizeof(DoeCompilerSamplingPairV16)}};
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME,0u,name,strlen(name)};
    const DoeCompilerSamplingOptionsV16 options = {UINT32_MAX,0u};
    DoeCompilerHlslProfileResultV19 measured, published;
    errno = ERANGE;
    DoeCompilerStatus status = doe_compiler_specialize_hlsl_profile(19u,&options,sizeof(options),
        &caps,sizeof(caps),workspace,required.size_bytes,source,strlen(source),&selection,sizeof(selection),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),7u,NULL,0u,&measured,sizeof(measured));
    if (status != DOE_COMPILER_OK) fprintf(stderr,"profile measure: %s\n",
        measured.translation.sampling.target.stages.resources.module.diagnostic.message);
    assert(status == DOE_COMPILER_OK && errno == ERANGE);
    if (measured.profile.shader_model != model || measured.profile.stage != stage)
        fprintf(stderr,"profile expected model%u/stage%u got model%u/stage%u: %s\n",
            model,stage,measured.profile.shader_model,measured.profile.stage,source);
    assert(measured.profile.shader_model == model && measured.profile.stage == stage);
    assert(measured.profile.entry_index == 0u && measured.profile.reserved == 0u);
    outputs.sampling.target.hlsl = hlsl_outputs(first);
    status = doe_compiler_specialize_hlsl_profile(19u,&options,sizeof(options),&caps,sizeof(caps),
        workspace,required.size_bytes,source,strlen(source),&selection,sizeof(selection),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u,&outputs,sizeof(outputs),&published,sizeof(published));
    assert(status == DOE_COMPILER_OK && errno == ERANGE);
    assert(memcmp(&published.profile,&measured.profile,sizeof(measured.profile)) == 0);
    assert(published.translation.sampling.target.language_version == 600u);
    outputs.sampling.target.hlsl = hlsl_outputs(second);
    DoeCompilerTranslationResultV17 old;
    assert(doe_compiler_specialize_translation(17u,DOE_COMPILER_TARGET_HLSL,&options,sizeof(options),
        &caps,sizeof(caps),workspace,required.size_bytes,source,strlen(source),&selection,sizeof(selection),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u,&outputs,sizeof(outputs),&old,sizeof(old)) == DOE_COMPILER_OK);
    assert(old.sampling.target.published_text_bytes == published.translation.sampling.target.published_text_bytes);
    assert(memcmp(first->text,second->text,old.sampling.target.published_text_bytes) == 0);
    assert(memcmp(first->target_entries,second->target_entries,
        old.sampling.target.published_entries*sizeof(*first->target_entries)) == 0);
    for (size_t i=0u;i<old.published_entries;++i) assert(entries[i].flags == 0u && entries[i].reserved == 0u);
    memset(&published,0x5a,sizeof(published));
    assert(doe_compiler_specialize_hlsl_profile(17u,&options,sizeof(options),&caps,sizeof(caps),
        workspace,required.size_bytes,source,strlen(source),&selection,sizeof(selection),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u,&outputs,sizeof(outputs),&published,sizeof(published)) == DOE_COMPILER_ABI_MISMATCH);
    const unsigned char *bytes = (const unsigned char *)&published;
    for (size_t i=0u;i<sizeof(published);++i) assert(bytes[i] == 0x5au);
    memset(second,0x5a,sizeof(*second));
    outputs.sampling.target.hlsl.text_capacity = 0u;
    assert(doe_compiler_specialize_hlsl_profile(19u,&options,sizeof(options),&caps,sizeof(caps),
        workspace,required.size_bytes,source,strlen(source),&selection,sizeof(selection),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u,&outputs,sizeof(outputs),&published,sizeof(published)) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    bytes = (const unsigned char *)second;
    for (size_t i=0u;i<sizeof(*second);++i) assert(bytes[i] == 0x5au);
    assert(published.profile.shader_model == 0u && published.translation.published_entries == 0u);
    if (model == 501u) dump_certified(source,first,&old.sampling.target);
    free(second); free(first); free(workspace);
}

int main(void)
{
    run("struct P{n:u32,pad:vec3u} @group(0)@binding(0)var<uniform>p:P;"
        "@group(0)@binding(1)var<storage,read>a:array<f32>;"
        "@group(0)@binding(2)var<storage,read_write>b:array<f32>;"
        "@compute @workgroup_size(64)fn main(@builtin(global_invocation_id)g:vec3u){"
        "let i=g.x;if(i>=p.n){return;}b[i]=a[i]*1.5+0.25;}","main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "fn f(x:u32)->u32{var y=x;for(var i=0u;i<3u;i++){y=(y<<1u)^i;}return y;}"
        "@compute @workgroup_size(1)fn main(){b[0]=f(arrayLength(&b));}","main",600u,DOE_COMPILER_STAGE_COMPUTE);
    run("struct S{x:array<vec2f,2>,y:i32} const C=S(array(vec2f(1),vec2f(2)),3);"
        "@group(0)@binding(0)var<storage,read_write>b:array<S>;"
        "fn f(s:S)->S{var v=s;v.y=v.y+(-1);return v;}"
        "@compute @workgroup_size(1)fn main(){b[0]=f(C);}","main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<i32>;"
        "fn f(p:ptr<function,i32>){*p=(*p>>1)^7;}"
        "@compute @workgroup_size(1)fn main(){var x=b[1];f(&x);"
        "switch(x){case 0:{x=1;}case 1:{x=2;}default:{x=3;}}"
        "b[0]=select(x,-x,x<0);}","main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "@compute @workgroup_size(1)fn main(){let x=b[1];"
        "b[0]=u32(f32(x))+bitcast<u32>(f32(i32(x)));}","main",600u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "@compute @workgroup_size(1)fn main(){b[0]=arrayLength(&b);}","main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<i32>;"
        "@compute @workgroup_size(1)fn main(){let x=b[1];let y=b[2];b[0]=x/y+x%y;}",
        "main",600u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "@compute @workgroup_size(1)fn main(){let x=b[1];let y=b[2];b[0]=x/y+x%y;}",
        "main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<f32>;"
        "@compute @workgroup_size(1)fn main(){let x=b[1];let y=b[2];b[0]=x/y+x%y;}",
        "main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "@compute @workgroup_size(1)fn unused(@builtin(global_invocation_id)a:vec3u,"
        "@builtin(local_invocation_id)c:vec3u,@builtin(workgroup_id)d:vec3u,"
        "@builtin(num_workgroups)e:vec3u,@builtin(local_invocation_index)f:u32){b[0]=a.x+c.x+d.x+e.x+f;}"
        "@compute @workgroup_size(1)fn main(@builtin(global_invocation_id)g:vec3u){b[0]=g.x;}",
        "main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("enable subgroups;@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "@compute @workgroup_size(1)fn main(@builtin(subgroup_size)n:u32){b[0]=n;}","main",600u,DOE_COMPILER_STAGE_COMPUTE);
    run("enable subgroups;@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "fn f()->u32{return subgroupAdd(1u);}"
        "@compute @workgroup_size(1)fn main(){b[0]=f();}","main",600u,DOE_COMPILER_STAGE_COMPUTE);
    run("enable f16;struct Half{x:f16} @group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "fn unused()->Half{return Half(1h);}"
        "@compute @workgroup_size(1)fn main(){b[0]=1u;}","main",620u,DOE_COMPILER_STAGE_COMPUTE);
    run("enable f16;@group(0)@binding(0)var<storage,read_write>b:array<u32>;"
        "fn unused()->f16{return 1h;}"
        "@compute @workgroup_size(1)fn main(){b[0]=1u;}","main",501u,DOE_COMPILER_STAGE_COMPUTE);
    run("@group(0)@binding(0)var<storage,read_write>b:array<f32>;"
        "@compute @workgroup_size(1)fn main(){b[0]=abs(b[1]);}","main",600u,DOE_COMPILER_STAGE_COMPUTE);
    run("@vertex fn main()->@builtin(position)vec4f{return vec4f(0,0,0,1);}","main",600u,DOE_COMPILER_STAGE_VERTEX);
    puts("PASS explicit HLSL profile ABI19: closed typed subset, emitted unused half helpers, pruned functions, legacy bytes/workspace, measure/fill and failure publication");
    return 0;
}
