/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
static const char source[] =
    "fn put(a:ptr<function,u32>,b:ptr<function,u32>,c:ptr<function,u32>,d:ptr<function,u32>,"
    "e:ptr<function,u32>,f:ptr<function,u32>,g:ptr<function,u32>,h:ptr<function,u32>){"
    "*a=1u;*b=1u;*c=1u;*d=1u;*e=1u;*f=1u;*g=1u;*h=1u;workgroupBarrier();}"
    "@compute @workgroup_size(4)fn main(){var a=0u;var b=0u;var c=0u;var d=0u;"
    "var e=0u;var f=0u;var g=0u;var h=0u;put(&a,&b,&c,&d,&e,&f,&g,&h);}";
int main(void) {
    DoeCompilerStageCapacities caps = hlsl_capacities().stages;
    caps.module.typed.ir_instructions = 128u;
    caps.module.typed.words = 65536u;
    for (uint32_t attempt=0u;attempt<2u;++attempt) {
        DoeCompilerWorkspaceRequirements requirements;
        assert(doe_compiler_stage_requirements(8u,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
        void *workspace = malloc(requirements.size_bytes);
        HlslFixture *fixture = malloc(sizeof(*fixture));
        uint32_t *words = malloc(65536u*sizeof(*words));
        assert(workspace != NULL && fixture != NULL && words != NULL);
        memset(fixture,0x5a,sizeof(*fixture)); memset(words,0x5a,65536u*sizeof(*words));
        DoeCompilerStageOutputs outputs = hlsl_outputs(fixture).stages;
        outputs.resources.module.words = words; outputs.resources.module.word_capacity = 65536u;
        const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO,0u,NULL,0u};
        DoeCompilerStageResult result;
        errno = ERANGE;
        const DoeCompilerStatus status = doe_compiler_compile_stages(8u,&caps,sizeof(caps),workspace,requirements.size_bytes,
            source,strlen(source),&selection,sizeof(selection),3u,&outputs,sizeof(outputs),&result,sizeof(result));
        assert(errno == ERANGE);
        if (attempt == 0u) {
            assert(status == DOE_COMPILER_INPUT_ERROR);
            assert(strstr(result.resources.module.diagnostic.message,"pointer uniformity dependency scratch") != NULL);
            assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_IR);
            assert(result.resources.module.minimum_pool_capacity > caps.module.typed.ir_instructions);
            assert(result.resources.module.published.words == 0u && words[0] == 0x5a5a5a5au);
            assert(fixture->entries[0].stage == 0x5a5a5a5au);
            caps.module.typed.ir_instructions = result.resources.module.minimum_pool_capacity;
        } else assert(status == DOE_COMPILER_OK);
        free(words); free(fixture); free(workspace);
    }
    puts("pointer dependency scratch exact capacity, rollback and caller-state controls passed");
    return 0;
}
