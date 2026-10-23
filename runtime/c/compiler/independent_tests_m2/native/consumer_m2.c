#include "doe_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const char source[] =
      "@group(3) @binding(31) var<storage,read> left:array<f32>;"
      "@group(1) @binding(17) var<storage,read> right:array<f32>;"
      "@group(0) @binding(3) var<storage,read_write> dst:array<f32>;"
      "@compute @workgroup_size(1) fn consumer(@builtin(global_invocation_id) gid:vec3u)"
      "{let idx=gid.x;if (idx == 0u) { dst[idx]=left[idx]; return; } dst[idx]=left[idx-1u]+right[idx];}";
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    DoeCompilerStatus status=doe_compiler_compile(DOE_COMPILER_ABI_VERSION,source,sizeof(source)-1,words,DOE_COMPILER_MAX_WORDS,&result,sizeof(result));
    assert(status==DOE_COMPILER_OK);
    assert(result.word_count>0 && result.word_count<=DOE_COMPILER_MAX_WORDS);
    assert(words[0]==0x07230203 && words[1]==0x00010300);
    assert(strcmp(result.reflection.entry_point,"consumer")==0);
    assert(result.reflection.binding_count==3);
    assert(result.reflection.bindings[0].group==3 && result.reflection.bindings[0].binding==31);
    assert(result.reflection.bindings[0].scalar==DOE_COMPILER_F32 && result.reflection.bindings[0].access==DOE_COMPILER_READ);
    assert(result.reflection.bindings[2].access==DOE_COMPILER_READ_WRITE);
    puts("ordinary consumer build/load/call: PASS");
    return 0;
}
