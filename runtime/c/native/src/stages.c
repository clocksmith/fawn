/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool reject(native_pipeline_error *error, WGPUErrorType type, const char *message)
{
    error->type = type;
    const native_failure failure = native_failure_status(NATIVE_FAILURE);
    native_failure_format(&failure,message,error->message);
    return false;
}

static bool selected_entry(const doe_shader_module_view *module, WGPUStringView name,
    uint32_t stage, const char **selected)
{
    size_t length;
    if (!native_string_size(name,&length) ||
        (name.data && (!length || memchr(name.data,0,length)))) return false;
    const DoeCompilerModuleEntryV13 *found = NULL;
    for (size_t i = 0; i < module->entry_count; ++i) {
        const DoeCompilerModuleEntryV13 *entry = &module->entries[i];
        if (entry->stage != stage) continue;
        if (entry->name.offset > module->name_bytes ||
            entry->name.length >= module->name_bytes - entry->name.offset ||
            module->names[entry->name.offset + entry->name.length] != 0) return false;
        if (name.data && (entry->name.length != length ||
            memcmp(module->names + entry->name.offset,name.data,length))) continue;
        if (found) return false;
        found = entry;
    }
    if (!found) return false;
    *selected = module->names + found->name.offset;
    return true;
}

static bool target_admitted(native_device *device, const doe_shader_view *view)
{
    if (device->backend.kind == WGPUBackendType_Vulkan)
        return view->target == DOE_SHADER_SPIRV &&
            native_shader_capabilities(view,&device->backend.value.vulkan.caps);
    if (device->backend.kind == WGPUBackendType_Metal)
        return view->target == DOE_SHADER_METAL && !view->counts.words &&
            !view->words && view->target_language_version == 300;
    if (device->backend.kind == WGPUBackendType_D3D12)
        return view->target == DOE_SHADER_HLSL && !view->counts.words &&
            !view->words && view->target_language_version == 600;
    return false;
}

/* Only ABI spans/strings are converted here. Compiler ABI13 owns key identity,
 * duplicate handling, value conversion and every override-expression rule. */
static bool constant_values(const WGPUConstantEntry *constants, size_t count,
    DoeCompilerOverrideValueV13 *values)
{
    for (size_t i = 0; i < count; ++i) {
        size_t length;
        if (constants[i].nextInChain || !native_string_size(constants[i].key,&length) ||
            !constants[i].key.data || !length ||
            memchr(constants[i].key.data,0,length)) return false;
        values[i] = (DoeCompilerOverrideValueV13){constants[i].key.data,length,constants[i].value};
    }
    return true;
}

