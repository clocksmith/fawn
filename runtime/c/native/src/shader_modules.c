/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static void *shader_allocate(void *userdata, size_t size, size_t alignment)
{
    return native_allocate(userdata, NULL, size, alignment);
}
static void shader_deallocate(void *userdata, void *memory, size_t size, size_t alignment)
{
    native_deallocate(userdata, NULL, memory, size, alignment);
}
static void release_shader(void *userdata, void *handle)
{
    (void)userdata;
    native_shader *payload = handle;
    doe_shader_release(payload->compiled);
    free(payload);
}

WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice handle, const WGPUShaderModuleDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (!descriptor || !descriptor->nextInChain || descriptor->nextInChain->next ||
        descriptor->nextInChain->sType != WGPUSType_ShaderSourceWGSL) {
        native_error(device, WGPUErrorType_Validation, "only a single WGSL shader source is supported");
        return NULL;
    }
    const WGPUShaderSourceWGSL *source = (const WGPUShaderSourceWGSL *)descriptor->nextInChain;
    size_t size;
    if (!native_string_size(source->code, &size) || !size || size > 65536) {
        native_error(device, WGPUErrorType_Validation, "WGSL source exceeds admitted capacity");
        return NULL;
    }
    /* Capacity is explicit and compiler-owned. Exhaustion fails visibly; no
     * reparsing, fallback compiler, or claimed support beyond ABI5. */
    static const char identity[] = "experimental-c-native-abi6-no-cache";
    doe_shader_options options = {
        .allocator = {NULL, shader_allocate, shader_deallocate},
        .capacities = {
            .typed = {.source_bytes=65536, .tokens=8192, .statements=2048, .expressions=8192,
                .types=512, .members=1024, .resources=128, .access_steps=8192,
                .constants=1024, .constant_edges=4096, .ir_instructions=32768,
                .ir_operands=32768, .target_declarations=4096, .traversal_items=193,
                .words=131072, .expression_depth=64, .control_depth=32, .type_syntax_depth=64},
            .entries=64, .entry_inputs=256, .entry_references=8192},
        .selection = {.mode=DOE_COMPILER_SELECT_NONE},
        .compiler_identity=identity, .compiler_identity_size=sizeof(identity)-1
    };
    native_shader *payload = calloc(1, sizeof(*payload));
    if (!payload) {
        native_error(device, WGPUErrorType_OutOfMemory, "shader payload allocation failed");
        return NULL;
    }
    doe_shader_status status = doe_shader_create(&options, source->code.data, size,
        &payload->compiled, &payload->error);
    if (status != DOE_SHADER_OK && !payload->error.diagnostic.message[0])
        memcpy(payload->error.diagnostic.message, "C shader compilation failed", sizeof("C shader compilation failed"));
    doe_resource_shader *shader = NULL;
    native_lock(device);
    doe_resource_status created = doe_resource_shader_create(&device->resources,
        (doe_resource_native){payload, NULL, release_shader}, &shader);
    native_unlock(device);
    if (created != DOE_RESOURCE_OK) {
        release_shader(NULL, payload);
        native_error(device, WGPUErrorType_Validation, "shader resource creation failed");
    } else {
        native_label(device, shader, descriptor->label);
        if (status != DOE_SHADER_OK)
            native_error(device, status == DOE_SHADER_OUT_OF_MEMORY ? WGPUErrorType_OutOfMemory : WGPUErrorType_Validation,
                payload->error.diagnostic.message);
    }
    return (WGPUShaderModule)shader;
}
void wgpuShaderModuleAddRef(WGPUShaderModule shader) { doe_resource_shader_retain((doe_resource_shader *)shader); }
void wgpuShaderModuleRelease(WGPUShaderModule shader) { doe_resource_shader_release((doe_resource_shader *)shader); }
