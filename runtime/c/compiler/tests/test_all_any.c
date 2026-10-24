/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_metal.h"
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

static char *read_source(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0 && length < 8192 && fseek(file, 0, SEEK_SET) == 0);
    char *bytes = malloc((size_t)length + 1u);
    assert(bytes != NULL && fread(bytes, 1u, (size_t)length, file) == (size_t)length && fclose(file) == 0);
    bytes[length] = '\0';
    return bytes;
}
static void artifact(const char *directory, const char *name, const void *bytes, size_t size) {
    if (directory == NULL) return;
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wbx");
    assert(file != NULL && fwrite(bytes, 1u, size, file) == size && fclose(file) == 0);
}
static DoeCompilerStatus compile(uint32_t target, const char *source, bool measure,
    HlslFixture *storage, uint32_t *words, DoeCompilerTextResult *result, size_t operand_capacity) {
    const int saved_errno = errno, rounding = fegetround(), exceptions = fetestexcept(FE_ALL_EXCEPT);
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.stages.module.typed.words = 65536u;
    if (operand_capacity != SIZE_MAX) caps.stages.module.typed.ir_operands = operand_capacity;
    caps.stages.module.typed.target_declarations = 8192u;
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status;
    if (target == 0u) status = doe_compiler_stage_requirements(9u, &caps.stages, sizeof(caps.stages),
        &requirements, sizeof(requirements));
    else if (target == 1u) status = doe_compiler_metal_requirements(12u, &caps, sizeof(caps),
        &requirements, sizeof(requirements));
    else status = doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &requirements, sizeof(requirements));
    assert(status == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0u, NULL, 0u};
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(storage);
    DoeCompilerMetalBindingV12 bindings[64];
    DoeCompilerMetalEntryV12 entries[8];
    DoeCompilerMetalOutputs metal = {hlsl.stages,hlsl.text,hlsl.text_capacity,
        bindings,64u,sizeof(bindings[0]),entries,8u,sizeof(entries[0])};
    const uint32_t flags = 3u | (measure ? 4u : 0u);
    memset(result, 0, sizeof(*result));
    if (target == 0u) {
        hlsl.stages.resources.module.words = words;
        hlsl.stages.resources.module.word_capacity = 65536u;
        status = doe_compiler_compile_stages(9u, &caps.stages, sizeof(caps.stages), workspace,
            requirements.size_bytes, source, strlen(source), &selection, sizeof(selection), flags,
            measure ? NULL : &hlsl.stages, measure ? 0u : sizeof(hlsl.stages), &result->stages, sizeof(result->stages));
    } else if (target == 1u) status = doe_compiler_compile_metal(12u, &caps, sizeof(caps), workspace,
        requirements.size_bytes, source, strlen(source), &selection, sizeof(selection), flags,
        measure ? NULL : &metal, measure ? 0u : sizeof(metal), result, sizeof(*result));
    else status = doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, strlen(source), &selection, sizeof(selection), flags,
        measure ? NULL : &hlsl, measure ? 0u : sizeof(hlsl), result, sizeof(*result));
    free(workspace);
    assert(fegetround() == rounding && fetestexcept(FE_ALL_EXCEPT) == exceptions && errno == saved_errno);
    return status;
}
static void expect(const char *source, bool success, uint64_t minimum, const char *directory, const char *name) {
    HlslFixture *storage = malloc(sizeof(*storage));
    uint32_t *words = malloc(65536u * sizeof(*words));
    assert(storage != NULL && words != NULL);
    for (uint32_t target = 0u; target < 3u; ++target) {
        memset(storage,0x5a,sizeof(*storage));
        memset(words,0x5a,65536u*sizeof(*words));
        DoeCompilerTextResult result;
        const DoeCompilerStatus status = compile(target,source,false,storage,words,&result,SIZE_MAX);
        if ((status==DOE_COMPILER_OK)!=success)
            fprintf(stderr,"target%u offset%zu %s\n%s\n",target,result.stages.resources.module.diagnostic.offset,
                result.stages.resources.module.diagnostic.message,source);
        assert((status==DOE_COMPILER_OK)==success);
        if (!success) {
            for (size_t i=0u;i<sizeof(*storage);++i) assert(((unsigned char *)storage)[i]==0x5a);
            for (size_t i=0u;i<65536u;++i) assert(words[i]==UINT32_C(0x5a5a5a5a));
            continue;
        }
        if (minimum) assert(storage->resources[0].minimum_binding_size_bytes==minimum);
        if (target==0u && name!=NULL) {
            size_t calls=0u,alls=0u,anys=0u;
            for (size_t at=5u;at<result.stages.resources.module.published.words;) {
                const uint32_t width=words[at]>>16u,opcode=words[at]&65535u;
                assert(width && width<=result.stages.resources.module.published.words-at);
                if (opcode==57u) ++calls;
                if (opcode==154u) ++anys;
                if (opcode==155u) ++alls;
                assert(opcode!=11u); /* No floating math import for logical reductions. */
                at+=width;
            }
            assert(alls && anys);
            if (strcmp(name,"cases")==0) assert(calls==11u);
        }
        DoeCompilerTextResult measured;
        assert(compile(target,source,true,storage,words,&measured,SIZE_MAX)==DOE_COMPILER_OK);
        assert(result.stages.resources.module.required.words==measured.stages.resources.module.required.words);
        assert(result.required_text_bytes==measured.required_text_bytes);
        if (name!=NULL) {
            char filename[64];
            assert(snprintf(filename,sizeof(filename),"%s.%s",name,target==0u ? "spv" : target==1u ? "metal" : "hlsl")>0);
            artifact(directory,filename,target==0u ? (const void *)words : (const void *)storage->text,
                target==0u ? result.stages.resources.module.published.words*sizeof(*words) : result.published_text_bytes);
        }
    }
    free(words);
    free(storage);
}
int main(int argc,char **argv) {
    assert(argc==1 || argc==2 || argc==4);
    fenv_t caller;
    assert(fegetenv(&caller)==0 && fesetround(FE_DOWNWARD)==0);
    assert(feraiseexcept(FE_INVALID|FE_DIVBYZERO)==0);
    errno=EDOM;
    const char *directory=argc>=2 ? argv[1] : NULL;
    char *retained=read_source(argc==4 ? argv[2] : "tests/all_any_retained.wgsl");
    char *cases=read_source(argc==4 ? argv[3] : "tests/all_any_cases.wgsl");
    expect(retained,true,0u,directory,"retained");
    expect(cases,true,0u,directory,"cases");
    for (uint32_t width=2u;width<=4u;++width) for (uint32_t mask=0u;mask<(1u<<width);++mask) {
        char lanes[64]={0},source[1024];
        size_t cursor=0u;
        for (uint32_t i=0u;i<width;++i) {
            const int size=snprintf(lanes+cursor,sizeof(lanes)-cursor,"%s%s",i ? "," : "",(mask&(1u<<i)) ? "true" : "false");
            assert(size>0 && (size_t)size<sizeof(lanes)-cursor);
            cursor+=(size_t)size;
        }
        assert(snprintf(source,sizeof(source),"const a=all(vec%u<bool>(%s));const b=any(vec%u<bool>(%s));"
            "@group(0) @binding(0) var<storage,read> witness:array<u32,select(1u,2u,a)+select(0u,4u,b)>;"
            "@compute @workgroup_size(1) fn main(){_=witness[0];}",width,lanes,width,lanes)>0);
        const uint32_t elements=(mask==(1u<<width)-1u ? 2u : 1u)+(mask ? 4u : 0u);
        expect(source,true,4u*elements,NULL,NULL);
    }
    expect("const a=all(true);const b=any(false);@group(0) @binding(0) var<storage,read> witness:array<u32,select(1u,2u,a)+select(0u,4u,b)>;"
        "@compute @workgroup_size(1) fn main(){_=witness[0];}",true,8u,NULL,NULL);
    expect("const a=all(vec2<bool>());const b=any(vec3<bool>());@group(0) @binding(0) var<storage,read> witness:array<u32,select(1u,2u,a)+select(0u,4u,b)>;"
        "@compute @workgroup_size(1) fn main(){_=witness[0];}",true,4u,NULL,NULL);
    const char *bad[]={"all(1u)","any(1i)","all(0.0)","all(vec2u(1))","any(vec3f(1))", "all(array(true,false))",
        "any(true,false)","all()","min(true,false)","any(mat2x2f())"};
    for (size_t i=0u;i<sizeof(bad)/sizeof(bad[0]);++i) {
        char source[512];
        assert(snprintf(source,sizeof(source),"@compute @workgroup_size(1) fn main(){_=%s;}",bad[i])>0);
        expect(source,false,0u,NULL,NULL);
    }
    expect("fn all(x:u32)->u32{return x;}alias any=u32;@compute @workgroup_size(1) fn main(){_=all(any(7u));}",true,0u,NULL,NULL);
    expect("@compute @workgroup_size(1) fn main(){const b=all(vec2<bool>(false,1u/0u>0u));}",false,0u,NULL,NULL);
    free(retained);
    free(cases);
    assert(fesetenv(&caller)==0);
    puts("all/any all-target bool signatures, exhaustive constant truth tables/layout, eager snapshots, OOB and FP/failure controls passed");
    return 0;
}
