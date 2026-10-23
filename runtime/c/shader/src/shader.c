/* SPDX-License-Identifier: MIT
 * Owned compilation result derived from WGSL runtime TranslationInfo lifetime,
 * using the new compiler's actual typed metadata, not the retained Zig layout. */
#include "doe_shader.h"
#include <limits.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct doe_shader {
    atomic_uint references;
    doe_shader_allocator allocator;
    size_t allocation_size, source_size, identity_size;
    char *source;
    unsigned char *identity;
    uint32_t selection_mode;
    size_t selection_size;
    char selection_name[64];
    DoeCompilerStageOutputs outputs;
    doe_shader_view view;
};

static doe_shader_status fail(doe_shader_error *error, doe_shader_status status,
    DoeCompilerStatus compiler, const DoeCompilerDiagnostic *diagnostic)
{
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = status;
        error->compiler_status = compiler;
        if (diagnostic) error->diagnostic = *diagnostic;
    }
    return status;
}

static bool reserve(size_t *cursor, size_t count, size_t width, size_t alignment,
    size_t *offset)
{
    size_t remainder = *cursor % alignment;
    size_t padding = remainder ? alignment - remainder : 0;
    if (*cursor > SIZE_MAX - padding || count > SIZE_MAX / width) return false;
    size_t start = *cursor + padding, bytes = count * width;
    if (start > SIZE_MAX - bytes) return false;
    *offset = start;
    *cursor = start + bytes;
    return true;
}

static bool layout(DoeCompilerModuleOutputCounts n, size_t handles, size_t interfaces, size_t source, size_t identity,
    size_t text, size_t bindings, size_t entries, size_t *bytes, size_t offsets[15])
{
    *bytes = sizeof(doe_shader);
    return reserve(bytes, source, 1, 1, &offsets[0]) &&
        reserve(bytes, identity, 1, 1, &offsets[1]) &&
        reserve(bytes, n.words, sizeof(uint32_t), alignof(uint32_t), &offsets[2]) &&
        reserve(bytes, n.types, sizeof(DoeCompilerTypeV4), alignof(DoeCompilerTypeV4), &offsets[3]) &&
        reserve(bytes, n.members, sizeof(DoeCompilerMemberV4), alignof(DoeCompilerMemberV4), &offsets[4]) &&
        reserve(bytes, n.resources, sizeof(DoeCompilerResourceV4), alignof(DoeCompilerResourceV4), &offsets[5]) &&
        reserve(bytes, n.workgroups, sizeof(DoeCompilerWorkgroupV5), alignof(DoeCompilerWorkgroupV5), &offsets[6]) &&
        reserve(bytes, n.entries, sizeof(DoeCompilerEntryV5), alignof(DoeCompilerEntryV5), &offsets[7]) &&
        reserve(bytes, n.entry_references, sizeof(uint32_t), alignof(uint32_t), &offsets[8]) &&
        reserve(bytes, n.name_bytes, 1, 1, &offsets[9]) &&
        reserve(bytes, handles, sizeof(DoeCompilerHandleV6), alignof(DoeCompilerHandleV6), &offsets[10]) &&
        reserve(bytes, interfaces, sizeof(DoeCompilerStageInterfaceV7),
            alignof(DoeCompilerStageInterfaceV7), &offsets[11]) &&
        reserve(bytes, text, 1, 1, &offsets[12]) &&
        reserve(bytes, bindings, sizeof(DoeCompilerTextBindingV10),
            alignof(DoeCompilerTextBindingV10), &offsets[13]) &&
        reserve(bytes, entries, sizeof(DoeCompilerTextEntryV10),
            alignof(DoeCompilerTextEntryV10), &offsets[14]);
}

