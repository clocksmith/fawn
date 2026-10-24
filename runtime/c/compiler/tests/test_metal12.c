/* SPDX-License-Identifier: MIT */
#include "metal12_fixture.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static DoeCompilerStatus compile(const DoeCompilerTextCapacities *caps, void *workspace, size_t bytes,
    const char *source, const char *name, uint32_t flags, const DoeCompilerMetalOutputs *outputs,
    DoeCompilerTextResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {name ? DOE_COMPILER_SELECT_NAME : DOE_COMPILER_SELECT_NONE,
        0, name, name ? strlen(name) : 0};
    const int round = fegetround(), raised = fetestexcept(FE_ALL_EXCEPT);
    errno = EDOM;
    DoeCompilerStatus status = doe_compiler_compile_metal(12, caps, sizeof(*caps), workspace, bytes,
        source, strlen(source), &selection, sizeof(selection), flags | DOE_COMPILER_CORE_BINDING_NUMBERS, outputs,
        outputs ? sizeof(*outputs) : 0, result, sizeof(*result));
    assert(errno == EDOM && fegetround() == round && fetestexcept(FE_ALL_EXCEPT) == raised);
    return status;
}
static void require(DoeCompilerStatus status, const DoeCompilerTextResult *result) {
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%u: %s\n", (unsigned)status, result->stages.resources.module.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
}
static void append(char *source, size_t capacity, size_t *used, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int length = vsnprintf(source + *used, capacity - *used, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < capacity - *used);
    *used += (size_t)length;
}
static void namespace_limit(const DoeCompilerTextCapacities *caps, void *workspace, size_t bytes,
    uint32_t table, unsigned count, bool success) {
    char source[16384];
    size_t used = 0;
    append(source, sizeof(source), &used, "@group(3) @binding(999) var<storage,read_write> out:array<u32>;");
    if (table == DOE_COMPILER_TEXT_TABLE_SAMPLER)
        append(source, sizeof(source), &used, "@group(3) @binding(998) var tex:texture_2d<f32>;");
    for (unsigned i = 0; i < count; ++i)
        append(source, sizeof(source), &used, table == DOE_COMPILER_TEXT_TABLE_BUFFER ?
            "@group(1) @binding(%u) var<uniform> v%u:u32;" : table == DOE_COMPILER_TEXT_TABLE_TEXTURE ?
            "@group(1) @binding(%u) var v%u:texture_2d<f32>;" :
            "@group(1) @binding(%u) var v%u:sampler;", i, i);
    append(source, sizeof(source), &used, "@compute @workgroup_size(1) fn main(){out[0]=0u;");
    for (unsigned i = 0; i < count; ++i)
        append(source, sizeof(source), &used, table == DOE_COMPILER_TEXT_TABLE_BUFFER ? "out[0]+=v%u;" :
            table == DOE_COMPILER_TEXT_TABLE_TEXTURE ? "out[0]+=textureDimensions(v%u).x;" :
            "out[0]+=u32(textureSampleLevel(tex,v%u,vec2f(0.5),0.0).x);", i);
    append(source, sizeof(source), &used, "}");
    DoeCompilerTextResult result;
    DoeCompilerStatus status = compile(caps, workspace, bytes, source, NULL, 7, NULL, &result);
    if (success) require(status, &result);
    else {
        assert(status == DOE_COMPILER_UNSUPPORTED);
        assert(strstr(result.stages.resources.module.diagnostic.message, "namespace capacity"));
        assert(!result.published_text_bytes && !result.published_bindings && !result.stages.resources.module.module.schema_version);
    }
}
static void check_rows(const Metal12Fixture *fixture, const DoeCompilerTextResult *result) {
    assert(result->published_bindings == 5 && result->published_entries == 2);
    const DoeCompilerMetalEntryV12 *a = &fixture->entries[0], *b = &fixture->entries[1];
    assert(a->entry_index == 0 && a->first_binding == 0 && a->binding_count == 4 && a->buffer_count == 2);
    assert(b->entry_index == 1 && b->first_binding == 4 && b->binding_count == 1 && b->buffer_count == 1);
    assert(!strcmp(a->name, "first") && !strcmp(b->name, "second"));
    assert(a->sizes_slot == DOE_COMPILER_METAL_SIZES_SLOT && a->sizes_bytes == 8);
    assert(b->sizes_slot == DOE_COMPILER_METAL_SIZES_SLOT && b->sizes_bytes == 4);
    const uint32_t tables[] = {1,1,2,3,1}, slots[] = {0,1,0,0,0}, resources[] = {0,1,2,3,1};
    for (size_t i = 0; i < 5; ++i) {
        const DoeCompilerMetalBindingV12 *r = &fixture->bindings[i];
        assert(r->entry_index == (i < 4 ? 0u : 1u) && r->resource_index == resources[i]);
        assert(r->table == tables[i] && r->slot == slots[i]);
        assert(r->size_index == (tables[i] == 1 ? slots[i] : UINT32_MAX));
    }
    /* A global resource->slot map cannot satisfy these two entry contracts. */
    assert(fixture->bindings[1].resource_index == fixture->bindings[4].resource_index);
    assert(fixture->bindings[1].slot != fixture->bindings[4].slot);
}
static void core_binding_policy(void) {
    DoeCompilerTextCapacities caps = metal12_capacities();
    caps.stages.module.typed.words = 8192;
    caps.stages.module.typed.target_declarations = 2048;
    const char *source = "@group(3) @binding(999) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){data[0]=19u;}";
    DoeCompilerWorkspaceRequirements required;
    assert(doe_compiler_stage_requirements(9, &caps.stages, sizeof(caps.stages), &required, sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = malloc(required.size_bytes);
    HlslFixture *fixture = calloc(1, sizeof(*fixture));
    uint32_t words[8192];
    assert(workspace && fixture);
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(fixture);
    DoeCompilerStageOutputs stages = hlsl.stages;
    stages.resources.module.words = words;
    stages.resources.module.word_capacity = 8192;
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0, NULL, 0};
    DoeCompilerStageResult result;
    assert(doe_compiler_compile_stages(9, &caps.stages, sizeof(caps.stages), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 3, &stages, sizeof(stages), &result, sizeof(result)) == DOE_COMPILER_UNSUPPORTED);
    assert(doe_compiler_compile_stages(9, &caps.stages, sizeof(caps.stages), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 3 | DOE_COMPILER_CORE_BINDING_NUMBERS,
        &stages, sizeof(stages), &result, sizeof(result)) == DOE_COMPILER_OK);
    assert(fixture->resources[0].group == 3 && fixture->resources[0].binding == 999);
    assert(doe_compiler_compile_stages(9, &caps.stages, sizeof(caps.stages), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 3, &stages, sizeof(stages), &result, sizeof(result)) == DOE_COMPILER_UNSUPPORTED);
    free(workspace);
    assert(doe_compiler_hlsl_requirements(11, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_OK);
    workspace = malloc(required.size_bytes);
    assert(workspace);
    DoeCompilerTextResult text;
    assert(doe_compiler_compile_hlsl(11, &caps, sizeof(caps), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 3 | DOE_COMPILER_CORE_BINDING_NUMBERS,
        &hlsl, sizeof(hlsl), &text, sizeof(text)) == DOE_COMPILER_OK);
    assert(fixture->bindings[0].register_space == 3 && fixture->bindings[0].shader_register == 999);
    free(workspace);
    assert(doe_compiler_metal_requirements(12, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_OK);
    workspace = malloc(required.size_bytes);
    assert(workspace);
    assert(doe_compiler_compile_metal(12, &caps, sizeof(caps), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 7, NULL, 0, &text, sizeof(text)) == DOE_COMPILER_UNSUPPORTED);
    const char *bad[] = {
        "@group(3) @binding(1000) var<storage,read_write> data:array<u32>;@compute @workgroup_size(1) fn main(){data[0]=1u;}",
        "@group(4) @binding(999) var<storage,read_write> data:array<u32>;@compute @workgroup_size(1) fn main(){data[0]=1u;}"
    };
    for (size_t i = 0; i < 2; ++i)
        assert(doe_compiler_compile_metal(12, &caps, sizeof(caps), workspace, required.size_bytes,
            bad[i], strlen(bad[i]), &selection, sizeof(selection), 7 | DOE_COMPILER_CORE_BINDING_NUMBERS,
            NULL, 0, &text, sizeof(text)) == DOE_COMPILER_UNSUPPORTED);
    /* ABI10 remains explicitly fixed even when source numeric admission opts in. */
    assert(doe_compiler_compile_text(10, 1, &caps, sizeof(caps), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 7 | DOE_COMPILER_CORE_BINDING_NUMBERS,
        NULL, 0, &text, sizeof(text)) == DOE_COMPILER_UNSUPPORTED);
    free(workspace); free(fixture);
}
static void write_text(const char *directory, const char *name, const char *text, size_t bytes) {
    if (!directory) return;
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s.metal", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wbx");
    assert(file && fwrite(text, 1, bytes, file) == bytes && !fclose(file));
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    const DoeCompilerTextCapacities caps = metal12_capacities();
    DoeCompilerWorkspaceRequirements requirements, old;
    assert(doe_compiler_metal_requirements(12, &caps, sizeof(caps), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    assert(doe_compiler_text_requirements(10, 1, &caps, sizeof(caps), &old, sizeof(old)) == DOE_COMPILER_OK);
    size_t delta = caps.stages.module.entry_references * sizeof(DoeCompilerMetalBindingV12) -
        caps.stages.module.typed.resources * sizeof(DoeCompilerTextBindingV10) +
        caps.stages.module.entries * (sizeof(DoeCompilerMetalEntryV12) - sizeof(DoeCompilerTextEntryV10));
    assert(requirements.size_bytes == old.size_bytes + delta && requirements.alignment_bytes == old.alignment_bytes);
    void *workspace = malloc(requirements.size_bytes);
    Metal12Fixture *fixture = calloc(1, sizeof(*fixture)), *before = malloc(sizeof(*before));
    assert(workspace && fixture && before);
    DoeCompilerMetalOutputs outputs = metal12_outputs(fixture);
    DoeCompilerTextResult result, measured;
    fenv_t environment;
    assert(fegetenv(&environment) == 0 && fesetround(FE_DOWNWARD) == 0 && feraiseexcept(FE_DIVBYZERO) == 0);
    require(compile(&caps, workspace, requirements.size_bytes, metal12_mixed, NULL, 7, NULL, &measured), &measured);
    require(compile(&caps, workspace, requirements.size_bytes, metal12_mixed, NULL, 3, &outputs, &result), &result);
    assert(result.published_text_bytes == measured.required_text_bytes && result.published_bindings == measured.required_bindings);
    check_rows(fixture, &result);
    write_text(directory, "mixed", fixture->results.text, result.published_text_bytes);
    require(compile(&caps, workspace, requirements.size_bytes, metal12_mixed, "second", 3, &outputs, &result), &result);
    assert(result.published_bindings == 1 && fixture->bindings[0].entry_index == 1 && fixture->bindings[0].resource_index == 1);
    assert(fixture->bindings[0].slot == 0 && fixture->entries[0].sizes_bytes == 4);
    for (unsigned failure = 0; failure < 4; ++failure) {
        memset(fixture, 0x5a, sizeof(*fixture));
        memcpy(before, fixture, sizeof(*fixture));
        outputs = metal12_outputs(fixture);
        if (failure == 0) outputs.text_capacity = measured.required_text_bytes - 1;
        if (failure == 1) outputs.binding_capacity = measured.required_bindings - 1;
        if (failure == 2) outputs.entry_capacity = measured.required_entries - 1;
        DoeCompilerTextCapacities short_caps = caps;
        if (failure == 3) short_caps.text_bytes = 1;
        assert(compile(&short_caps, workspace, requirements.size_bytes, metal12_mixed, NULL, 3, &outputs, &result) != DOE_COMPILER_OK);
        assert(!memcmp(before, fixture, sizeof(*fixture)) && !result.published_text_bytes && !result.published_bindings);
    }
    outputs = metal12_outputs(fixture);
    require(compile(&caps, workspace, requirements.size_bytes, metal12_buffers, NULL, 3, &outputs, &result), &result);
    write_text(directory, "buffers", fixture->results.text, result.published_text_bytes);
    namespace_limit(&caps, workspace, requirements.size_bytes, 1, 29, true);
    namespace_limit(&caps, workspace, requirements.size_bytes, 1, 30, false);
    namespace_limit(&caps, workspace, requirements.size_bytes, 2, 31, true);
    namespace_limit(&caps, workspace, requirements.size_bytes, 2, 32, false);
    namespace_limit(&caps, workspace, requirements.size_bytes, 3, 16, true);
    namespace_limit(&caps, workspace, requirements.size_bytes, 3, 17, false);
    core_binding_policy();
    assert(fesetenv(&environment) == 0);
    free(before); free(fixture); free(workspace);
    puts("PASS Metal ABI12: sparse bindings, perentry conflicting closures, independent dense namespaces, exact tail sizes, limits, measure/untouched outputs and FP restoration");
}
