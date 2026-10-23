/* SPDX-License-Identifier: MIT
 * Ordinary source-only C++ consumer; exceptions and RTTI are disabled. */
#include "doe_compiler_job.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include "fixtures_m8.h"
static_assert(sizeof(DoeCompilerJobCapacities)==48u,"frozen x86-64 job layout");
static_assert(offsetof(DoeCompilerJobCapacities,expression_depth)==40u,"frozen depth offset");
static_assert(std::is_standard_layout<DoeCompilerResultV2>::value,"C-compatible result");
static_assert(DOE_COMPILER_OK==0 && DOE_COMPILER_INPUT_ERROR==1 && DOE_COMPILER_PARSE_ERROR==2 && DOE_COMPILER_UNSUPPORTED==3 && DOE_COMPILER_SEMANTIC_ERROR==4 && DOE_COMPILER_OUTPUT_TOO_SMALL==5 && DOE_COMPILER_ABI_MISMATCH==6,"frozen status identities");
int main() {
    (void)numeric_expected; (void)rejected_numeric_source; (void)rejected_parse_source; (void)retained_source;
    const DoeCompilerJobCapacities caps{65536u,8192u,1024u,4096u,65536u,64u,32u};
    DoeCompilerWorkspaceRequirements req{};
    if (doe_compiler_job_requirements(3u,&caps,sizeof(caps),&req,sizeof(req))!=DOE_COMPILER_OK || req.alignment_bytes>alignof(std::max_align_t)) return 1;
    void *workspace=std::malloc(req.size_bytes);
    if (workspace==nullptr) return 2;
    std::uint32_t words[4096]; DoeCompilerResultV2 result{};
    std::memset(words,0xa5,sizeof(words));
    auto status=doe_compiler_compile_job(3u,&caps,sizeof(caps),workspace,req.size_bytes,numeric_source,sizeof(numeric_source)-1u,words,4096u,&result,sizeof(result));
    std::free(workspace);
    if (status!=DOE_COMPILER_OK) { std::fprintf(stderr,"native C++ status=%d diagnostic=%s\n",static_cast<int>(status),result.diagnostic.message); return 3; }
    if (result.word_count<=5u || words[0]!=0x07230203u || result.reflection.binding_count!=1u) return 4;
    std::puts("PASS strict C++17 numeric public-API consumer without exceptions or RTTI");
    return 0;
}
