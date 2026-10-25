/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Snapshot the exact typed rows and key bytes before either compiler pass. The
 * owned artifact makes its own copy; this temporary span is released before
 * publication. Double copies/comparisons operate on bytes, not FP arithmetic. */
static doe_shader_status snapshot_values(const doe_shader *module,
    const DoeCompilerOverrideValueV13 *values, size_t count,
    DoeCompilerOverrideValueV13 **out, size_t *extent)
{
    if (count && !values) return DOE_SHADER_INVALID_ARGUMENT;
    if (count > SIZE_MAX / sizeof(*values)) return DOE_SHADER_OVERFLOW;
    size_t bytes = 0, rows, keys, key_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!values[i].key && values[i].key_length) return DOE_SHADER_INVALID_ARGUMENT;
        if (values[i].key_length > SIZE_MAX - key_bytes) return DOE_SHADER_OVERFLOW;
        key_bytes += values[i].key_length;
    }
    if (!shader_reserve(&bytes,count,sizeof(*values),alignof(DoeCompilerOverrideValueV13),&rows) ||
        !shader_reserve(&bytes,key_bytes,1,1,&keys)) return DOE_SHADER_OVERFLOW;
    *extent = bytes;
    if (!count) { *out = NULL; return DOE_SHADER_OK; }
    DoeCompilerOverrideValueV13 *copy = module->allocator.allocate(module->allocator.userdata,bytes,
        alignof(DoeCompilerOverrideValueV13));
    if (!copy) return DOE_SHADER_OUT_OF_MEMORY;
    char *target = (char *)copy + keys;
    size_t remaining = key_bytes;
    for (size_t i = 0; i < count; ++i) {
        DoeCompilerOverrideValueV13 row;
        memcpy(&row,&values[i],sizeof(row));
        /* Allocator reentry may have changed the caller's rows. Revalidate
         * before copying or forming an offset; never exceed the admitted arena.
         * Once copied, both compiler passes consume only these owned rows. */
        if ((!row.key && row.key_length) || row.key_length > remaining) {
            module->allocator.deallocate(module->allocator.userdata,copy,bytes,alignof(DoeCompilerOverrideValueV13));
            return DOE_SHADER_INVALID_ARGUMENT;
        }
        memcpy(&copy[i],&row,sizeof(row));
        copy[i].key = target;
        if (row.key_length) memcpy(target,row.key,row.key_length);
        target += row.key_length;
        remaining -= row.key_length;
    }
    *out = copy;
    return DOE_SHADER_OK;
}

static doe_shader_status specialize(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t value_count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, doe_shader **out, doe_shader_error *error)
{
    if (!module || module->kind != SHADER_MODULE || !selection || !out || selection->reserved ||
        (selection->mode != DOE_COMPILER_SELECT_AUTO && selection->mode != DOE_COMPILER_SELECT_NAME) ||
        selection->name_length > 63 || (!selection->name && selection->name_length) ||
        (selection->mode == DOE_COMPILER_SELECT_AUTO && (selection->name || selection->name_length)))
        return shader_fail(error,DOE_SHADER_INVALID_ARGUMENT,DOE_COMPILER_INPUT_ERROR,NULL);
    if (pipeline && module->options.target != DOE_SHADER_METAL)
        return shader_fail(error,DOE_SHADER_INVALID_ARGUMENT,DOE_COMPILER_INPUT_ERROR,NULL);
    const DoeCompilerMetalPipelineOptionsV15 policy = {pipeline ? pipeline->sample_mask : UINT32_MAX};
    doe_shader_options options = module->options;
    char name[64] = {0};
    if (selection->name_length) memcpy(name,selection->name,selection->name_length);
    options.selection = *selection;
    options.selection.name = selection->name_length ? name : NULL;
    doe_shader_status status = doe_shader_retain((doe_shader *)module);
    if (status != DOE_SHADER_OK) return shader_fail(error,status,DOE_COMPILER_OK,NULL);
    DoeCompilerOverrideValueV13 *snapshot = NULL;
    size_t bytes = 0;
    status = snapshot_values(module,values,value_count,&snapshot,&bytes);
    doe_shader *result = NULL;
    if (status == DOE_SHADER_OK)
        status = shader_compile(&options,module->source,module->source_size,module,snapshot,value_count,pipeline ? &policy : NULL,&result,error);
    else (void)shader_fail(error,status,DOE_COMPILER_OK,NULL);
    if (snapshot) module->allocator.deallocate(module->allocator.userdata,snapshot,bytes,alignof(DoeCompilerOverrideValueV13));
    doe_shader_release((doe_shader *)module);
    if (status == DOE_SHADER_OK) *out = result;
    return status;
}

static bool specialization_matches(const doe_shader *shader, const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t value_count)
{
    if (!shader || shader->kind != SHADER_SPECIALIZED || !module || shader->module != module ||
        !selection || selection->reserved || (!selection->name && selection->name_length) ||
        (selection->mode == DOE_COMPILER_SELECT_AUTO && (selection->name || selection->name_length)) ||
        selection->mode != shader->selection_mode || selection->name_length != shader->selection_size ||
        value_count != shader->value_count || (value_count && !values)) return false;
    if (selection->name_length && memcmp(selection->name,shader->selection_name,selection->name_length)) return false;
    for (size_t i = 0; i < value_count; ++i) {
        const DoeCompilerOverrideValueV13 *saved = &shader->values[i];
        if ((!values[i].key && values[i].key_length) || values[i].key_length != saved->key_length ||
            (saved->key_length && memcmp(values[i].key,saved->key,saved->key_length)) ||
            memcmp(&values[i].value,&saved->value,sizeof(saved->value))) return false;
    }
    return true;
}

doe_shader_status doe_shader_specialize(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t count, doe_shader **out, doe_shader_error *error)
{
    return specialize(module,selection,values,count,NULL,out,error);
}
doe_shader_status doe_shader_specialize_metal_pipeline(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *options, doe_shader **out, doe_shader_error *error)
{
    if (!options) return shader_fail(error,DOE_SHADER_INVALID_ARGUMENT,DOE_COMPILER_INPUT_ERROR,NULL);
    return specialize(module,selection,values,count,options,out,error);
}
bool doe_shader_specialization_matches(const doe_shader *shader, const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t count)
{
    return shader && shader->view.metal_sample_mask == UINT32_MAX &&
        specialization_matches(shader,module,selection,values,count);
}
bool doe_shader_metal_pipeline_matches(const doe_shader *shader, const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t count, const DoeCompilerMetalPipelineOptionsV15 *options)
{
    return options && shader && shader->view.target == DOE_SHADER_METAL &&
        shader->view.metal_sample_mask == options->sample_mask &&
        specialization_matches(shader,module,selection,values,count);
}
