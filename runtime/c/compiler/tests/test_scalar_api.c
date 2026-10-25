/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _GNU_SOURCE
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <fenv.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

static bool compiling;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __wrap_free(void *pointer);
void *__wrap_malloc(size_t size) { assert(!compiling); return __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { assert(!compiling); return __real_calloc(count, size); }
void *__wrap_realloc(void *pointer, size_t size) { assert(!compiling); return __real_realloc(pointer, size); }
void __wrap_free(void *pointer) { assert(!compiling); __real_free(pointer); }

static const char valid[] = "const N=3; @compute @workgroup_size(N) fn main(){const a=0.1+0.2;let b:f32=a;let n=i32(b);}";
static const char invalid[] = "@compute @workgroup_size(1) fn main(){let x=bitcast<f32>(0x7fc00000u);}";
static const char skipped[] = "@compute @workgroup_size(1) fn main(){const x=false&&(10i<i32(5*1000*1000*1000));}";

static DoeCompilerStatus compile_mode(unsigned mode, const DoeCompilerJobCapacities *caps,
    void *workspace, size_t size, const char *source, uint32_t *words, size_t count) {
    DoeCompilerResult one;
    DoeCompilerResultV2 two;
    DoeCompilerStatus result;
    compiling = true;
    if (mode == 0u) result = doe_compiler_compile(1u, source, strlen(source), words, count, &one, sizeof(one));
    else if (mode == 1u) result = doe_compiler_compile_v2(2u, workspace, size, source, strlen(source), words, count, &two, sizeof(two));
    else result = doe_compiler_compile_job(3u, caps, sizeof(*caps), workspace, size, source, strlen(source), words, count, &two, sizeof(two));
    compiling = false;
    if (result != DOE_COMPILER_OK) {
        assert((mode == 0u ? one.word_count : two.word_count) == 0u);
        for (size_t i = 0; i < 4096u; ++i) assert(words[i] == UINT32_C(0xa5a5a5a5));
    }
    return result;
}
int main(void) {
    const DoeCompilerJobCapacities caps = { .source_bytes=65536u, .tokens=8192u, .statements=128u,
        .expressions=1024u, .words=4096u, .expression_depth=32u, .control_depth=16u };
    DoeCompilerWorkspaceRequirements fixed, job;
    assert(doe_compiler_workspace_requirements(2u,&fixed,sizeof(fixed)) == DOE_COMPILER_OK);
    /* The M10/M13 owner ledger superseded M8's 77760-byte layout. */
    assert(fixed.size_bytes <= 147456u);
    assert(doe_compiler_job_requirements(3u,&caps,sizeof(caps),&job,sizeof(job)) == DOE_COMPILER_OK);
    const size_t size = fixed.size_bytes > job.size_bytes ? fixed.size_bytes : job.size_bytes;
    void *workspace = malloc(size); assert(workspace != NULL);
    uint32_t words[4096];
    memset(words, 0xa5, sizeof(words));
    assert(compile_mode(1u, &caps, workspace, fixed.size_bytes - 1u, valid, words, 4096u) == DOE_COMPILER_INPUT_ERROR);
    assert(compile_mode(1u, &caps, workspace, fixed.size_bytes, valid, words, 4096u) == DOE_COMPILER_OK);
    fenv_t original; assert(fegetenv(&original)==0);
    const uint32_t original_sse = _mm_getcsr();
    const int rounds[] = {FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const char *const sources[] = {valid,invalid,skipped,valid};
    const DoeCompilerStatus expected[] = {DOE_COMPILER_OK,DOE_COMPILER_SEMANTIC_ERROR,DOE_COMPILER_OK,DOE_COMPILER_OUTPUT_TOO_SMALL};
    for (unsigned mode=0u; mode<3u; ++mode) for (size_t round=0;round<4u;++round) {
        assert(fedisableexcept(FE_ALL_EXCEPT)>=0); assert(feclearexcept(FE_ALL_EXCEPT)==0);
        assert(feraiseexcept(FE_INEXACT|FE_UNDERFLOW)==0); assert(fesetround(rounds[round])==0);
        assert(feenableexcept(FE_DIVBYZERO|FE_INVALID|FE_OVERFLOW)>=0);
        _mm_setcsr(_mm_getcsr()|UINT32_C(0x8040));
        const int flags=fetestexcept(FE_ALL_EXCEPT), traps=fegetexcept(); const uint32_t sse=_mm_getcsr();
        for(size_t i=0;i<4u;++i){
            memset(words,0xa5,sizeof(words));
            assert(compile_mode(mode,&caps,workspace,size,sources[i],words,i==3u?0u:4096u)==expected[i]);
            assert(fegetround()==rounds[round] && fetestexcept(FE_ALL_EXCEPT)==flags);
            assert(fegetexcept()==traps && _mm_getcsr()==sse);
        }
    }
    assert(fesetenv(&original)==0); _mm_setcsr(original_sse);
    DoeCompilerJobCapacities tiny=caps; tiny.expressions=0u; tiny.statements=0u;
    assert(doe_compiler_job_requirements(3u,&tiny,sizeof(tiny),&job,sizeof(job))==DOE_COMPILER_OK);
    memset(words,0xa5,sizeof(words));
    assert(compile_mode(2u,&tiny,workspace,size,"const N:u32=1u;@compute @workgroup_size(1) fn main(){}",words,4096u)==DOE_COMPILER_OK);
    memset(words,0xa5,sizeof(words));
    assert(compile_mode(2u,&tiny,workspace,size,"const N=1;@compute @workgroup_size(1) fn main(){}",words,4096u)==DOE_COMPILER_INPUT_ERROR);
    tiny.expressions=1u;
    memset(words,0xa5,sizeof(words));
    assert(compile_mode(2u,&tiny,workspace,size,"const N:u32=1u;const M=2;@compute @workgroup_size(1) fn main(){}",words,4096u)==DOE_COMPILER_OK);
    memset(words,0xa5,sizeof(words));
    assert(compile_mode(2u,&tiny,workspace,size,"const N:u32=1u;const M=2;const P=3;@compute @workgroup_size(1) fn main(){}",words,4096u)==DOE_COMPILER_INPUT_ERROR);
    free(workspace);
    puts("M8 scalar API/fenv/no-allocation/budget checks passed");
    return 0;
}
