/* SPDX-License-Identifier: MIT */
#include "doe_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 const char source[]="@group(0) @binding(0) var<storage,read_write> data: array<f32>; @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3u) { let i=gid.x; let saved: f32=data[i]; data[i]=0.1; if (i==0u) { let v=max(saved*2.0, 1.0); data[i]=v-0.5; return; } data[i]=saved; }";
 const char bad[]="@group(0) @binding(0) var<storage,read_write> data: array<f32>; @compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid: vec3u) { data[gid.x]=data[gid.x]*(16777217.0-16777216.0); }";
 uint32_t words[4096],copy[4096];DoeCompilerResult result;
 assert(doe_compiler_compile(1,source,sizeof(source)-1,words,4096,&result,sizeof(result))==DOE_COMPILER_OK);
 assert(result.word_count>0 && result.word_count<4096);size_t count=result.word_count;assert(result.reflection.binding_count==1);assert(result.reflection.bindings[0].scalar==DOE_COMPILER_F32);
 memcpy(copy,words,sizeof(words));
 assert(doe_compiler_compile(1,source,sizeof(source)-1,words,count,&result,sizeof(result))==DOE_COMPILER_OK);assert(memcmp(copy,words,count*4)==0);
 for(size_t i=0;i<4096;i++)words[i]=0xa55a36c9;
 assert(doe_compiler_compile(1,bad,sizeof(bad)-1,words,4096,&result,sizeof(result))!=DOE_COMPILER_OK);
 assert(result.word_count==0);for(size_t i=0;i<4096;i++)assert(words[i]==0xa55a36c9);
 puts("Native C/C++ M4 header/ABI/typed local/control/decimal source consumption passed.");return 0;
}