static void assign_outputs(doe_shader *shader, const size_t offsets[15],
    DoeCompilerModuleOutputCounts n, size_t handles, size_t interfaces)
{
    unsigned char *base = (unsigned char *)shader;
    shader->source = (char *)(base + offsets[0]);
    shader->identity = base + offsets[1];
    shader->outputs.resources.module = (DoeCompilerModuleOutputs) {
        .words = (uint32_t *)(base + offsets[2]), .word_capacity = n.words,
        .types = (DoeCompilerTypeV4 *)(base + offsets[3]), .type_capacity = n.types,
        .type_record_size = sizeof(DoeCompilerTypeV4),
        .members = (DoeCompilerMemberV4 *)(base + offsets[4]), .member_capacity = n.members,
        .member_record_size = sizeof(DoeCompilerMemberV4),
        .resources = (DoeCompilerResourceV4 *)(base + offsets[5]), .resource_capacity = n.resources,
        .resource_record_size = sizeof(DoeCompilerResourceV4),
        .workgroups = (DoeCompilerWorkgroupV5 *)(base + offsets[6]), .workgroup_capacity = n.workgroups,
        .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
        .entries = (DoeCompilerEntryV5 *)(base + offsets[7]), .entry_capacity = n.entries,
        .entry_record_size = sizeof(DoeCompilerEntryV5),
        .entry_references = (uint32_t *)(base + offsets[8]), .entry_reference_capacity = n.entry_references,
        .names = (char *)(base + offsets[9]), .name_capacity = n.name_bytes
    };
    shader->outputs.resources.handles = handles ? (DoeCompilerHandleV6 *)(base + offsets[10]) : NULL;
    shader->outputs.resources.handle_capacity = handles;
    shader->outputs.resources.handle_record_size = sizeof(DoeCompilerHandleV6);
    shader->outputs.interfaces = interfaces ? (DoeCompilerStageInterfaceV7 *)(base + offsets[11]) : NULL;
    shader->outputs.interface_capacity = interfaces;
    shader->outputs.interface_record_size = sizeof(DoeCompilerStageInterfaceV7);
}