static bool stage_load(native_device *device, WGPUShaderModule module, WGPUStringView name,
    uint32_t stage, const WGPUConstantEntry *constants, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, native_stage *out, native_pipeline_error *error)
{
    if (!device || !module || doe_resource_shader_device((doe_resource_shader *)module) != device)
        return reject(error,WGPUErrorType_Validation,"pipeline stage requires a shader from this device");
    native_device_retain(device);
    doe_resource_shader_retain((doe_resource_shader *)module);
    native_shader *payload = doe_resource_shader_native((doe_resource_shader *)module);
    native_stage result = {0};
    if (payload && payload->imported.data) {
        bool valid=count == 0;
        if (!valid) reject(error,WGPUErrorType_Validation,"native source imports do not accept WGSL override constants");
        if (valid) valid=native_shader_import_select(device,&payload->imported,name,stage,&result,error);
        if (valid) {
            result.imported_module=(doe_resource_shader *)module;
            doe_resource_shader_retain(result.imported_module);
        }
        doe_resource_shader_release((doe_resource_shader *)module);
        native_device_release(device);
        if (!valid) { native_stage_release(&result); return false; }
        *out=result;
        out->entry=&out->imported_entry;
        out->view.entries=&out->imported_entry;
        out->view.names=out->name;
        return true;
    }
    DoeCompilerOverrideValueV13 *values = NULL;
    doe_shader_module_view metadata;
    bool valid = payload && payload->module &&
        doe_shader_borrow_module(payload->module,&metadata) == DOE_SHADER_OK;
    if (!valid) reject(error,WGPUErrorType_Validation,payload && payload->error.diagnostic.message[0] ?
        payload->error.diagnostic.message : "shader module creation failed; see compilation info");
    if (valid && (!selected_entry(&metadata,name,stage,&result.name) || count > metadata.override_count ||
        (count && !constants) || count > SIZE_MAX / sizeof(*values)))
        valid = reject(error,WGPUErrorType_Validation,"stage entry selection or pipeline constant span is invalid");
    if (valid && count) {
        values = calloc(count,sizeof(*values));
        if (!values) valid = reject(error,WGPUErrorType_OutOfMemory,"pipeline constant conversion allocation failed");
        else if (!constant_values(constants,count,values))
            valid = reject(error,WGPUErrorType_Validation,"pipeline constant chain or key string is invalid");
    }
    if (valid) {
        DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_NAME,
            .name=result.name,.name_length=strlen(result.name)};
        doe_shader_error detail = {0};
        const doe_shader_status status = pipeline ?
            doe_shader_specialize_metal_pipeline(payload->module,&selection,values,count,pipeline,&result.compiled,&detail) :
            doe_shader_specialize(payload->module,&selection,values,count,&result.compiled,&detail);
        if (status != DOE_SHADER_OK)
            valid = reject(error,status == DOE_SHADER_OUT_OF_MEMORY ? WGPUErrorType_OutOfMemory :
                WGPUErrorType_Validation,detail.diagnostic.message[0] ? detail.diagnostic.message :
                    "selected shader specialization failed");
    }
    if (valid) {
        valid = doe_shader_borrow(result.compiled,&result.view) == DOE_SHADER_OK &&
            result.view.counts.entries == 1 && result.view.entries &&
            target_admitted(device,&result.view);
        if (valid) {
            result.entry = &result.view.entries[0];
            const DoeCompilerNameV4 entry = result.entry->name;
            valid = result.entry->stage == stage && entry.offset <= result.view.counts.name_bytes &&
                entry.length < result.view.counts.name_bytes - entry.offset &&
                result.view.names[entry.offset + entry.length] == 0 &&
                entry.length == strlen(result.name) &&
                !memcmp(result.view.names + entry.offset,result.name,entry.length);
        }
        if (valid) result.name = result.view.names + result.entry->name.offset;
        if (!valid) reject(error,WGPUErrorType_Validation,"specialized entry identity or native capability admission failed");
    }
    free(values);
    doe_resource_shader_release((doe_resource_shader *)module);
    native_device_release(device);
    if (!valid) {
        doe_shader_release(result.compiled);
        return false;
    }
    result.visibility = stage == DOE_COMPILER_STAGE_COMPUTE ? WGPUShaderStage_Compute :
        stage == DOE_COMPILER_STAGE_VERTEX ? WGPUShaderStage_Vertex : WGPUShaderStage_Fragment;
    *out = result;
    return true;
}

bool native_stage_load(native_device *device, WGPUShaderModule module, WGPUStringView name,
    uint32_t stage, const WGPUConstantEntry *constants, size_t count,
    native_stage *out, native_pipeline_error *error)
{
    return stage_load(device,module,name,stage,constants,count,NULL,out,error);
}
bool native_fragment_load(native_device *device, const WGPUFragmentState *fragment, uint32_t sample_mask,
    native_stage *out, native_pipeline_error *error)
{
    const DoeCompilerMetalPipelineOptionsV15 options = {sample_mask};
    return stage_load(device,fragment->module,fragment->entryPoint,DOE_COMPILER_STAGE_FRAGMENT,
        fragment->constants,fragment->constantCount,
        device->backend.kind == WGPUBackendType_Metal ? &options : NULL,out,error);
}

void native_stage_release(native_stage *stage)
{
    if (!stage) return;
    /* Allocator hooks and final module release must remain outside owner locks. */
    free(stage->owned_name);
    doe_shader_release(stage->compiled);
    doe_resource_shader_release(stage->imported_module);
    memset(stage,0,sizeof(*stage));
}
