/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_text.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char text[65536], names[4096];
    DoeCompilerTypeV4 types[128];
    DoeCompilerMemberV4 members[64];
    DoeCompilerResourceV4 resources[16];
    DoeCompilerWorkgroupV5 workgroups[8];
    DoeCompilerEntryV5 entries[8];
    DoeCompilerHandleV6 handles[16];
    DoeCompilerStageInterfaceV7 interfaces[64];
    uint32_t references[64];
    DoeCompilerTextBindingV10 bindings[16];
    DoeCompilerTextEntryV10 target_entries[8];
} Outputs;
static const DoeCompilerTextCapacities capacity = {
    .stages = {.module = {.typed = {.source_bytes = 16384u, .tokens = 4096u, .statements = 128u,
        .expressions = 1024u, .types = 128u, .members = 64u, .resources = 16u, .access_steps = 256u,
        .constants = 256u, .constant_edges = 512u, .ir_instructions = 2048u, .ir_operands = 2048u,
        .target_declarations = 0u, .traversal_items = 128u, .words = 0u,
        .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u},
        .entries = 8u, .entry_inputs = 32u, .entry_references = 64u},
        .interface_attributes = 64u, .stage_interfaces = 64u}, .text_bytes = 65536u
};
static const char source[] =
    "struct O{@builtin(position) position:vec4f,@location(0) color:vec4f}"
    "@group(1) @binding(2) var<storage,read_write> out:array<u32>;"
    "@group(1) @binding(14) var tex:texture_2d<f32>;"
    "@group(0) @binding(15) var smp:sampler;"
    "@group(3) @binding(0) var<storage,read> unused:array<u32>;"
    "@compute @workgroup_size(1) fn run(){"
    "out[0]=textureDimensions(tex).x+u32(textureSampleLevel(tex,smp,vec2f(0.5f),0.0f).x);}"
    "@vertex fn vertex(@location(0) p:vec4f)->O{return O(p,vec4f(1.0f));}"
    "@fragment fn fragment(o:O)->@location(0) vec4f{return o.color;}";
