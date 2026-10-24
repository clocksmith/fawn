/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "compile_internal.h"
#include <errno.h>
#include <string.h>

/* Public adapters capture errno before preflight and restore it only after all
 * compilation, FP restoration and result publication have finished. */
static DoeCompilerStatus compiler_return(DoeCompilerStatus status, int saved_errno) {
    errno = saved_errno;
    return status;
}

bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message) {
    DoeCompilerDiagnostic *diagnostic = source->diagnostic;
    diagnostic->status = status;
    diagnostic->offset = offset;
    diagnostic->line = 1u;
    diagnostic->column = 1u;
/* CRLF counts as one newline; lone CR and LF each count as one. */
    for (size_t i = 0; i < offset && i < source->length; ++i) {
        if (source->source[i] == '\r') {
            ++diagnostic->line;
            diagnostic->column = 1u;
        }
        else if (source->source[i] == '\n') {
            if (i == 0u || source->source[i - 1u] != '\r') ++diagnostic->line;
            diagnostic->column = 1u;
        }
        else ++diagnostic->column;
    }
    size_t message_length = strlen(message);
    if (message_length >= sizeof(diagnostic->message)) message_length = sizeof(diagnostic->message) - 1u;
    memcpy(diagnostic->message, message, message_length);
    diagnostic->message[message_length] = '\0';
    return false;
}

bool source_pool_error(Source *source, uint32_t pool, size_t minimum, size_t offset, const char *message) {
    if (source->exhausted_pool != NULL && *source->exhausted_pool == DOE_COMPILER_POOL_NONE) {
        *source->exhausted_pool = pool;
        *source->minimum_pool_capacity = minimum;
    }
    return source_error(source, pool == DOE_COMPILER_POOL_WORDS?DOE_COMPILER_OUTPUT_TOO_SMALL:DOE_COMPILER_INPUT_ERROR, offset, message);
}
/* Resolution, metadata admission, target emission and FP restoration all finish
 * before any bulk output is published. Every adapter uses this pipeline. Valid
 * entryless modules have metadata but no Vulkan shader binary; entry selection
 * is a separate consumer operation, not a fabricated entry or Linkage shader. */
