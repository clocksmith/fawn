/* SPDX-License-Identifier: MIT */
#include "uniform_standard_fixture.h"
#include <stdio.h>

static void write_file(const char *directory, const char *name, const char *suffix, const void *data, size_t bytes) {
    if (!directory) return;
    char path[1024];
    int size = snprintf(path, sizeof(path), "%s/%s.%s", directory, name, suffix);
    assert(size > 0 && (size_t)size < sizeof(path));
    FILE *file = fopen(path, "wbx");
    assert(file && fwrite(data, 1, bytes, file) == bytes && !fclose(file));
}
static void require(DoeCompilerStatus status, const DoeCompilerDiagnostic *diagnostic) {
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%u: %s\n", (unsigned)status, diagnostic->message);
    assert(status == DOE_COMPILER_OK);
}
/* Tiny raw blocks have no internal prefix/tail branch. Every uniform pointer
 * must therefore be formed directly in the true body of its access bounds test. */
static void tiny_guarded_pointers(const uint32_t *words, size_t count) {
    uint32_t bound = words[3], uniform = 0, label = 0;
    assert(bound < 65536);
    uint32_t *conditions = calloc(bound, sizeof(*conditions));
    uint32_t *operations = calloc(bound, sizeof(*operations));
    assert(conditions && operations);
    unsigned pointers = 0;
    for (size_t j = 5; j < count; j += words[j] >> 16) {
        uint32_t op = words[j] & 65535u;
        assert((words[j] >> 16) > 0);
        if (op == 59 && words[j + 3] == 2) uniform = words[j + 2];
        if (op == 176 || op == 167) operations[words[j + 2]] = op;
        if (op == 250) conditions[words[j + 2]] = words[j + 1];
        if (op == 248) label = words[j + 1];
        if (op == 65 && words[j + 3] == uniform) {
            assert(conditions[label] && (operations[conditions[label]] == 176 || operations[conditions[label]] == 167));
            ++pointers;
        }
    }
    assert(pointers >= 4);
    free(operations); free(conditions);
}
static size_t wrong_offset(uint32_t *words, size_t count) {
    uint32_t bound = words[3], scalar = 0;
    assert(bound < 65536 && count + 4 < 65536);
    unsigned char *literal_kinds = calloc(bound, 1);
    assert(literal_kinds);
    size_t insertion = 0;
    unsigned changed = 0;
    for (size_t j = 5; j < count; j += words[j] >> 16) {
        uint32_t op = words[j] & 65535u;
        if (op == 43 && (words[j] >> 16) == 4) {
            if (words[j + 3] == 1) literal_kinds[words[j + 2]] = 1;
            if (words[j + 3] == 4) literal_kinds[words[j + 2]] = 2;
        }
        if (op == 54 && !insertion) insertion = j;
        if (op == 132 && !changed && literal_kinds[words[j + 4]] == 2 && literal_kinds[words[j + 3]] == 1) {
            scalar = words[j + 1];
            words[j + 4] = bound; /* Only the u[1] load uses byte offset8 instead of4. */
            ++changed;
        }
    }
    assert(changed == 1 && insertion && scalar);
    memmove(words + insertion + 4, words + insertion, (count - insertion) * sizeof(*words));
    words[insertion] = (4u << 16) | 43u;
    words[insertion + 1] = scalar;
    words[insertion + 2] = bound;
    words[insertion + 3] = 8;
    words[3] = bound + 1;
    free(literal_kinds);
    return count + 4;
}
static void text_targets(const char *directory, const uniform_case *test) {
    DoeCompilerTextCapacities caps = uniform_capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_text_requirements(10, 1, &caps, sizeof(caps), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    DoeCompilerWorkspaceRequirements hlsl_requirements;
    assert(doe_compiler_hlsl_requirements(11, &caps, sizeof(caps), &hlsl_requirements, sizeof(hlsl_requirements)) == DOE_COMPILER_OK);
    if (requirements.size_bytes < hlsl_requirements.size_bytes) requirements = hlsl_requirements;
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *storage = calloc(1, sizeof(*storage));
    assert(workspace && storage);
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(storage);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0, NULL, 0};
    DoeCompilerTextResult result;
    uint32_t flags = DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT;
    DoeCompilerStatus status = doe_compiler_compile_hlsl(11, &caps, sizeof(caps), workspace, requirements.size_bytes,
        test->source, strlen(test->source), &selection, sizeof(selection), flags, &hlsl, sizeof(hlsl), &result, sizeof(result));
    require(status, &result.stages.resources.module.diagnostic);
    assert(result.stages.resources.module.module.layout_profile == DOE_COMPILER_TYPED_PROFILE_STANDARD_UNIFORM);
    write_file(directory, test->name, "hlsl", storage->text, result.published_text_bytes);
    DoeCompilerTextBindingV10 bindings[16];
    DoeCompilerTextEntryV10 entries[8];
    DoeCompilerTextOutputs metal = {.stages=hlsl.stages,.text=storage->text,.text_capacity=sizeof(storage->text),
        .bindings=bindings,.binding_capacity=16,.binding_record_size=sizeof(bindings[0]),
        .entries=entries,.entry_capacity=8,.entry_record_size=sizeof(entries[0])};
    status = doe_compiler_compile_text(10, 1, &caps, sizeof(caps), workspace, requirements.size_bytes,
        test->source, strlen(test->source), &selection, sizeof(selection), flags, &metal, sizeof(metal), &result, sizeof(result));
    require(status, &result.stages.resources.module.diagnostic);
    write_file(directory, test->name, "metal", storage->text, result.published_text_bytes);
    free(storage);
    free(workspace);
}
static void invalid_cases(void *workspace, size_t bytes, HlslFixture *storage, uint32_t *words) {
    static const char *const bad[] = {
        "@group(0) @binding(0) var<uniform> u:array<u32>;@compute @workgroup_size(1) fn main(){}",
        "struct S{x:bool}@group(0) @binding(0) var<uniform> u:S;@compute @workgroup_size(1) fn main(){}",
        "struct S{x:atomic<u32>}@group(0) @binding(0) var<uniform> u:S;@compute @workgroup_size(1) fn main(){}",
        "struct S{@align(2) x:u32}@group(0) @binding(0) var<uniform> u:S;@compute @workgroup_size(1) fn main(){}",
        "enable uniform_buffer_standard_layout;@compute @workgroup_size(1) fn main(){}",
        "requires uniform_buffer_standard_layout,missing_feature;@compute @workgroup_size(1) fn main(){}"
    };
    HlslFixture *before = malloc(sizeof(*before));
    assert(before);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(storage, 0x5a, sizeof(*storage));
        memcpy(before, storage, sizeof(*storage));
        words[0] = UINT32_C(0x76543210);
        DoeCompilerStageResult result;
        assert(uniform_compile(workspace, bytes, bad[i], 11, storage, words, &result) != DOE_COMPILER_OK);
        assert(!memcmp(before, storage, sizeof(*storage)) && words[0] == UINT32_C(0x76543210));
        assert(!result.resources.module.published.words && !result.resources.module.module.schema_version);
    }
    DoeCompilerStageResult result;
    assert(uniform_compile(workspace, bytes, uniform_cases[0].source, 8, storage, words, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(uniform_compile(workspace, bytes, uniform_cases[0].source, 12, storage, words, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(uniform_compile(workspace, bytes, uniform_cases[0].source, 27, storage, words, &result) == DOE_COMPILER_INPUT_ERROR);
    free(before);
}
static void typed_adapter(void) {
    DoeCompilerTextCapacities caps = uniform_capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_typed_requirements(4, &caps.stages.module.typed, sizeof(caps.stages.module.typed),
        &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *storage = calloc(1, sizeof(*storage));
    uint32_t *words = malloc(65536 * sizeof(*words));
    assert(workspace && storage && words);
    const DoeCompilerTypedOutputs outputs = {.words=words,.word_capacity=65536,
        .types=storage->types,.type_capacity=128,.type_record_size=sizeof(storage->types[0]),
        .members=storage->members,.member_capacity=64,.member_record_size=sizeof(storage->members[0]),
        .resources=storage->resources,.resource_capacity=16,.resource_record_size=sizeof(storage->resources[0]),
        .names=storage->names,.name_capacity=sizeof(storage->names)};
    DoeCompilerTypedResult result;
    const char *source = uniform_cases[2].source;
    DoeCompilerStatus status = doe_compiler_compile_typed(4, &caps.stages.module.typed, sizeof(caps.stages.module.typed),
        workspace, requirements.size_bytes, source, strlen(source), 11, &outputs, sizeof(outputs), &result, sizeof(result));
    require(status, &result.diagnostic);
    assert(result.module.layout_profile == DOE_COMPILER_TYPED_PROFILE_STANDARD_UNIFORM);
    assert(storage->resources[0].minimum_binding_size_bytes == 12 && storage->resources[0].address_space_alignment_bytes == 4);
    status = doe_compiler_compile_typed(4, &caps.stages.module.typed, sizeof(caps.stages.module.typed),
        workspace, requirements.size_bytes, source, strlen(source), 3, &outputs, sizeof(outputs), &result, sizeof(result));
    assert(status != DOE_COMPILER_OK);
    free(words); free(storage); free(workspace);
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    DoeCompilerTextCapacities caps = uniform_capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_stage_requirements(9, &caps.stages, sizeof(caps.stages), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *storage = calloc(1, sizeof(*storage));
    uint32_t *words = malloc(65536 * sizeof(*words));
    assert(workspace && storage && words);
    for (size_t i = 0; i < sizeof(uniform_cases) / sizeof(uniform_cases[0]); ++i) {
        const uniform_case *test = &uniform_cases[i];
        DoeCompilerStageResult result, measured;
        assert(uniform_compile(workspace, requirements.size_bytes, test->source, 3, storage, words, &result) != DOE_COMPILER_OK);
        DoeCompilerStatus status = uniform_compile(workspace, requirements.size_bytes, test->source, 11, storage, words, &result);
        require(status, &result.resources.module.diagnostic);
        assert(storage->resources[0].minimum_binding_size_bytes == test->bytes);
        assert(storage->resources[0].address_space_alignment_bytes == (i >= 3 ? 8u : 4u));
        assert(result.resources.module.module.layout_profile == DOE_COMPILER_TYPED_PROFILE_STANDARD_UNIFORM);
        size_t word_count = result.resources.module.published.words;
        write_file(directory, test->name, "spv", words, word_count * sizeof(*words));
        if (i < 3) tiny_guarded_pointers(words, word_count);
        if (i == 2) {
            size_t mutant_count = wrong_offset(words, word_count);
            write_file(directory, "tail12-wrong-offset", "spv", words, mutant_count * sizeof(*words));
        }
        if (i == 3) {
            const uint64_t expected[] = {0, 12, 16, 20, 40, 44, 56};
            unsigned found = 0;
            for (size_t j = 0; j < result.resources.module.published.members; ++j) {
                DoeCompilerMemberV4 *member = &storage->members[j];
                if (member->name.length == 6 && !memcmp(storage->names + member->name.offset, "values", 6)) {
                    for (size_t k = 0; k < 7; ++k) assert(storage->members[j + k].offset_bytes == expected[k]);
                    ++found;
                }
            }
            assert(found == 1);
        }
        status = uniform_compile(workspace, requirements.size_bytes, test->source, 15, storage, words, &measured);
        require(status, &measured.resources.module.diagnostic);
        assert(measured.resources.module.required.words == word_count && !measured.resources.module.published.words);
        char required[4096];
        int length = snprintf(required, sizeof(required), "requires uniform_buffer_standard_layout;%s", test->source);
        assert(length > 0 && (size_t)length < sizeof(required));
        status = uniform_compile(workspace, requirements.size_bytes, required, 11, storage, words, &result);
        require(status, &result.resources.module.diagnostic);
        assert(uniform_compile(workspace, requirements.size_bytes, required, 3, storage, words, &result) == DOE_COMPILER_UNSUPPORTED);
        text_targets(directory, test);
    }
    invalid_cases(workspace, requirements.size_bytes, storage, words);
    typed_adapter();
    const char *legacy = "@group(0) @binding(0) var<uniform> u:vec4u;@compute @workgroup_size(1) fn main(){}";
    DoeCompilerStageResult result;
    assert(uniform_compile(workspace, requirements.size_bytes, legacy, 3, storage, words, &result) == DOE_COMPILER_OK);
    assert(result.resources.module.module.layout_profile == DOE_COMPILER_TYPED_PROFILE_CONSERVATIVE_UNIFORM);
    free(words); free(storage); free(workspace);
    puts("PASS explicit uniform standard layout: natural reflection, exact tail blocks, nested array/struct/attribute/matrix layout, requirement versus support, default rejection, measure and untouched failures; MSL/HLSL emitted");
}