static DoeCompilerTextOutputs spans(Outputs *o) {
    return (DoeCompilerTextOutputs){.stages = {.resources = {.module = {
        .types = o->types, .type_capacity = 128u, .type_record_size = sizeof(*o->types),
        .members = o->members, .member_capacity = 64u, .member_record_size = sizeof(*o->members),
        .resources = o->resources, .resource_capacity = 16u, .resource_record_size = sizeof(*o->resources),
        .workgroups = o->workgroups, .workgroup_capacity = 8u, .workgroup_record_size = sizeof(*o->workgroups),
        .entries = o->entries, .entry_capacity = 8u, .entry_record_size = sizeof(*o->entries),
        .entry_references = o->references, .entry_reference_capacity = 64u,
        .names = o->names, .name_capacity = sizeof(o->names)},
        .handles = o->handles, .handle_capacity = 16u, .handle_record_size = sizeof(*o->handles)},
        .interfaces = o->interfaces, .interface_capacity = 64u, .interface_record_size = sizeof(*o->interfaces)},
        .text = o->text, .text_capacity = sizeof(o->text),
        .bindings = o->bindings, .binding_capacity = 16u, .binding_record_size = sizeof(*o->bindings),
        .entries = o->target_entries, .entry_capacity = 8u, .entry_record_size = sizeof(*o->target_entries)};
}
static DoeCompilerTextOutputs empty_spans(void) {
    DoeCompilerTextOutputs out = {0};
    out.stages.resources.module.type_record_size = sizeof(DoeCompilerTypeV4);
    out.stages.resources.module.member_record_size = sizeof(DoeCompilerMemberV4);
    out.stages.resources.module.resource_record_size = sizeof(DoeCompilerResourceV4);
    out.stages.resources.module.workgroup_record_size = sizeof(DoeCompilerWorkgroupV5);
    out.stages.resources.module.entry_record_size = sizeof(DoeCompilerEntryV5);
    out.stages.resources.handle_record_size = sizeof(DoeCompilerHandleV6);
    out.stages.interface_record_size = sizeof(DoeCompilerStageInterfaceV7);
    out.binding_record_size = sizeof(DoeCompilerTextBindingV10);
    out.entry_record_size = sizeof(DoeCompilerTextEntryV10);
    return out;
}
static DoeCompilerStatus compile(const DoeCompilerTextCapacities *caps, void *workspace, size_t bytes,
    const char *text, const char *name, uint32_t flags, const DoeCompilerTextOutputs *outputs,
    DoeCompilerTextResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {name == NULL ? DOE_COMPILER_SELECT_NONE :
        DOE_COMPILER_SELECT_NAME, 0u, name, name == NULL ? 0u : strlen(name)};
    const DoeCompilerStatus status = doe_compiler_compile_text(10u, DOE_COMPILER_TARGET_METAL,
        caps, sizeof(*caps), workspace, bytes, text, strlen(text), &selection, sizeof(selection), flags,
        outputs, outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
    if (status != DOE_COMPILER_OK) fprintf(stderr, "status=%u: %s\n", (unsigned)status,
        result->stages.resources.module.diagnostic.message);
    return status;
}
static void unchanged(const Outputs *before, const Outputs *after, const DoeCompilerTextResult *result) {
    assert(memcmp(before, after, sizeof(*after)) == 0);
    assert(result->published_text_bytes == 0u && result->published_bindings == 0u && result->published_entries == 0u);
    assert(result->stages.resources.module.module.schema_version == 0u);
    assert(result->stages.resources.module.published.words == 0u);
}
int main(int argc, char **argv) {
    DoeCompilerWorkspaceRequirements requirements, old;
    assert(doe_compiler_text_requirements(10u, 1u, &capacity, sizeof(capacity), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    assert(doe_compiler_stage_requirements(9u, &capacity.stages, sizeof(capacity.stages), &old, sizeof(old)) == DOE_COMPILER_OK);
    assert(requirements.size_bytes > old.size_bytes + capacity.text_bytes);
    void *workspace = malloc(requirements.size_bytes);
    Outputs *out = malloc(sizeof(*out)), *saved = malloc(sizeof(*saved));
    assert(workspace != NULL && out != NULL && saved != NULL);
    DoeCompilerTextOutputs outputs = spans(out);
    DoeCompilerTextResult result, measured;
    errno = ERANGE;
    assert(fesetround(FE_DOWNWARD) == 0 && feraiseexcept(FE_INVALID) == 0);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 7u, NULL, &measured) == DOE_COMPILER_OK);
    assert(errno == ERANGE && fegetround() == FE_DOWNWARD && (fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(measured.stages.resources.module.required_is_exact == 1u && measured.required_text_bytes > 0u);
    assert(measured.required_bindings == 3u && measured.required_entries == 3u);
    assert(measured.stages.resources.module.required.words == 0u);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.target == DOE_COMPILER_TARGET_METAL && result.language_version == 300u);
    assert(result.published_text_bytes == measured.required_text_bytes && result.published_bindings == 3u);
    assert(result.stages.resources.module.module.schema_version == 9u && result.stages.resources.module.required.resources == 4u);
    assert(out->bindings[0].resource_index == 0u && out->bindings[0].table == DOE_COMPILER_TEXT_TABLE_BUFFER);
    assert(out->bindings[0].slot == 18u && out->bindings[0].size_index == 18u);
    assert(out->bindings[1].slot == 30u && out->bindings[1].table == DOE_COMPILER_TEXT_TABLE_TEXTURE);
    assert(out->bindings[2].slot == 15u && out->bindings[2].table == DOE_COMPILER_TEXT_TABLE_SAMPLER);
    assert(out->target_entries[0].sizes_slot == 30u && out->target_entries[0].sizes_bytes == 76u);
    assert(strcmp(out->target_entries[0].name, "run") == 0 && out->target_entries[0].stage == 1u);
    assert(out->target_entries[1].sizes_slot == UINT32_MAX && out->target_entries[1].sizes_bytes == 0u);
    assert(out->target_entries[1].stage == 2u && out->target_entries[2].stage == 3u);
    out->text[result.published_text_bytes] = '\0';
    assert(strstr(out->text, "host_name(\"run\")") != NULL && strstr(out->text, "[[buffer(18)]]") != NULL);
    assert(strstr(out->text, "[[texture(30)]]") != NULL && strstr(out->text, "[[sampler(15)]]") != NULL);
    assert(strstr(out->text, "sizes[18]") != NULL);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, "fragment", 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.required_entries == 1u && result.required_bindings == 0u);
    assert(out->target_entries[0].entry_index == 2u && strcmp(out->target_entries[0].name, "fragment") == 0);
    assert(result.stages.resources.module.module.selected_entry == 2u && result.stages.resources.module.required.entries == 3u);
    out->text[result.published_text_bytes] = '\0';
    assert(strstr(out->text, "host_name(\"fragment\")") != NULL && strstr(out->text, "host_name(\"run\")") == NULL);
    memset(out, 0xa5, sizeof(*out));
    memcpy(saved, out, sizeof(*out));
    for (unsigned field = 0u; field < 11u; ++field) {
        DoeCompilerTextOutputs small = outputs;
        switch (field) {
            case 0u: small.text_capacity = measured.required_text_bytes - 1u; break;
            case 1u: small.binding_capacity = measured.required_bindings - 1u; break;
            case 2u: small.entry_capacity = measured.required_entries - 1u; break;
            case 3u: small.stages.resources.module.type_capacity = 0u; break;
            case 4u: small.stages.resources.module.member_capacity = 0u; break;
            case 5u: small.stages.resources.module.resource_capacity = 0u; break;
            case 6u: small.stages.resources.module.entry_capacity = 0u; break;
            case 7u: small.stages.resources.module.name_capacity = 0u; break;
            case 8u: small.stages.resources.handle_capacity = 0u; break;
            case 9u: small.stages.interface_capacity = 0u; break;
            default: small.stages.resources.module.entry_reference_capacity = 0u; break;
        }
        assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 3u, &small, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(result.stages.resources.module.required_is_exact == 1u);
        unchanged(saved, out, &result);
    }
    DoeCompilerTextCapacities tiny = capacity;
    tiny.text_bytes = measured.required_text_bytes - 1u;
    assert(compile(&tiny, workspace, requirements.size_bytes, source, NULL, 3u, &outputs, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.stages.resources.module.exhausted_pool == DOE_COMPILER_POOL_TEXT_BYTES);
    assert(result.stages.resources.module.minimum_pool_capacity == measured.required_text_bytes);
    unchanged(saved, out, &result);
    tiny.text_bytes = 0u;
    assert(compile(&tiny, workspace, requirements.size_bytes, source, NULL, 7u, NULL, &result) == DOE_COMPILER_OK);
    DoeCompilerTextOutputs metadata = outputs;
    metadata.text = NULL; metadata.text_capacity = 0u;
    assert(compile(&tiny, workspace, requirements.size_bytes, source, NULL, 2u, &metadata, &result) == DOE_COMPILER_OK);
    assert(result.required_text_bytes == 0u && result.published_entries == 3u);
    DoeCompilerTextOutputs code = empty_spans();
    code.text = out->text; code.text_capacity = sizeof(out->text);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, "run", 1u, &code, &result) == DOE_COMPILER_OK);
    assert(result.published_entries == 0u && result.stages.resources.module.module.schema_version == 0u);
    DoeCompilerTextOutputs empty = empty_spans();
    assert(compile(&tiny, workspace, requirements.size_bytes, "", NULL, 3u, &empty, &result) == DOE_COMPILER_OK);
    assert(result.required_text_bytes == 0u && result.required_entries == 0u);
    memset(out, 0xa5, sizeof(*out)); memcpy(saved, out, sizeof(*out));
    assert(compile(&capacity, workspace, requirements.size_bytes, "@compute @workgroup_size(1) fn main(){} fn bad(){let x=missing;}",
        "main", 3u, &outputs, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    unchanged(saved, out, &result);
    assert(compile(&capacity, workspace, requirements.size_bytes, "fn helper(){}", "helper", 3u, &outputs, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    unchanged(saved, out, &result);
    assert(compile(&capacity, workspace, requirements.size_bytes,
        "@group(3) @binding(0) var<storage,read> a:array<u32>;@compute @workgroup_size(1) fn main(){let x=a[0];}",
        "main", 3u, &outputs, &result) == DOE_COMPILER_UNSUPPORTED);
    unchanged(saved, out, &result);
    assert(compile(&capacity, workspace, requirements.size_bytes,
        "fn f(p:ptr<function,u32>){*p=1u;}@compute @workgroup_size(1) fn main(){var x=0u;f(&x);}",
        "main", 3u, &outputs, &result) == DOE_COMPILER_UNSUPPORTED);
    unchanged(saved, out, &result);
    DoeCompilerTextOutputs invalid = outputs;
    invalid.bindings = (DoeCompilerTextBindingV10 *)((unsigned char *)out->bindings + 1u);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 3u, &invalid, &result) == DOE_COMPILER_INPUT_ERROR);
    unchanged(saved, out, &result);
    uint32_t word = 0u;
    invalid = outputs; invalid.stages.resources.module.words = &word;
    assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 3u, &invalid, &result) == DOE_COMPILER_INPUT_ERROR);
    unchanged(saved, out, &result);
    assert(compile(&capacity, workspace, requirements.size_bytes - 1u, source, NULL, 3u, &outputs, &result) == DOE_COMPILER_INPUT_ERROR);
    unchanged(saved, out, &result);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 3u, &empty, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    unchanged(saved, out, &result);
    const DoeCompilerEntrySelectionV5 none = {DOE_COMPILER_SELECT_NONE, 0u, NULL, 0u};
    memset(&result, 0x5a, sizeof(result));
    DoeCompilerTextResult result_before = result;
    invalid = outputs; --invalid.entry_record_size;
    assert(doe_compiler_compile_text(10u, 1u, &capacity, sizeof(capacity), workspace, requirements.size_bytes,
        source, sizeof(source) - 1u, &none, sizeof(none), 3u, &invalid, sizeof(invalid), &result, sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&result, &result_before, sizeof(result)) == 0);
    assert(doe_compiler_compile_text(10u, 99u, &capacity, sizeof(capacity), workspace, requirements.size_bytes,
        source, sizeof(source) - 1u, &none, sizeof(none), 3u, &outputs, sizeof(outputs), &result, sizeof(result)) == DOE_COMPILER_UNSUPPORTED);
    unchanged(saved, out, &result);
    DoeCompilerTextCapacities overflow = capacity; overflow.text_bytes = SIZE_MAX;
    DoeCompilerWorkspaceRequirements preserved = requirements;
    assert(doe_compiler_text_requirements(10u, 1u, &overflow, sizeof(overflow), &requirements, sizeof(requirements)) == DOE_COMPILER_INPUT_ERROR);
    assert(memcmp(&preserved, &requirements, sizeof(requirements)) == 0);
    assert(doe_compiler_text_requirements(10u, 99u, &capacity, sizeof(capacity), &requirements, sizeof(requirements)) == DOE_COMPILER_UNSUPPORTED);
    assert(memcmp(&preserved, &requirements, sizeof(requirements)) == 0);
    assert(compile(&capacity, workspace, requirements.size_bytes, source, NULL, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(errno == ERANGE && fegetround() == FE_DOWNWARD);
    if (argc == 2) {
        const char operation_source[] =
            "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
            "fn next(x:u32)->u32{return x+1u;}"
            "@compute @workgroup_size(1) fn main(){out[0]=next(41u);out[1]=arrayLength(&out);"
            "out[2]=out[99];out[99]=7u;var sum=0u;for(var i=0u;i<4u;i++){sum+=i;}out[3]=sum;}";
        assert(compile(&capacity, workspace, requirements.size_bytes, operation_source, "main", 1u, &code, &result) == DOE_COMPILER_OK);
        FILE *file = fopen(argv[1], "wb");
        assert(file != NULL && fwrite(out->text, 1u, result.published_text_bytes, file) == result.published_text_bytes);
        assert(fclose(file) == 0);
    }
    assert(fesetround(FE_TONEAREST) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0);
    free(saved); free(out); free(workspace);
    puts("ABI10 shared source-to-MSL, sparse slots/host names/per-entry sizes, graphics, no SPIR-V pools, exact measure, failure atomicity and reuse passed");
    return 0;
}
