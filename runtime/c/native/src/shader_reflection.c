/* SPDX-License-Identifier: MIT
 * Binding reflection extension: projects the compiler's per-entry resource
 * closure (the same specialization pipeline creation uses) onto the retained
 * Doe binding records. No second WGSL parser or binding table. */
#include "internal.h"
#include "doe_native_reflection.h"
#include <string.h>

#define REFLECTION_CAPACITY 128u /* The module resource pool (shader_options). */

typedef struct reflection {
    DoeNativeShaderBinding records[REFLECTION_CAPACITY];
    size_t count;
} reflection;

static size_t fail(const char *kind, const char *message)
{
    native_check_report("reflection", kind, message, 0, 0);
    return DOE_NATIVE_REFLECTION_FAILED;
}

static bool project(const DoeCompilerResourceV4 *resource, DoeNativeShaderBinding *out)
{
    *out = (DoeNativeShaderBinding){.group=resource->group, .binding=resource->binding,
        .access=DOE_NATIVE_ACCESS_READ};
    switch (resource->kind) {
    case DOE_COMPILER_UNIFORM_BUFFER:
        out->kind = DOE_NATIVE_BINDING_BUFFER;
        out->addr_space = DOE_NATIVE_SPACE_UNIFORM;
        return true;
    case DOE_COMPILER_STORAGE_BUFFER:
        out->kind = DOE_NATIVE_BINDING_BUFFER;
        out->addr_space = DOE_NATIVE_SPACE_STORAGE;
        break;
    case DOE_COMPILER_SAMPLED_TEXTURE:
        out->kind = DOE_NATIVE_BINDING_TEXTURE;
        out->addr_space = DOE_NATIVE_SPACE_HANDLE;
        return true;
    case DOE_COMPILER_STORAGE_TEXTURE:
        out->kind = DOE_NATIVE_BINDING_STORAGE_TEXTURE;
        out->addr_space = DOE_NATIVE_SPACE_HANDLE;
        break;
    case DOE_COMPILER_SAMPLER:
        out->kind = DOE_NATIVE_BINDING_SAMPLER;
        out->addr_space = DOE_NATIVE_SPACE_HANDLE;
        return true;
    default:
        return false;
    }
    if (resource->access == DOE_COMPILER_READ_WRITE) out->access = DOE_NATIVE_ACCESS_READ_WRITE;
    else if (resource->access == DOE_COMPILER_WRITE) out->access = DOE_NATIVE_ACCESS_WRITE;
    else if (resource->access != DOE_COMPILER_READ) return false;
    return true;
}

/* Appends the resources one specialized entry uses, each (group, binding) once. */
static size_t collect(const doe_shader *module, const char *name, size_t size, reflection *into)
{
    DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_NAME, .name=name, .name_length=size};
    doe_shader *compiled = NULL;
    doe_shader_error error = {0};
    doe_shader_status status = doe_shader_specialize(module, &selection, NULL, 0, &compiled, &error);
    if (status != DOE_SHADER_OK)
        return fail(status == DOE_SHADER_OUT_OF_MEMORY ? "OutOfMemory" : "InvalidEntryPoint",
            error.diagnostic.message[0] ? error.diagnostic.message : "entry point specialization failed");
    doe_shader_view view;
    size_t result = 0;
    if (doe_shader_borrow(compiled, &view) != DOE_SHADER_OK || view.counts.entries != 1 || !view.entries) {
        result = fail("InvalidEntryPoint", "specialized entry has no reflection");
    } else {
        const DoeCompilerEntryV5 *entry = &view.entries[0];
        for (uint32_t i = 0; i < entry->resource_reference_count && !result; ++i) {
            const uint32_t index = view.entry_references[entry->first_resource_reference + i];
            DoeNativeShaderBinding record;
            if (index >= view.counts.resources || !project(&view.resources[index], &record)) {
                result = fail("Unsupported", "entry point uses a resource kind binding reflection cannot report");
                break;
            }
            bool seen = false;
            for (size_t j = 0; j < into->count && !seen; ++j)
                seen = into->records[j].group == record.group && into->records[j].binding == record.binding;
            if (seen) continue;
            if (into->count == REFLECTION_CAPACITY) result = fail("CapacityExceeded", "shader binding reflection exceeds capacity");
            else into->records[into->count++] = record;
        }
    }
    doe_shader_release(compiled);
    return result;
}

static size_t publish(const reflection *found, DoeNativeShaderBinding *out, size_t out_count)
{
    native_check_report(NULL, NULL, NULL, 0, 0);
    if (out) memcpy(out, found->records, (found->count < out_count ? found->count : out_count) * sizeof(*out));
    return found->count;
}

/* The module of a public shader handle, or failure with its validation detail. */
static const doe_shader *module_of(WGPUShaderModule handle, const char **failure)
{
    native_shader *payload = handle ? doe_resource_shader_native((doe_resource_shader *)handle) : NULL;
    if (!payload) {
        *failure = "expected a live shader module";
        return NULL;
    }
    if (!payload->module) {
        *failure = payload->error.diagnostic.message[0] ? payload->error.diagnostic.message :
            "shader module failed WGSL validation";
        return NULL;
    }
    return payload->module;
}

size_t doeNativeShaderModuleGetBindings(WGPUShaderModule handle, DoeNativeShaderBinding *out, size_t out_count)
{
    const char *failure = NULL;
    const doe_shader *module = module_of(handle, &failure);
    if (!module) return fail("InvalidShaderModule", failure);
    doe_shader_module_view metadata;
    if (doe_shader_borrow_module(module, &metadata) != DOE_SHADER_OK)
        return fail("InvalidShaderModule", "shader module has no reflection metadata");
    reflection found = {0};
    for (size_t i = 0; i < metadata.entry_count; ++i) {
        const DoeCompilerNameV4 name = metadata.entries[i].name;
        if (name.offset > metadata.name_bytes || name.length > metadata.name_bytes - name.offset)
            return fail("InvalidShaderModule", "shader module entry name is out of range");
        if (collect(module, metadata.names + name.offset, name.length, &found)) return DOE_NATIVE_REFLECTION_FAILED;
    }
    return publish(&found, out, out_count);
}

size_t doeNativeShaderModuleGetBindingsForEntryPoint(WGPUShaderModule handle, const char *entry_point,
    size_t entry_point_size, DoeNativeShaderBinding *out, size_t out_count)
{
    const char *failure = NULL;
    const doe_shader *module = module_of(handle, &failure);
    if (!module) return fail("InvalidShaderModule", failure);
    if (!entry_point || !entry_point_size) return fail("UnknownIdentifier", "expected an entry point name");
    doe_shader_module_view metadata;
    if (doe_shader_borrow_module(module, &metadata) != DOE_SHADER_OK)
        return fail("InvalidShaderModule", "shader module has no reflection metadata");
    bool declared = false;
    for (size_t i = 0; i < metadata.entry_count && !declared; ++i) {
        const DoeCompilerNameV4 name = metadata.entries[i].name;
        declared = name.offset <= metadata.name_bytes && name.length <= metadata.name_bytes - name.offset &&
            name.length == entry_point_size && !memcmp(metadata.names + name.offset, entry_point, entry_point_size);
    }
    if (!declared) return fail("UnknownIdentifier", "shader module has no entry point with that name");
    reflection found = {0};
    if (collect(module, entry_point, entry_point_size, &found)) return DOE_NATIVE_REFLECTION_FAILED;
    return publish(&found, out, out_count);
}