static bool compile_pipeline(Source *source, CompilerWorkspace *c, uint32_t projection, DoeCompilerTypedOutputCounts *counts) {
    NumericEnvironment environment;
    const bool lowered = compiler_analyze(source, c, &environment) &&
        (c->entry_count == 0u || emit_spirv(source, c)) &&
        (projection==0u || (c->entry_count==1u || source_error(source,DOE_COMPILER_UNSUPPORTED,
            c->functions[c->entries[1].function].name.offset,"multiple compute entries cannot use a singular reflection projection"))) &&
        (projection==0u || projection==3u || reflect_legacy_admit(source,c,projection==2u)) &&
        (projection!=3u || reflect_buffers_only(source)) && reflect_prepare(source,c,counts) &&
        (projection == 0u || reflect_version_admit(source, DOE_COMPILER_TYPED_ABI_VERSION));
    const bool restored = numeric_environment_end(source, &environment);
    return lowered && restored;
}
static bool old_output(Source *s, CompilerWorkspace *c, uint32_t projection, uint32_t *words, size_t capacity) {
    if ((words == NULL && capacity != 0u) || capacity>SIZE_MAX/sizeof(*words))
        return source_error(s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid output word span");
    DoeCompilerTypedOutputCounts counts;
    if (!compile_pipeline(s, c, projection, &counts)) return false;
    if (capacity<c->word_count) return source_error(s, DOE_COMPILER_OUTPUT_TOO_SMALL, s->length, "caller output word capacity is too small");
    memcpy(words, c->words, c->word_count*sizeof(*words));
    return true;
}
DoeCompilerStatus doe_compiler_compile(uint32_t version, const char *source, size_t length, uint32_t *words, size_t capacity, DoeCompilerResult *result, size_t size) {
    const int saved_errno = errno;
    if (result == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_ABI_VERSION || size != sizeof(*result)) return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    Source context = {.source = source, .length = length, .diagnostic = &result->diagnostic};
    LegacyWorkspace storage;
    compiler_legacy_bind(&storage, false);
    if (old_output(&context, &storage.job, 1u, words, capacity)) {
        reflect_program(&context, &storage.job, &result->reflection);
        result->word_count = storage.job.word_count;
    }
    return compiler_return(result->diagnostic.status, saved_errno);
}
_Static_assert(_Alignof(LegacyWorkspace) <= _Alignof(max_align_t), "caller allocation alignment");
DoeCompilerStatus doe_compiler_workspace_requirements(uint32_t version, DoeCompilerWorkspaceRequirements *requirements, size_t size) {
    const int saved_errno = errno;
    if (requirements == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_ABI_VERSION_2 || size != sizeof(*requirements)) return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){sizeof(LegacyWorkspace), _Alignof(LegacyWorkspace)};
    return compiler_return(DOE_COMPILER_OK, saved_errno);
}
DoeCompilerStatus doe_compiler_compile_v2(uint32_t version, void *workspace, size_t workspace_size, const char *source, size_t length, uint32_t *words, size_t capacity, DoeCompilerResultV2 *result, size_t size) {
    const int saved_errno = errno;
    if (result == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_ABI_VERSION_2 || size != sizeof(*result)) return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    Source context = {.source = source, .length = length, .diagnostic = &result->diagnostic};
    if (workspace == NULL || workspace_size<sizeof(LegacyWorkspace) || (uintptr_t)workspace%_Alignof(LegacyWorkspace) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "workspace must satisfy queried size and alignment");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    LegacyWorkspace *storage = workspace;
    compiler_legacy_bind(storage, true);
    if (old_output(&context, &storage->job, 2u, words, capacity)) {
        reflect_program_v2(&context, &storage->job, &result->reflection);
        result->word_count = storage->job.word_count;
    }
    return compiler_return(result->diagnostic.status, saved_errno);
}
DoeCompilerStatus doe_compiler_job_requirements(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_JOB_ABI_VERSION || capacities_size != sizeof(*capacities) || requirements_size != sizeof(*requirements))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_job_layout(capacities, &limits, &bytes)) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){bytes, _Alignof(max_align_t)};
    return compiler_return(DOE_COMPILER_OK, saved_errno);
}
DoeCompilerStatus doe_compiler_compile_job(uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size, void *workspace, size_t workspace_size, const char *source, size_t length, uint32_t *words, size_t capacity, DoeCompilerResultV2 *result, size_t size) {
    const int saved_errno = errno;
    if (result == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_JOB_ABI_VERSION || capacities_size != sizeof(*capacities) || size != sizeof(*result))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    Source context = {.source = source, .length = length, .diagnostic = &result->diagnostic};
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_job_layout(capacities, &limits, &bytes)) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid job capacities or layout overflow");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    if (workspace == NULL || workspace_size<bytes || (uintptr_t)workspace%_Alignof(max_align_t) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "workspace must satisfy job size and alignment");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    CompilerWorkspace *c = workspace;
    compiler_job_bind(c, workspace, &limits);
    if (old_output(&context, c, 2u, words, capacity)) {
        reflect_program_v2(&context, c, &result->reflection);
        result->word_count = c->word_count;
    }
    return compiler_return(result->diagnostic.status, saved_errno);
}
DoeCompilerStatus doe_compiler_typed_requirements(uint32_t version, const DoeCompilerTypedCapacities *capacities, size_t capacities_size, DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_TYPED_ABI_VERSION || capacities_size != sizeof(*capacities) || requirements_size != sizeof(*requirements))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_typed_layout(capacities, &limits, &bytes)) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){bytes, _Alignof(max_align_t)};
    return compiler_return(DOE_COMPILER_OK, saved_errno);
}
static bool typed_spans(Source *s, uint32_t flags, const DoeCompilerTypedOutputs *o) {
    const bool measure = (flags&DOE_COMPILER_TYPED_MEASURE) != 0u, words = (flags&DOE_COMPILER_TYPED_WORDS) != 0u, reflection = (flags&DOE_COMPILER_TYPED_REFLECTION) != 0u;
    if ((flags & 3u) == 0u || (flags & ~(7u | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_CORE_BINDING_NUMBERS)) != 0u || (measure != (o == NULL)))
        return source_error(s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid output flags or measure descriptor");
    if (measure) return true;
    if (!compiler_valid_span(o->words, o->word_capacity, sizeof(*o->words), _Alignof(uint32_t), words) || !compiler_valid_span(o->types, o->type_capacity, sizeof(*o->types), _Alignof(DoeCompilerTypeV4), reflection) || !compiler_valid_span(o->members, o->member_capacity, sizeof(*o->members), _Alignof(DoeCompilerMemberV4), reflection) || !compiler_valid_span(o->resources, o->resource_capacity, sizeof(*o->resources), _Alignof(DoeCompilerResourceV4), reflection) || !compiler_valid_span(o->names, o->name_capacity, 1u, 1u, reflection))
        return source_error(s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid selected output span or capacity product");
    return true;
}
DoeCompilerStatus doe_compiler_compile_typed(uint32_t version, const DoeCompilerTypedCapacities *capacities, size_t capacities_size, void *workspace, size_t workspace_size, const char *source, size_t length, uint32_t flags, const DoeCompilerTypedOutputs *outputs, size_t outputs_size, DoeCompilerTypedResult *result, size_t result_size) {
    const int saved_errno = errno;
    if (result == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_TYPED_ABI_VERSION || capacities_size != sizeof(*capacities) || result_size != sizeof(*result) || (outputs != NULL?outputs_size != sizeof(*outputs):outputs_size != 0u)) return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    if (outputs != NULL && (outputs->type_record_size != sizeof(DoeCompilerTypeV4) || outputs->member_record_size != sizeof(DoeCompilerMemberV4) || outputs->resource_record_size != sizeof(DoeCompilerResourceV4))) return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    Source context = {.source = source, .length = length, .diagnostic = &result->diagnostic, .exhausted_pool = &result->exhausted_pool, .minimum_pool_capacity = &result->minimum_pool_capacity};
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_typed_layout(capacities, &limits, &bytes)) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid typed capacities or layout overflow");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    if (!typed_spans(&context, flags, outputs)) return compiler_return(result->diagnostic.status, saved_errno);
    if (workspace == NULL || workspace_size<bytes || (uintptr_t)workspace%_Alignof(max_align_t) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "workspace must satisfy typed size and alignment");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    CompilerWorkspace *c = workspace;
    compiler_job_bind(c, workspace, &limits);
    c->module_flags = ((flags & DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT) != 0u ? MODULE_STANDARD_UNIFORM : 0u) |
        ((flags & DOE_COMPILER_CORE_BINDING_NUMBERS) != 0u ? MODULE_CORE_BINDINGS : 0u);
    DoeCompilerTypedOutputCounts counts;
    if (!compile_pipeline(&context, c, (flags&DOE_COMPILER_TYPED_REFLECTION)!=0u?3u:0u, &counts)) return compiler_return(result->diagnostic.status, saved_errno);
    if ((flags&DOE_COMPILER_TYPED_WORDS) == 0u) counts.words = 0u;
    if ((flags&DOE_COMPILER_TYPED_REFLECTION) == 0u) counts.types = counts.members = counts.resources = counts.name_bytes = 0u;
    result->required = counts;
    result->required_is_exact = 1u;
    if ((flags&DOE_COMPILER_TYPED_MEASURE) != 0u) return compiler_return(DOE_COMPILER_OK, saved_errno);
    if (outputs->word_capacity<counts.words || outputs->type_capacity<counts.types || outputs->member_capacity<counts.members || outputs->resource_capacity<counts.resources || outputs->name_capacity<counts.name_bytes) {
        (void)source_error(&context, DOE_COMPILER_OUTPUT_TOO_SMALL, length, "selected caller output capacity is too small");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
/* Publication contains only prevalidated, bounded copies and assignments. */
    if (counts.words != 0u) memcpy(outputs->words, c->words, counts.words*sizeof(*outputs->words));
    if ((flags&DOE_COMPILER_TYPED_REFLECTION) != 0u) reflect_typed(&context, c, outputs, &result->module);
    result->published = counts;
    return compiler_return(DOE_COMPILER_OK, saved_errno);
}

DoeCompilerStatus doe_compiler_module_requirements(uint32_t version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno=errno;
    if (requirements==NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR,saved_errno);
    if (version!=DOE_COMPILER_MODULE_ABI_VERSION || capacities_size!=sizeof(*capacities) || requirements_size!=sizeof(*requirements))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH,saved_errno);
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_module_layout(capacities,&limits,&bytes)) return compiler_return(DOE_COMPILER_INPUT_ERROR,saved_errno);
    *requirements=(DoeCompilerWorkspaceRequirements){bytes,_Alignof(max_align_t)};
    return compiler_return(DOE_COMPILER_OK,saved_errno);
}
static DoeCompilerStatus compile_module(const DoeCompilerModuleCapacities *capacities,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, uint32_t flags, const DoeCompilerModuleOutputs *outputs,
    DoeCompilerModuleResult *result, const DoeCompilerResourceOutputs *resource_outputs,
    DoeCompilerResourceResult *resource_result, const DoeCompilerStageCapacities *stage_capacities,
    const DoeCompilerStageOutputs *stage_outputs, DoeCompilerStageResult *stage_result, uint32_t version) {
    const int saved_errno=errno;
    memset(result,0,sizeof(*result));
    Source context={.source=source,.length=length,.diagnostic=&result->diagnostic,
        .exhausted_pool=&result->exhausted_pool,.minimum_pool_capacity=&result->minimum_pool_capacity};
    CompilerLimits limits;
    size_t bytes, base = 0u;
    const bool layout = stage_capacities != NULL ? stage_layout(stage_capacities, &limits, &base, &bytes) :
        compiler_module_layout(capacities, &limits, &bytes);
    if (!layout) {
        (void)source_error(&context,DOE_COMPILER_INPUT_ERROR,0u,"invalid module capacities or layout overflow");
        return compiler_return(result->diagnostic.status,saved_errno);
    }
    if (resource_result != NULL && resource_outputs != NULL &&
        !compiler_valid_span(resource_outputs->handles, resource_outputs->handle_capacity, sizeof(DoeCompilerHandleV6),
            _Alignof(DoeCompilerHandleV6), (flags & DOE_COMPILER_MODULE_REFLECTION) != 0u)) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid selected handle span or capacity product");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    if (stage_outputs != NULL && !compiler_valid_span(stage_outputs->interfaces, stage_outputs->interface_capacity,
        sizeof(DoeCompilerStageInterfaceV7), _Alignof(DoeCompilerStageInterfaceV7),
        (flags & DOE_COMPILER_MODULE_REFLECTION) != 0u)) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid selected stage interface span or capacity product");
        return compiler_return(result->diagnostic.status, saved_errno);
    }
    if (!compiler_selection_shape(&context,selection) || !compiler_module_spans(&context,flags,outputs))
        return compiler_return(result->diagnostic.status,saved_errno);
    if (workspace==NULL || workspace_size<bytes || (uintptr_t)workspace%_Alignof(max_align_t)!=0u) {
        (void)source_error(&context,DOE_COMPILER_INPUT_ERROR,0u,"workspace must satisfy module size and alignment");
        return compiler_return(result->diagnostic.status,saved_errno);
    }
    CompilerWorkspace *c=workspace;
    compiler_job_bind(c,workspace,&limits);
    c->module_flags = ((flags & DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT) != 0u ? MODULE_STANDARD_UNIFORM : 0u) |
        ((flags & DOE_COMPILER_CORE_BINDING_NUMBERS) != 0u ? MODULE_CORE_BINDINGS : 0u);
    if (stage_capacities != NULL) stage_bind(&context, workspace, stage_capacities, base);
    DoeCompilerTypedOutputCounts typed;
    uint32_t selected;
    if (!compile_pipeline(&context,c,0u,&typed) || !compiler_select_entry(&context,selection,&selected))
        return compiler_return(result->diagnostic.status,saved_errno);
    if (resource_result == NULL && (flags & DOE_COMPILER_MODULE_REFLECTION) != 0u && !reflect_buffers_only(&context))
        return compiler_return(result->diagnostic.status, saved_errno);
    if ((flags & DOE_COMPILER_MODULE_REFLECTION) != 0u &&
        !reflect_version_admit(&context, version)) return compiler_return(result->diagnostic.status, saved_errno);
    CompilerMetadata metadata;
    if (!compiler_metadata_prepare(&context, &typed, flags, resource_result != NULL, &metadata))
        return compiler_return(result->diagnostic.status, saved_errno);
    if (stage_result != NULL) stage_result->required_interfaces = metadata.interfaces;
    if (resource_result != NULL) resource_result->required_handles = metadata.handles;
    result->required = metadata.counts;
    result->required_is_exact = 1u;
    if ((flags&DOE_COMPILER_MODULE_MEASURE)!=0u) return compiler_return(DOE_COMPILER_OK,saved_errno);
    if (!compiler_metadata_fits(&metadata, outputs, resource_outputs, stage_outputs)) {
        (void)source_error(&context,DOE_COMPILER_OUTPUT_TOO_SMALL,length,"selected module output capacity is too small");
        return compiler_return(result->diagnostic.status,saved_errno);
    }
    if (metadata.counts.words != 0u)
        memcpy(outputs->words, c->words, metadata.counts.words * sizeof(*outputs->words));
    if ((flags & DOE_COMPILER_MODULE_REFLECTION) != 0u)
        compiler_metadata_publish(&context, &metadata, selected, version, outputs, result,
            resource_outputs, resource_result, stage_outputs, stage_result);
    result->published = metadata.counts;
    return compiler_return(DOE_COMPILER_OK,saved_errno);
}
DoeCompilerStatus doe_compiler_compile_module(uint32_t version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size, uint32_t flags,
    const DoeCompilerModuleOutputs *outputs, size_t outputs_size,
    DoeCompilerModuleResult *result, size_t result_size) {
    const int saved_errno=errno;
    if (result==NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR,saved_errno);
    if (version!=DOE_COMPILER_MODULE_ABI_VERSION || capacities_size!=sizeof(*capacities) ||
        selection_size!=sizeof(*selection) || result_size!=sizeof(*result) ||
        (outputs!=NULL?outputs_size!=sizeof(*outputs):outputs_size!=0u))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH,saved_errno);
    if (!compiler_module_records(outputs)) return compiler_return(DOE_COMPILER_ABI_MISMATCH,saved_errno);
    return compiler_return(compile_module(capacities, workspace, workspace_size, source, length,
        selection, flags, outputs, result, NULL, NULL, NULL, NULL, NULL, version), saved_errno);
}

