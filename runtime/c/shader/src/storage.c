/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_shader_status shader_fail(doe_shader_error *error, doe_shader_status status,
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

bool shader_reserve(size_t *cursor, size_t count, size_t width, size_t alignment,
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

bool shader_layout(doe_shader_target target, DoeCompilerModuleOutputCounts n, size_t handles, size_t interfaces, size_t source, size_t identity,
    size_t text, size_t bindings, size_t entries, size_t *bytes, size_t offsets[15])
{
    *bytes = sizeof(doe_shader);
    const bool hlsl = target == DOE_SHADER_HLSL;
    const size_t binding_size = hlsl ? sizeof(DoeCompilerHlslBindingV11) : sizeof(DoeCompilerMetalBindingV12);
    const size_t entry_size = hlsl ? sizeof(DoeCompilerHlslEntryV11) : sizeof(DoeCompilerMetalEntryV12);
    const size_t binding_alignment = hlsl ? alignof(DoeCompilerHlslBindingV11) : alignof(DoeCompilerMetalBindingV12);
    const size_t entry_alignment = hlsl ? alignof(DoeCompilerHlslEntryV11) : alignof(DoeCompilerMetalEntryV12);
    return shader_reserve(bytes, source, 1, 1, &offsets[0]) &&
        shader_reserve(bytes, identity, 1, 1, &offsets[1]) &&
        shader_reserve(bytes, n.words, sizeof(uint32_t), alignof(uint32_t), &offsets[2]) &&
        shader_reserve(bytes, n.types, sizeof(DoeCompilerTypeV4), alignof(DoeCompilerTypeV4), &offsets[3]) &&
        shader_reserve(bytes, n.members, sizeof(DoeCompilerMemberV4), alignof(DoeCompilerMemberV4), &offsets[4]) &&
        shader_reserve(bytes, n.resources, sizeof(DoeCompilerResourceV4), alignof(DoeCompilerResourceV4), &offsets[5]) &&
        shader_reserve(bytes, n.workgroups, sizeof(DoeCompilerWorkgroupV5), alignof(DoeCompilerWorkgroupV5), &offsets[6]) &&
        shader_reserve(bytes, n.entries, sizeof(DoeCompilerEntryV5), alignof(DoeCompilerEntryV5), &offsets[7]) &&
        shader_reserve(bytes, n.entry_references, sizeof(uint32_t), alignof(uint32_t), &offsets[8]) &&
        shader_reserve(bytes, n.name_bytes, 1, 1, &offsets[9]) &&
        shader_reserve(bytes, handles, sizeof(DoeCompilerHandleV6), alignof(DoeCompilerHandleV6), &offsets[10]) &&
        shader_reserve(bytes, interfaces, sizeof(DoeCompilerStageInterfaceV7),
            alignof(DoeCompilerStageInterfaceV7), &offsets[11]) &&
        shader_reserve(bytes, text, 1, 1, &offsets[12]) &&
        shader_reserve(bytes, bindings, binding_size, binding_alignment, &offsets[13]) &&
        shader_reserve(bytes, entries, entry_size, entry_alignment, &offsets[14]);
}

void shader_assign_outputs(doe_shader *shader, const size_t offsets[15],
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


bool shader_options_valid(const doe_shader_options *options)
{
    return options && options->allocator.allocate && options->allocator.deallocate &&
        options->compiler_identity && options->compiler_identity_size &&
        (options->semantic_flags & ~(DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_CORE_BINDING_NUMBERS | DOE_COMPILER_DISABLE_F16 | DOE_COMPILER_DISABLE_SUBGROUPS | DOE_COMPILER_DISABLE_CLIP_DISTANCES |
            DOE_COMPILER_DISABLE_PRIMITIVE_INDEX | DOE_COMPILER_DISABLE_DUAL_SOURCE_BLENDING)) == 0u &&
        (options->target == DOE_SHADER_SPIRV || options->target == DOE_SHADER_METAL || options->target == DOE_SHADER_HLSL);
}

/* Fixed input extents are captured by value before allocator reentry. Copy all
 * externally owned bytes before measuring; later compiler passes use this arena
 * or the privately prepared artifact, never the caller's mutable storage. */
doe_shader_status shader_input_allocate(const doe_shader_allocator *allocator,
    const DoeCompilerWorkspaceRequirements *requirements, const char *source, size_t source_size,
    const void *identity, size_t identity_size, bool snapshot, shader_input *out)
{
    size_t bytes = requirements->size_bytes, source_offset = 0, identity_offset = 0;
    if (snapshot && (!shader_reserve(&bytes,source_size,1,1,&source_offset) ||
        !shader_reserve(&bytes,identity_size,1,1,&identity_offset))) return DOE_SHADER_OVERFLOW;
    void *allocation = allocator->allocate(allocator->userdata,bytes,requirements->alignment_bytes);
    if (!allocation) return DOE_SHADER_OUT_OF_MEMORY;
    shader_input input = {.allocation=allocation,.bytes=bytes,.source=source,.identity=identity};
    if (snapshot) {
        char *source_copy = (char *)allocation + source_offset;
        unsigned char *identity_copy = (unsigned char *)allocation + identity_offset;
        if (source_size) memcpy(source_copy,source,source_size);
        if (identity_size) memcpy(identity_copy,identity,identity_size);
        input.source = source_copy;
        input.identity = identity_copy;
    }
    *out = input;
    return DOE_SHADER_OK;
}
