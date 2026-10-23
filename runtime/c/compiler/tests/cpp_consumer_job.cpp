/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_job.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
int main() {
    const DoeCompilerJobCapacities capacities = {4096,1024,128,256,4096,18,8};
    DoeCompilerWorkspaceRequirements requirements{};
    assert(doe_compiler_job_requirements(DOE_COMPILER_JOB_ABI_VERSION, &capacities,
        sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = std::malloc(requirements.size_bytes);
    auto *result = static_cast<DoeCompilerResultV2 *>(std::malloc(sizeof(DoeCompilerResultV2)));
    auto *words = static_cast<uint32_t *>(std::malloc(capacities.words * sizeof(uint32_t)));
    assert(workspace && result && words);
    const char *source = "@compute @workgroup_size(1) fn f(){var x:u32;while x<3u{x++;}}";
    assert(doe_compiler_compile_job(DOE_COMPILER_JOB_ABI_VERSION, &capacities, sizeof(capacities),
        workspace, requirements.size_bytes, source, std::strlen(source), words, capacities.words,
        result, sizeof(*result)) == DOE_COMPILER_OK);
    assert(result->word_count > 5 && result->reflection.binding_count == 0);
    std::free(words); std::free(result); std::free(workspace);
    std::puts("C++17 job consumer: header, shared exports and loop-capable job call passed");
}
