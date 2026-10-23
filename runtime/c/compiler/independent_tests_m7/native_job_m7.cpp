/* SPDX-License-Identifier: MIT
 * Independent ordinary C++ public-job consumer; implementation remains C. */
#include "doe_compiler_job.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

static_assert(std::is_standard_layout<DoeCompilerJobCapacities>::value, "job standard layout");
static_assert(std::is_standard_layout<DoeCompilerResultV2>::value, "result standard layout");
static_assert(DOE_COMPILER_JOB_ABI_VERSION == 3u, "job ABI");
static_assert(DOE_COMPILER_JOB_MAX_EXPRESSION_DEPTH == 64u, "expression envelope");
static_assert(DOE_COMPILER_JOB_MAX_CONTROL_DEPTH == 32u, "control envelope");

int main() {
    const DoeCompilerJobCapacities caps{65536u,8192u,1024u,4096u,65536u,32u,16u};
    DoeCompilerWorkspaceRequirements req{};
    if (doe_compiler_job_requirements(3u,&caps,sizeof(caps),&req,sizeof(req)) != DOE_COMPILER_OK) return 1;
    if (req.alignment_bytes > alignof(std::max_align_t)) return 2;
    void *workspace = std::malloc(req.size_bytes);
    if (workspace == nullptr) return 3;
    const char source[] = "@compute @workgroup_size(1) fn main(){var x:u32;while x<3u{x++;}return;}";
    std::uint32_t words[4096];
    std::memset(words,0xA5,sizeof(words));
    DoeCompilerResultV2 result{};
    const auto status = doe_compiler_compile_job(3u,&caps,sizeof(caps),workspace,req.size_bytes,source,sizeof(source)-1u,words,4096u,&result,sizeof(result));
    std::free(workspace);
    if (status != DOE_COMPILER_OK || result.word_count <= 5u || words[0] != 0x07230203u) return 4;
    std::printf("{\"status\":\"PASS\",\"sizeof_capacities\":%zu,\"alignof_capacities\":%zu,\"sizeof_result\":%zu,\"sizeof_requirements\":%zu,\"offsetof_expression_depth\":%zu}\n",sizeof(caps),alignof(DoeCompilerJobCapacities),sizeof(result),sizeof(req),offsetof(DoeCompilerJobCapacities,expression_depth));
    return 0;
}
