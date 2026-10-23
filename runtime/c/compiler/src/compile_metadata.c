/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "compile_internal.h"

bool compiler_metadata_prepare(Source *source, DoeCompilerTypedOutputCounts *typed,
    uint32_t flags, bool handles, CompilerMetadata *metadata) {
    const CompilerWorkspace *c = source->compiler;
    *metadata = (CompilerMetadata){0};
    const uint64_t stage_names = stage_name_bytes(source);
    if (stage_names > UINT32_MAX - typed->name_bytes)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, source->length,
            "interface names exceed uint32 metadata format");
    metadata->stage_names = (size_t)stage_names;
    typed->name_bytes += (size_t)stage_names;
    if ((flags & DOE_COMPILER_MODULE_WORDS) != 0u) metadata->counts.words = typed->words;
    if ((flags & DOE_COMPILER_MODULE_REFLECTION) == 0u) return true;
    metadata->interfaces = source->stages != NULL ? source->stages->interface_count : 0u;
    metadata->handles = handles ? reflect_handle_count(c) : 0u;
    metadata->counts.types = typed->types;
    metadata->counts.members = typed->members;
    metadata->counts.resources = typed->resources;
    metadata->counts.workgroups = c->workgroup_count;
    metadata->counts.entries = c->entry_count;
    metadata->counts.entry_references = c->entry_reference_count;
    metadata->counts.name_bytes = typed->name_bytes;
    return true;
}
bool compiler_metadata_fits(const CompilerMetadata *metadata, const DoeCompilerModuleOutputs *outputs,
    const DoeCompilerResourceOutputs *resources, const DoeCompilerStageOutputs *stages) {
    const DoeCompilerModuleOutputCounts *counts = &metadata->counts;
    return (stages == NULL || stages->interface_capacity >= metadata->interfaces) &&
        (resources == NULL || resources->handle_capacity >= metadata->handles) &&
        outputs->word_capacity >= counts->words && outputs->type_capacity >= counts->types &&
        outputs->member_capacity >= counts->members && outputs->resource_capacity >= counts->resources &&
        outputs->workgroup_capacity >= counts->workgroups && outputs->entry_capacity >= counts->entries &&
        outputs->entry_reference_capacity >= counts->entry_references && outputs->name_capacity >= counts->name_bytes;
}
void compiler_metadata_publish(Source *source, const CompilerMetadata *metadata,
    uint32_t selected, uint32_t version,
    const DoeCompilerModuleOutputs *outputs, DoeCompilerModuleResult *result,
    const DoeCompilerResourceOutputs *resources, DoeCompilerResourceResult *resource_result,
    const DoeCompilerStageOutputs *stages, DoeCompilerStageResult *stage_result) {
    reflect_module(source, source->compiler, outputs, selected, &result->module);
    if (resource_result != NULL) {
        reflect_handles(source->compiler, resources->handles);
        result->module.schema_version = DOE_COMPILER_RESOURCES_SCHEMA_VERSION;
        resource_result->published_handles = metadata->handles;
    }
    if (stage_result != NULL) {
        stage_reflect(source, stages->interfaces, outputs->names,
            (uint32_t)(metadata->counts.name_bytes - metadata->stage_names));
        result->module.schema_version = version;
        stage_result->published_interfaces = metadata->interfaces;
    }
}