doe_shader_status doe_shader_create(const doe_shader_options *options,
    const char *source, size_t source_size, doe_shader **out, doe_shader_error *error)
{
    if (!options || !out || (!source && source_size) ||
        !options->allocator.allocate || !options->allocator.deallocate ||
        !options->compiler_identity || !options->compiler_identity_size ||
        (options->target != DOE_SHADER_SPIRV && options->target != DOE_SHADER_METAL))
        return fail(error, DOE_SHADER_INVALID_ARGUMENT, DOE_COMPILER_INPUT_ERROR, NULL);
    doe_shader_options saved = *options;
    if (saved.selection.name_length > 63 ||
        (!saved.selection.name && saved.selection.name_length))
        return fail(error, DOE_SHADER_INVALID_ARGUMENT, DOE_COMPILER_INPUT_ERROR, NULL);
    const bool metal = saved.target == DOE_SHADER_METAL;
    const DoeCompilerStageCapacities capacities = {.module = saved.capacities,
        .interface_attributes = saved.interface_attributes, .stage_interfaces = saved.stage_interfaces};
    DoeCompilerTextCapacities text_capacities = {.stages = capacities};
    DoeCompilerTextResult text_result = {0};
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status = metal ? doe_compiler_text_requirements(
        DOE_COMPILER_TEXT_ABI_VERSION, DOE_COMPILER_TARGET_METAL,
        &text_capacities, sizeof(text_capacities), &requirements, sizeof(requirements)) :
        doe_compiler_stage_requirements(DOE_COMPILER_ATOMIC_ABI_VERSION,
        &capacities, sizeof(capacities), &requirements, sizeof(requirements));
    if (status != DOE_COMPILER_OK)
        return fail(error, DOE_SHADER_COMPILE_FAILED, status, NULL);
    void *workspace = saved.allocator.allocate(saved.allocator.userdata,
        requirements.size_bytes, requirements.alignment_bytes);
    if (!workspace) return fail(error, DOE_SHADER_OUT_OF_MEMORY, DOE_COMPILER_OK, NULL);
    DoeCompilerStageResult result = {0};
    if (metal) {
        status = doe_compiler_compile_text(DOE_COMPILER_TEXT_ABI_VERSION,
            DOE_COMPILER_TARGET_METAL, &text_capacities, sizeof(text_capacities),
            workspace, requirements.size_bytes, source, source_size,
            &saved.selection, sizeof(saved.selection), DOE_COMPILER_TEXT_CODE |
            DOE_COMPILER_TEXT_REFLECTION | DOE_COMPILER_TEXT_MEASURE,
            NULL, 0, &text_result, sizeof(text_result));
        result = text_result.stages;
    } else {
        status = doe_compiler_compile_stages(DOE_COMPILER_ATOMIC_ABI_VERSION,
            &capacities, sizeof(capacities), workspace, requirements.size_bytes,
            source, source_size, &saved.selection, sizeof(saved.selection),
            DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION | DOE_COMPILER_MODULE_MEASURE,
            NULL, 0, &result, sizeof(result));
    }
    doe_shader *shader = NULL;
    doe_shader_status outcome = DOE_SHADER_COMPILE_FAILED;
    if (status != DOE_COMPILER_OK) goto finished;
    if (metal) {
        saved.allocator.deallocate(saved.allocator.userdata, workspace,
            requirements.size_bytes, requirements.alignment_bytes);
        workspace = NULL;
        text_capacities.text_bytes = text_result.required_text_bytes;
        status = doe_compiler_text_requirements(DOE_COMPILER_TEXT_ABI_VERSION,
            DOE_COMPILER_TARGET_METAL, &text_capacities, sizeof(text_capacities),
            &requirements, sizeof(requirements));
        if (status != DOE_COMPILER_OK) goto finished;
        workspace = saved.allocator.allocate(saved.allocator.userdata,
            requirements.size_bytes, requirements.alignment_bytes);
        if (!workspace) {
            outcome = DOE_SHADER_OUT_OF_MEMORY;
            goto finished;
        }
    }
    size_t bytes, offsets[15];
    if (!result.resources.module.required_is_exact || !layout(result.resources.module.required, result.resources.required_handles, result.required_interfaces, source_size,
            saved.compiler_identity_size, text_result.required_text_bytes,
            text_result.required_bindings, text_result.required_entries, &bytes, offsets)) {
        outcome = DOE_SHADER_OVERFLOW;
        goto finished;
    }
    shader = saved.allocator.allocate(saved.allocator.userdata, bytes, alignof(max_align_t));
    if (!shader) {
        outcome = DOE_SHADER_OUT_OF_MEMORY;
        goto finished;
    }
    memset(shader, 0, sizeof(*shader));
    atomic_init(&shader->references, 1);
    shader->allocator = saved.allocator;
    shader->allocation_size = bytes;
    shader->source_size = source_size;
    shader->identity_size = saved.compiler_identity_size;
    shader->selection_mode = saved.selection.mode;
    shader->selection_size = saved.selection.name_length;
    if (saved.selection.name_length)
        memcpy(shader->selection_name, saved.selection.name, saved.selection.name_length);
    assign_outputs(shader, offsets, result.resources.module.required, result.resources.required_handles, result.required_interfaces);
    if (source_size) memcpy(shader->source, source, source_size);
    memcpy(shader->identity, saved.compiler_identity, saved.compiler_identity_size);
    DoeCompilerTextOutputs text_outputs = {0};
    if (metal) {
        shader->outputs.resources.module.words = NULL;
        unsigned char *base = (unsigned char *)shader;
        text_outputs = (DoeCompilerTextOutputs) {
            .stages = shader->outputs,
            .text = text_result.required_text_bytes ? (char *)(base + offsets[12]) : NULL,
            .text_capacity = text_result.required_text_bytes,
            .bindings = text_result.required_bindings ? (DoeCompilerTextBindingV10 *)(base + offsets[13]) : NULL,
            .binding_capacity = text_result.required_bindings,
            .binding_record_size = sizeof(DoeCompilerTextBindingV10),
            .entries = text_result.required_entries ? (DoeCompilerTextEntryV10 *)(base + offsets[14]) : NULL,
            .entry_capacity = text_result.required_entries,
            .entry_record_size = sizeof(DoeCompilerTextEntryV10)
        };
        status = doe_compiler_compile_text(DOE_COMPILER_TEXT_ABI_VERSION,
            DOE_COMPILER_TARGET_METAL, &text_capacities, sizeof(text_capacities),
            workspace, requirements.size_bytes, shader->source, source_size,
            &saved.selection, sizeof(saved.selection),
            DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION,
            &text_outputs, sizeof(text_outputs), &text_result, sizeof(text_result));
        result = text_result.stages;
    } else {
        status = doe_compiler_compile_stages(DOE_COMPILER_ATOMIC_ABI_VERSION,
            &capacities, sizeof(capacities), workspace, requirements.size_bytes,
            shader->source, source_size, &saved.selection, sizeof(saved.selection),
            DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION,
            &shader->outputs, sizeof(shader->outputs), &result, sizeof(result));
    }
    if (status == DOE_COMPILER_OK) {
        shader->view = (doe_shader_view) {
            .target = saved.target, .target_language_version = text_result.language_version,
            .text = text_outputs.text, .text_size = text_result.published_text_bytes,
            .target_bindings = text_outputs.bindings,
            .target_binding_count = text_result.published_bindings,
            .target_entries = text_outputs.entries,
            .target_entry_count = text_result.published_entries,
            .module = result.resources.module.module, .counts = result.resources.module.published,
            .words = shader->outputs.resources.module.words, .types = shader->outputs.resources.module.types,
            .members = shader->outputs.resources.module.members, .resources = shader->outputs.resources.module.resources,
            .workgroups = shader->outputs.resources.module.workgroups, .entries = shader->outputs.resources.module.entries,
            .entry_references = shader->outputs.resources.module.entry_references, .names = shader->outputs.resources.module.names,
            .handles = shader->outputs.resources.handles, .handle_count = result.resources.published_handles,
            .interfaces = shader->outputs.interfaces, .interface_count = result.published_interfaces
        };
        outcome = DOE_SHADER_OK;
    }
finished:
    if (workspace) saved.allocator.deallocate(saved.allocator.userdata, workspace,
        requirements.size_bytes, requirements.alignment_bytes);
    if (outcome != DOE_SHADER_OK && shader) doe_shader_release(shader);
    if (outcome == DOE_SHADER_OK) *out = shader;
    return fail(error, outcome, status, &result.resources.module.diagnostic);
}

