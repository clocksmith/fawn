/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

bool native_stage_load(native_device *device, WGPUShaderModule module, WGPUStringView name,
    uint32_t stage, native_stage *out)
{
    if (!module || doe_resource_shader_device((doe_resource_shader *)module) != device) return false;
    native_shader *payload = doe_resource_shader_native((doe_resource_shader *)module);
    if (!payload->compiled || doe_shader_borrow(payload->compiled, &out->view) != DOE_SHADER_OK) return false;
    if (device->backend.kind == WGPUBackendType_Vulkan) {
        if (out->view.target != DOE_SHADER_SPIRV ||
            !native_shader_capabilities(&out->view,&device->backend.value.vulkan.caps)) return false;
    } else if (device->backend.kind == WGPUBackendType_Metal) {
        if (out->view.target != DOE_SHADER_METAL || out->view.counts.words ||
            out->view.words || out->view.target_language_version != 300) return false;
    } else if (device->backend.kind == WGPUBackendType_D3D12) {
        if (out->view.target != DOE_SHADER_HLSL || out->view.counts.words ||
            out->view.words || out->view.target_language_version != 600) return false;
    } else return false;
    size_t length;
    if (!native_string_size(name, &length) || length > 63 ||
        (name.data && (!length || memchr(name.data, 0, length)))) return false;
    const DoeCompilerEntryV5 *selected = NULL;
    for (size_t i = 0; i < out->view.counts.entries; ++i) {
        const DoeCompilerEntryV5 *entry = &out->view.entries[i];
        if (entry->stage != stage) continue;
        if (name.data && (entry->name.length != length ||
            memcmp(out->view.names + entry->name.offset, name.data, length))) continue;
        if (selected) return false;
        selected = entry;
    }
    if (!selected || selected->name.length > 63) return false;
    out->entry = selected;
    out->visibility = stage == DOE_COMPILER_STAGE_COMPUTE ? WGPUShaderStage_Compute :
        stage == DOE_COMPILER_STAGE_VERTEX ? WGPUShaderStage_Vertex : WGPUShaderStage_Fragment;
    memcpy(out->name, out->view.names + selected->name.offset, selected->name.length);
    out->name[selected->name.length] = 0;
    return true;
}
