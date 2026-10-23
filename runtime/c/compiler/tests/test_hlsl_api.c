/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static DoeCompilerStatus compile(const DoeCompilerTextCapacities *caps, void *workspace, size_t bytes,
    const char *source, const char *name, uint32_t flags, const DoeCompilerHlslOutputs *outputs,
    DoeCompilerTextResult *result) {
    DoeCompilerEntrySelectionV5 selection = {name ? DOE_COMPILER_SELECT_NAME : DOE_COMPILER_SELECT_NONE,
        0, name, name ? strlen(name) : 0};
    DoeCompilerStatus status = doe_compiler_compile_hlsl(11u, caps, sizeof(*caps), workspace, bytes,
        source, strlen(source), &selection, sizeof(selection), flags, outputs,
        outputs ? sizeof(*outputs) : 0, result, sizeof(*result));
    if (status != DOE_COMPILER_OK && status != DOE_COMPILER_OUTPUT_TOO_SMALL)
        fprintf(stderr, "status=%u: %s\n", (unsigned)status, result->stages.resources.module.diagnostic.message);
    return status;
}
static void unpublished(const HlslFixture *before, const HlslFixture *after,
    const DoeCompilerTextResult *result) {
    assert(memcmp(before, after, sizeof(*after)) == 0);
    assert(result->published_text_bytes == 0 && result->published_bindings == 0 && result->published_entries == 0);
    assert(result->stages.resources.module.module.schema_version == 0);
    assert(result->stages.resources.module.published.entries == 0);
}
static FILE *fixture_file(const char *directory, const char *name, const char *suffix) {
    char path[1024];
    int size = snprintf(path, sizeof(path), "%s/%s.%s", directory, name, suffix);
    assert(size > 0 && (size_t)size < sizeof(path));
    FILE *file = fopen(path, "wbx");
    assert(file != NULL); /* Optional fixture output never replaces old evidence. */
    return file;
}
static void write_fixture(const char *directory, const char *name,
    const DoeCompilerHlslOutputs *outputs, const DoeCompilerTextResult *result) {
    if (directory == NULL) return;
    FILE *file = fixture_file(directory, name, "hlsl");
    assert(fwrite(outputs->text, 1, result->published_text_bytes, file) == result->published_text_bytes);
    assert(fclose(file) == 0);
    file = fixture_file(directory, name, "json");
    assert(fprintf(file, "{\"bindings\":[") > 0);
    for (size_t i = 0; i < result->published_bindings; ++i) {
        const DoeCompilerHlslBindingV11 *r = &outputs->bindings[i];
        assert(fprintf(file, "%s{\"resource\":%" PRIu32 ",\"kind\":%" PRIu32
            ",\"register\":%" PRIu32 ",\"space\":%" PRIu32 ",\"stride\":%" PRIu32 "}",
            i ? "," : "", r->resource_index, r->register_kind, r->shader_register, r->register_space, r->stride) > 0);
    }
    assert(fprintf(file, "],\"entries\":[") > 0);
    for (size_t i = 0; i < result->published_entries; ++i) {
        const DoeCompilerHlslEntryV11 *r = &outputs->entries[i];
        assert(fprintf(file, "%s{\"index\":%" PRIu32 ",\"name\":\"%s\",\"flags\":%" PRIu32
            ",\"dispatchRegister\":%" PRIu32 ",\"dispatchSpace\":%" PRIu32 ",\"dispatchBytes\":%" PRIu32 "}",
            i ? "," : "", r->entry_index, r->name, r->flags, r->dispatch_register, r->dispatch_space, r->dispatch_bytes) > 0);
    }
    assert(fprintf(file, "]}\n") > 0 && fclose(file) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    DoeCompilerTextCapacities caps = hlsl_capacities();
    DoeCompilerWorkspaceRequirements required, saved_requirements;
    assert(doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = malloc(required.size_bytes);
    HlslFixture *storage = malloc(sizeof(*storage)), *saved = malloc(sizeof(*saved));
    assert(workspace && storage && saved);
    DoeCompilerHlslOutputs outputs = hlsl_outputs(storage);
    DoeCompilerTextResult result, measured;
    errno = ERANGE;
    assert(fesetround(FE_DOWNWARD) == 0 && feraiseexcept(FE_INVALID) == 0);
    assert(compile(&caps, workspace, required.size_bytes, hlsl_source, NULL, 7u, NULL, &measured) == DOE_COMPILER_OK);
    assert(errno == ERANGE && fegetround() == FE_DOWNWARD && (fetestexcept(FE_INVALID) & FE_INVALID));
    assert(measured.required_text_bytes && measured.required_bindings == 6 && measured.required_entries == 3);
    assert(measured.stages.resources.module.required.words == 0 && measured.stages.resources.module.required_is_exact);
    assert(compile(&caps, workspace, required.size_bytes, hlsl_source, NULL, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.target == DOE_COMPILER_TARGET_HLSL && result.language_version == 600);
    assert(result.published_text_bytes == measured.required_text_bytes && result.published_bindings == 6);
    assert(result.stages.resources.module.module.schema_version == 9 && result.stages.resources.module.published.resources == 7);
    static const uint32_t kinds[] = {DOE_COMPILER_HLSL_REGISTER_UAV, DOE_COMPILER_HLSL_REGISTER_SRV,
        DOE_COMPILER_HLSL_REGISTER_CBV, DOE_COMPILER_HLSL_REGISTER_SRV,
        DOE_COMPILER_HLSL_REGISTER_SAMPLER, DOE_COMPILER_HLSL_REGISTER_UAV};
    static const uint32_t slots[] = {5, 9, 2, 4, 7, 10}, spaces[] = {3, 2, 1, 0, 0, 2};
    for (size_t i = 0; i < 6; ++i) {
        assert(storage->bindings[i].resource_index == i && storage->bindings[i].register_kind == kinds[i]);
        assert(storage->bindings[i].shader_register == slots[i] && storage->bindings[i].register_space == spaces[i]);
        assert(storage->bindings[i].stride == (i < 2 ? 4u : 0u));
    }
    assert(!storage->target_entries[0].flags && storage->target_entries[0].dispatch_register == UINT32_MAX);
    assert(storage->target_entries[0].dispatch_space == UINT32_MAX && !storage->target_entries[0].dispatch_bytes);
    assert(storage->target_entries[1].entry_index == 1 && !strcmp(storage->target_entries[1].name, "selected"));
    assert(storage->target_entries[1].flags == DOE_COMPILER_HLSL_ENTRY_WAVES);
    assert(storage->target_entries[1].dispatch_register == 0 && storage->target_entries[1].dispatch_space == 7);
    assert(storage->target_entries[1].dispatch_bytes == 16);
    assert(!storage->target_entries[2].flags && !storage->target_entries[2].dispatch_bytes);
    assert(storage->target_entries[2].dispatch_register == UINT32_MAX && !strcmp(storage->target_entries[2].name, "last"));
    write_fixture(directory, "bundle", &outputs, &result);
    storage->text[result.published_text_bytes] = 0;
    assert(strstr(storage->text, "#if DOE_HLSL_ENTRY == 1") && strstr(storage->text, "register(b0, space7)"));
    assert(strstr(storage->text, "register(u5, space3)") && strstr(storage->text, "register(t9, space2)"));
    assert(strstr(storage->text, "register(b2, space1)") && strstr(storage->text, "register(s7, space0)"));
    assert(compile(&caps, workspace, required.size_bytes, hlsl_source, "first", 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.published_entries == 1 && result.published_bindings == 1);
    assert(!storage->target_entries[0].flags && !strcmp(storage->target_entries[0].name, "first"));
    write_fixture(directory, "first", &outputs, &result);
    assert(compile(&caps, workspace, required.size_bytes, hlsl_source, "selected", 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.published_entries == 1 && storage->target_entries[0].entry_index == 1);
    assert(storage->target_entries[0].flags == DOE_COMPILER_HLSL_ENTRY_WAVES);
    assert(result.stages.resources.module.module.selected_entry == 1);
    write_fixture(directory, "selected", &outputs, &result);
    storage->text[result.published_text_bytes] = 0;
    assert(!strstr(storage->text, "DOE_HLSL_ENTRY") && strstr(storage->text, "void selected("));

    memset(storage, 0xa5, sizeof(*storage));
    memcpy(saved, storage, sizeof(*saved));
    for (unsigned field = 0; field < 5; ++field) {
        DoeCompilerHlslOutputs small = outputs;
        if (field == 0) small.text_capacity = measured.required_text_bytes - 1;
        else if (field == 1) small.binding_capacity = 5;
        else if (field == 2) small.entry_capacity = 2;
        else if (field == 3) small.stages.resources.module.name_capacity = 0;
        else small.stages.resources.module.entry_reference_capacity = 0;
        assert(compile(&caps, workspace, required.size_bytes, hlsl_source, NULL, 3u, &small, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(result.stages.resources.module.required_is_exact);
        unpublished(saved, storage, &result);
    }
    DoeCompilerTextCapacities tiny = caps;
    tiny.text_bytes = measured.required_text_bytes - 1;
    assert(compile(&tiny, workspace, required.size_bytes, hlsl_source, NULL, 3u, &outputs, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.stages.resources.module.exhausted_pool == DOE_COMPILER_POOL_TEXT_BYTES);
    unpublished(saved, storage, &result);
    tiny.text_bytes = 0;
    DoeCompilerHlslOutputs reflection = outputs;
    reflection.text = NULL;
    reflection.text_capacity = 0;
    assert(compile(&tiny, workspace, required.size_bytes, hlsl_source, NULL, 2u, &reflection, &result) == DOE_COMPILER_OK);
    assert(!result.published_text_bytes && result.published_entries == 3);
    outputs = hlsl_outputs(NULL);
    outputs.text = storage->text;
    outputs.text_capacity = sizeof(storage->text);
    assert(compile(&caps, workspace, required.size_bytes, hlsl_source, "selected", 1u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.published_text_bytes && !result.published_bindings && !result.published_entries);
    outputs = hlsl_outputs(storage);
    memset(storage, 0xa5, sizeof(*storage));
    memcpy(saved, storage, sizeof(*saved));
    assert(compile(&caps, workspace, required.size_bytes, "@compute @workgroup_size(1) fn doe_reserved(){}", NULL,
        3u, &outputs, &result) == DOE_COMPILER_UNSUPPORTED);
    assert(result.stages.resources.module.diagnostic.offset > 0);
    unpublished(saved, storage, &result);
    assert(compile(&caps, workspace, required.size_bytes, "@compute @workgroup_size(1) fn valid(){} fn bad(){missing();}",
        "valid", 3u, &outputs, &result) != DOE_COMPILER_OK);
    unpublished(saved, storage, &result);
    assert(compile(&caps, workspace, required.size_bytes, "const value=1u;", NULL, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(!result.published_text_bytes && !result.published_bindings && !result.published_entries);
    assert(fegetround() == FE_DOWNWARD && (fetestexcept(FE_INVALID) & FE_INVALID));
    assert(fesetround(FE_TONEAREST) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);

    memset(&result, 0xa5, sizeof(result));
    DoeCompilerTextResult old_result = result;
    DoeCompilerEntrySelectionV5 selection = {0};
    outputs.binding_record_size--;
    assert(doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, required.size_bytes,
        hlsl_source, sizeof(hlsl_source)-1, &selection, sizeof(selection), 3u,
        &outputs, sizeof(outputs), &result, sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&result, &old_result, sizeof(result)) == 0);
    saved_requirements = required;
    assert(doe_compiler_hlsl_requirements(10u, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&required, &saved_requirements, sizeof(required)));
    caps.text_bytes = SIZE_MAX;
    assert(doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &required, sizeof(required)) == DOE_COMPILER_INPUT_ERROR);
    assert(!memcmp(&required, &saved_requirements, sizeof(required)));
    free(saved);
    free(storage);
    free(workspace);
    puts("ABI11 HLSL typed publication, metadata, sizing, selection and failure controls passed");
    return 0;
}