doe_shader_status doe_shader_retain(doe_shader *shader)
{
    if (!shader) return DOE_SHADER_INVALID_ARGUMENT;
    unsigned count = atomic_load_explicit(&shader->references, memory_order_relaxed);
    do {
        if (!count || count == UINT_MAX) return DOE_SHADER_OVERFLOW;
    } while (!atomic_compare_exchange_weak_explicit(&shader->references, &count,
        count + 1, memory_order_relaxed, memory_order_relaxed));
    return DOE_SHADER_OK;
}

void doe_shader_release(doe_shader *shader)
{
    if (!shader) return;
    unsigned previous = atomic_fetch_sub_explicit(&shader->references, 1, memory_order_acq_rel);
    if (!previous) abort();
    if (previous != 1) return;
    doe_shader_allocator allocator = shader->allocator;
    allocator.deallocate(allocator.userdata, shader, shader->allocation_size, alignof(max_align_t));
}

doe_shader_status doe_shader_borrow(const doe_shader *shader, doe_shader_view *out)
{
    if (!shader || !out) return DOE_SHADER_INVALID_ARGUMENT;
    *out = shader->view;
    return DOE_SHADER_OK;
}

bool doe_shader_matches(const doe_shader *shader, doe_shader_target target,
    const char *source, size_t size,
    const void *identity, size_t identity_size, const DoeCompilerEntrySelectionV5 *selection)
{
    if (!shader || shader->view.target != target || !selection || (!source && size) || (!identity && identity_size) ||
        (!selection->name && selection->name_length) || selection->reserved ||
        size != shader->source_size || identity_size != shader->identity_size ||
        selection->mode != shader->selection_mode || selection->name_length != shader->selection_size)
        return false;
    return (!size || !memcmp(source, shader->source, size)) &&
        (!identity_size || !memcmp(identity, shader->identity, identity_size)) &&
        (!selection->name_length || !memcmp(selection->name, shader->selection_name, selection->name_length));
}