DoeCompilerStatus doe_compiler_resource_requirements(uint32_t version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_RESOURCES_ABI_VERSION || capacities_size != sizeof(*capacities) || requirements_size != sizeof(*requirements))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_module_layout(capacities, &limits, &bytes)) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){bytes, _Alignof(max_align_t)};
    return compiler_return(DOE_COMPILER_OK, saved_errno);
}
DoeCompilerStatus doe_compiler_compile_resources(uint32_t version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size, uint32_t flags,
    const DoeCompilerResourceOutputs *outputs, size_t outputs_size,
    DoeCompilerResourceResult *result, size_t result_size) {
    const int saved_errno = errno;
    if (result == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_RESOURCES_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        selection_size != sizeof(*selection) || result_size != sizeof(*result) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    if (outputs != NULL && (!compiler_module_records(&outputs->module) || outputs->handle_record_size != sizeof(DoeCompilerHandleV6)))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    return compiler_return(compile_module(capacities, workspace, workspace_size, source, length,
        selection, flags, outputs != NULL ? &outputs->module : NULL, &result->module, outputs, result, NULL, NULL, NULL, version), saved_errno);
}

DoeCompilerStatus doe_compiler_stage_requirements(uint32_t version,
    const DoeCompilerStageCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if ((version != DOE_COMPILER_STAGES_ABI_VERSION && version != DOE_COMPILER_MATRIX_ABI_VERSION && version != DOE_COMPILER_ATOMIC_ABI_VERSION) || capacities_size != sizeof(*capacities) || requirements_size != sizeof(*requirements))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerLimits limits;
    size_t base, bytes;
    if (!stage_layout(capacities, &limits, &base, &bytes)) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){bytes, _Alignof(max_align_t)};
    return compiler_return(DOE_COMPILER_OK, saved_errno);
}
DoeCompilerStatus doe_compiler_compile_stages(uint32_t version,
    const DoeCompilerStageCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size, uint32_t flags,
    const DoeCompilerStageOutputs *outputs, size_t outputs_size,
    DoeCompilerStageResult *result, size_t result_size) {
    const int saved_errno = errno;
    if (result == NULL) return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if ((version != DOE_COMPILER_STAGES_ABI_VERSION && version != DOE_COMPILER_MATRIX_ABI_VERSION && version != DOE_COMPILER_ATOMIC_ABI_VERSION) || capacities_size != sizeof(*capacities) ||
        selection_size != sizeof(*selection) || result_size != sizeof(*result) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    if (outputs != NULL && (!compiler_module_records(&outputs->resources.module) ||
        outputs->resources.handle_record_size != sizeof(DoeCompilerHandleV6) ||
        outputs->interface_record_size != sizeof(DoeCompilerStageInterfaceV7)))
        return compiler_return(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    if (capacities == NULL) {
        result->resources.module.diagnostic.status = DOE_COMPILER_INPUT_ERROR;
        return compiler_return(DOE_COMPILER_INPUT_ERROR, saved_errno);
    }
    return compiler_return(compile_module(capacities != NULL ? &capacities->module : NULL,
        workspace, workspace_size, source, length, selection, flags,
        outputs != NULL ? &outputs->resources.module : NULL, &result->resources.module,
        outputs != NULL ? &outputs->resources : NULL, &result->resources, capacities, outputs, result, version), saved_errno);
}
