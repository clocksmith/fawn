/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static const char source[] =
    "@group(0)@binding(0)var<storage,read_write>o:array<u32>;"
    "fn leaf(p:ptr<function,u32>){*p=7u;}"
    "fn relay(p:ptr<function,u32>){leaf(p);}"
    "@compute @workgroup_size(1)fn main(){var a:array<u32,4>;let p=&a[o[0]];relay(p);leaf(p);o[1]=a[2];}";
int main(void) {
    bool saw_specialized_capacity = false, succeeded = false;
    for (uint32_t instructions = 8u; instructions <= 128u && !succeeded; ++instructions) {
        DoeCompilerTextCapacities all = hlsl_capacities();
        DoeCompilerStageCapacities caps = all.stages;
        caps.module.typed.ir_instructions = instructions;
        caps.module.typed.words = 65536u;
        caps.module.typed.target_declarations = 4096u;
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
        DoeCompilerStatus status = doe_compiler_compile_stages(8u,&caps,sizeof(caps),workspace,requirements.size_bytes,
            source,strlen(source),&selection,sizeof(selection),3u,&outputs,sizeof(outputs),&result,sizeof(result));
        assert(errno == ERANGE);
        if (status != DOE_COMPILER_OK) {
            assert(result.resources.module.published.words == 0u);
            assert(words[0] == 0x5a5a5a5au && fixture->entries[0].stage == 0x5a5a5a5au);
            if (strstr(result.resources.module.diagnostic.message,"specialized function instruction capacity") != NULL) {
                assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_IR);
                assert(result.resources.module.minimum_pool_capacity > instructions);
                saw_specialized_capacity = true;
            }
        } else {
            succeeded = true;
            assert(fixture->entries[0].stage == DOE_COMPILER_STAGE_COMPUTE);
            for (size_t at=5u; at<result.resources.module.published.words; at+=words[at]>>16)
                if ((words[at]&65535u) == 17u) assert(words[at+1u] != 4441u && words[at+1u] != 4442u);
        }
        free(words); free(fixture); free(workspace);
    }
    assert(saw_specialized_capacity && succeeded);
    puts("pointer call specialization capacity rollback, caller state and whole-root SPIR-V controls passed");
}
