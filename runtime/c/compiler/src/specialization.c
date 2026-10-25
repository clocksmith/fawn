/* SPDX-License-Identifier: MIT */
#include "override.h"
#include "compile_internal.h"
#include "text_internal.h"
#include <errno.h>
#include <string.h>

static bool append(size_t *bytes, size_t count, size_t width, size_t alignment) {
    const size_t padding = (alignment - *bytes % alignment) % alignment;
    if (*bytes > SIZE_MAX - padding) return false;
    *bytes += padding;
    if (count > (SIZE_MAX - *bytes) / width) return false;
    *bytes += count * width;
    return true;
}
bool specialization_layout(uint32_t target, const DoeCompilerSpecializationCapacities *caps,
                           size_t *base, DoeCompilerWorkspaceRequirements *requirements) {
    if (caps == NULL || caps->overrides >= UINT32_MAX) return false;
    if (target == DOE_COMPILER_TARGET_SPIRV) {
        CompilerLimits limits;
        size_t stage_base, bytes;
        if (!stage_layout(&caps->target.stages, &limits, &stage_base, &bytes)) return false;
        *requirements = (DoeCompilerWorkspaceRequirements){bytes, _Alignof(max_align_t)};
    } else if (!compiler_text_workspace(target, &caps->target, requirements)) return false;
    size_t bytes = requirements->size_bytes;
    if (!append(&bytes, 0u, 1u, _Alignof(OverrideRecord))) return false;
    *base = bytes;
    const DoeCompilerTypedCapacities *typed = &caps->target.stages.module.typed;
    if (!append(&bytes, caps->overrides, sizeof(OverrideRecord), _Alignof(OverrideRecord)) ||
        !append(&bytes, typed->tokens / 6u, sizeof(uint32_t), _Alignof(uint32_t)) ||
        !append(&bytes, typed->tokens / 8u, 1u, 1u) ||
        !append(&bytes, caps->target.stages.module.entries, 6u * sizeof(uint32_t), _Alignof(uint32_t))) return false;
    requirements->size_bytes = bytes;
    return true;
}
void specialization_bind(SpecializationContext *context, void *workspace, size_t base,
                         const DoeCompilerSpecializationCapacities *caps) {
    unsigned char *bytes = (unsigned char *)workspace + base;
    context->records = (OverrideRecord *)bytes;
    context->capacity = (uint32_t)caps->overrides;
    context->count = 0u;
    bytes += caps->overrides * sizeof(OverrideRecord);
    context->functions = (uint32_t *)bytes;
    bytes += (caps->target.stages.module.typed.tokens / 6u) * sizeof(uint32_t);
    context->workgroups = bytes;
    bytes += caps->target.stages.module.typed.tokens / 8u;
    const size_t padding = (_Alignof(uint32_t) - (uintptr_t)bytes % _Alignof(uint32_t)) % _Alignof(uint32_t);
    context->entry_expressions = (uint32_t *)(bytes + padding);
    for (size_t i = 0u; i < caps->target.stages.module.entries; ++i)
        for (size_t axis = 0u; axis < 6u; ++axis) context->entry_expressions[6u*i + axis] = NO_ID;
}
static DoeCompilerStatus specialization_requirements(uint32_t version, uint32_t expected, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    DoeCompilerStatus status = DOE_COMPILER_OK;
    DoeCompilerWorkspaceRequirements queried;
    size_t base;
    if (requirements == NULL) status = DOE_COMPILER_INPUT_ERROR;
    else if (version != expected || capacities_size != sizeof(*caps) ||
             requirements_size != sizeof(*requirements)) status = DOE_COMPILER_ABI_MISMATCH;
    else if (!specialization_layout(target, caps, &base, &queried)) status = DOE_COMPILER_INPUT_ERROR;
    else *requirements = queried;
    errno = saved_errno;
    return status;
}
DoeCompilerStatus doe_compiler_specialization_requirements(uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    return specialization_requirements(version, DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
        target, caps, capacities_size, requirements, requirements_size);
}
DoeCompilerStatus doe_compiler_module_specialization_requirements(uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    return specialization_requirements(version, DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION,
        target, caps, capacities_size, requirements, requirements_size);
}
static DoeCompilerNameV4 publish_name(const Source *s, NameSpan name, char *names, uint32_t *cursor) {
    const DoeCompilerNameV4 out = {*cursor, name.length};
    memcpy(names + *cursor, s->source + name.offset, name.length);
    names[*cursor + name.length] = '\0';
    *cursor += name.length + 1u;
    return out;
}
static bool validation_metadata(Source *s, const DoeCompilerValidationOutputs *outputs,
                                DoeCompilerValidationResult *result) {
    const CompilerWorkspace *c = s->compiler;
    const SpecializationContext *context = s->specialization;
    uint64_t names = 0u;
    for (uint32_t i = 0u; i < context->count; ++i)
        names += (uint64_t)c->constant_decls[context->records[i].declaration].name.length + 1u;
    for (uint32_t i = 0u; i < c->entry_count; ++i)
        names += (uint64_t)c->functions[c->entries[i].function].name.length + 1u;
    if (names > UINT32_MAX)
        return source_error(s, DOE_COMPILER_UNSUPPORTED, s->length, "validation names exceed uint32 metadata format");
    result->required_overrides = context->count;
    result->required_entries = c->entry_count;
    result->required_name_bytes = (size_t)names;
    result->required_is_exact = 1u;
    if (outputs != NULL && (outputs->override_capacity < context->count ||
        outputs->entry_capacity < c->entry_count || outputs->name_capacity < names))
        return source_error(s, DOE_COMPILER_OUTPUT_TOO_SMALL, s->length, "validation output capacity is too small");
    return true;
}
static void validation_publish(const Source *s, const DoeCompilerValidationOutputs *outputs,
                               DoeCompilerValidationResult *result) {
    const CompilerWorkspace *c = s->compiler;
    const SpecializationContext *context = s->specialization;
    uint32_t cursor = 0u;
    for (uint32_t i = 0u; i < context->count; ++i) {
        const OverrideRecord *record = &context->records[i];
        const ConstantSyntax *declaration = &c->constant_decls[record->declaration];
        outputs->overrides[i] = (DoeCompilerOverrideV13){
            publish_name(s, declaration->name, outputs->names, &cursor), declaration->type,
            record->flags, record->id, 0u};
    }
    for (uint32_t i = 0u; i < c->entry_count; ++i) {
        const FunctionRecord *function = &c->functions[c->entries[i].function];
        outputs->entries[i] = (DoeCompilerModuleEntryV13){
            publish_name(s, function->name, outputs->names, &cursor), stage_kind(function), 0u};
    }
    result->published_overrides = result->required_overrides;
    result->published_entries = result->required_entries;
    result->published_name_bytes = result->required_name_bytes;
}
DoeCompilerStatus doe_compiler_validate_module(uint32_t version,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    uint32_t flags, const DoeCompilerValidationOutputs *outputs, size_t outputs_size,
    DoeCompilerValidationResult *result, size_t result_size) {
    const int saved_errno = errno;
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_SPECIALIZATION_ABI_VERSION || capacities_size != sizeof(*caps) ||
        result_size != sizeof(*result) || (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u) ||
        (outputs != NULL && (outputs->override_record_size != sizeof(DoeCompilerOverrideV13) ||
         outputs->entry_record_size != sizeof(DoeCompilerModuleEntryV13)))) return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    Source s = {.source=source, .length=length, .diagnostic=&result->diagnostic,
        .exhausted_pool=&result->exhausted_pool, .minimum_pool_capacity=&result->minimum_pool_capacity};
    SpecializationContext context = {0};
    DoeCompilerWorkspaceRequirements requirements;
    CompilerLimits limits;
    size_t base, stage_base, stage_bytes;
    bool valid = specialization_layout(DOE_COMPILER_TARGET_SPIRV, caps, &base, &requirements) &&
        workspace != NULL && workspace_size >= requirements.size_bytes &&
        (uintptr_t)workspace % requirements.alignment_bytes == 0u &&
        (flags & ~(DOE_COMPILER_CORE_BINDING_NUMBERS | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_DISABLE_F16)) == 0u;
    if (valid && outputs != NULL)
        valid = compiler_valid_span(outputs->overrides, outputs->override_capacity, sizeof(DoeCompilerOverrideV13),
                    _Alignof(DoeCompilerOverrideV13), true) &&
            compiler_valid_span(outputs->entries, outputs->entry_capacity, sizeof(DoeCompilerModuleEntryV13),
                    _Alignof(DoeCompilerModuleEntryV13), true) &&
            compiler_valid_span(outputs->names, outputs->name_capacity, 1u, 1u, true);
    if (!valid) (void)source_error(&s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid validation capacities/workspace/flags/output spans");
    else if (!stage_layout(&caps->target.stages, &limits, &stage_base, &stage_bytes))
        (void)source_error(&s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid validation stage layout");
    else {
        specialization_bind(&context, workspace, base, caps);
        s.specialization = &context;
        CompilerWorkspace *c = workspace;
        compiler_job_bind(c, workspace, &limits);
        c->module_flags = ((flags & DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT) != 0u ? MODULE_STANDARD_UNIFORM : 0u) |
            ((flags & DOE_COMPILER_CORE_BINDING_NUMBERS) != 0u ? MODULE_CORE_BINDINGS : 0u) |
        ((flags & DOE_COMPILER_DISABLE_F16) != 0u ? MODULE_DISABLE_F16 : 0u);
        stage_bind(&s, workspace, &caps->target.stages, stage_base);
        NumericEnvironment environment;
        const bool analyzed = compiler_analyze(&s, c, &environment) && validation_metadata(&s, outputs, result);
        const bool restored = numeric_environment_end(&s, &environment);
        if (analyzed && restored && outputs != NULL) validation_publish(&s, outputs, result);
    }
    errno = saved_errno;
    return result->diagnostic.status;
}

bool sampling_layout(uint32_t target, const DoeCompilerSamplingCapacitiesV16 *caps,
    size_t *base, DoeCompilerWorkspaceRequirements *requirements) {
    size_t specialization_base;
    if (caps == NULL || caps->sampling_pairs >= UINT32_MAX ||
        !specialization_layout(target, &caps->specialization, &specialization_base, requirements)) return false;
    const size_t alignment = _Alignof(DoeCompilerSamplingPairV16);
    const size_t padding = (alignment - requirements->size_bytes % alignment) % alignment;
    if (requirements->size_bytes > SIZE_MAX - padding) return false;
    *base = requirements->size_bytes + padding;
    if (caps->sampling_pairs > (SIZE_MAX - *base) / sizeof(DoeCompilerSamplingPairV16)) return false;
    requirements->size_bytes = *base + caps->sampling_pairs * sizeof(DoeCompilerSamplingPairV16);
    return true;
}
DoeCompilerStatus doe_compiler_sampling_requirements(uint32_t version, uint32_t target,
    const DoeCompilerSamplingCapacitiesV16 *caps, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    if (requirements == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_SAMPLING_ABI_VERSION || capacities_size != sizeof(*caps) ||
        requirements_size != sizeof(*requirements)) return DOE_COMPILER_ABI_MISMATCH;
    DoeCompilerWorkspaceRequirements result;
    size_t base;
    if (!sampling_layout(target, caps, &base, &result)) return DOE_COMPILER_INPUT_ERROR;
    *requirements = result;
    return DOE_COMPILER_OK;
}
