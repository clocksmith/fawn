/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "geometric_fixture.h"
#include "../include/doe_compiler_metal.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

static void require(DoeCompilerStatus status, const DoeCompilerDiagnostic *diagnostic) {
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%u: %s\n", (unsigned)status, diagnostic->message);
    assert(status == DOE_COMPILER_OK);
}
static void save(const char *directory, const char *name, const void *data, size_t bytes) {
    if (!directory) return;
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, bytes, file) == bytes && !fclose(file));
}

int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.stages.module.typed.words = 8192;
    caps.stages.module.typed.target_declarations = 2048;
    HlslFixture *fixture = calloc(1, sizeof(*fixture));
    assert(fixture);
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(fixture);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO,0,NULL,0};
    DoeCompilerWorkspaceRequirements required;
    assert(doe_compiler_stage_requirements(9, &caps.stages, sizeof(caps.stages), &required, sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = malloc(required.size_bytes);
    uint32_t words[8192];
    assert(workspace);
    DoeCompilerStageOutputs stages = hlsl.stages;
    stages.resources.module.words = words;
    stages.resources.module.word_capacity = 8192;
    DoeCompilerStageResult stage_result;
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(feraiseexcept(FE_DIVBYZERO) == 0);
    const int raised = fetestexcept(FE_ALL_EXCEPT);
    errno = EDOM;
    DoeCompilerStatus status = doe_compiler_compile_stages(9, &caps.stages, sizeof(caps.stages), workspace,
        required.size_bytes, geometric_source, sizeof(geometric_source)-1, &selection, sizeof(selection), 3,
        &stages, sizeof(stages), &stage_result, sizeof(stage_result));
    require(status, &stage_result.resources.module.diagnostic);
    assert(errno == EDOM && fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == raised);
    save(directory, "geometric.spv", words, stage_result.resources.module.published.words * sizeof(*words));
    free(workspace);

    DoeCompilerTextResult result;
    for (unsigned target = 0; target < 2; ++target) {
        DoeCompilerMetalBindingV12 bindings[16];
        DoeCompilerMetalEntryV12 entries[8];
        DoeCompilerMetalOutputs metal = {.stages=hlsl.stages,.text=fixture->text,.text_capacity=sizeof(fixture->text),
            .bindings=bindings,.binding_capacity=16,.binding_record_size=sizeof(*bindings),
            .entries=entries,.entry_capacity=8,.entry_record_size=sizeof(*entries)};
        status = target ? doe_compiler_metal_requirements(12, &caps, sizeof(caps), &required, sizeof(required)) :
            doe_compiler_hlsl_requirements(11, &caps, sizeof(caps), &required, sizeof(required));
        assert(status == DOE_COMPILER_OK);
        workspace = malloc(required.size_bytes);
        assert(workspace);
        errno = EDOM;
        status = target ? doe_compiler_compile_metal(12, &caps, sizeof(caps), workspace, required.size_bytes,
            geometric_source, sizeof(geometric_source)-1, &selection, sizeof(selection), 3,
            &metal, sizeof(metal), &result, sizeof(result)) :
            doe_compiler_compile_hlsl(11, &caps, sizeof(caps), workspace, required.size_bytes,
            geometric_source, sizeof(geometric_source)-1, &selection, sizeof(selection), 3,
            &hlsl, sizeof(hlsl), &result, sizeof(result));
        require(status, &result.stages.resources.module.diagnostic);
        assert(result.published_bindings == 1 && result.published_entries == 1);
        assert(result.published_text_bytes < sizeof(fixture->text));
        fixture->text[result.published_text_bytes] = 0;
        assert(strstr(fixture->text, "faceforward(") && strstr(fixture->text, "refract(") && errno == EDOM && fegetround() == FE_DOWNWARD &&
            fetestexcept(FE_ALL_EXCEPT) == raised);
        save(directory, target ? "geometric.metal" : "geometric.hlsl", fixture->text, result.published_text_bytes);
        free(workspace);
    }
    assert(fesetround(FE_TONEAREST) == 0);
    free(fixture);
    puts("PASS geometric: shared public SPIR-V/MSL/HLSL compilation, faceForward/refract mixed shapes, total internal reflection, owned entry/resource metadata and FP/errno preservation; no physical qualification");
    return 0;
}
