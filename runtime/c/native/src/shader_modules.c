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
    doe_shader_release(payload->module);
    free(payload);
}

uint32_t native_shader_semantic_flags(void)
{
    /* Raw uniform lowering supplies natural WGSL layout on every composed
     * target without requiring a backend layout extension. This exact policy
     * also owns language-feature reporting; standalone defaults stay conservative. */
    return DOE_COMPILER_CORE_BINDING_NUMBERS | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_DISABLE_F16 | DOE_COMPILER_DISABLE_SUBGROUPS;
}

static doe_shader_options shader_options(doe_shader_target target)
{
    /* Capacity is explicit and compiler-owned. Exhaustion fails visibly without
     * growing pools or retrying through another compiler. */
    static const char identity[] = "experimental-c-native-no-cache";
    doe_shader_options options = {
        .target=target,.semantic_flags=native_shader_semantic_flags(),
        .allocator = {NULL, shader_allocate, shader_deallocate},
        .capacities = {
            .typed = {.source_bytes=65536, .tokens=8192, .statements=2048, .expressions=8192,
                .types=512, .members=1024, .resources=128, .access_steps=8192,
                .constants=1024, .constant_edges=4096, .ir_instructions=32768,
                .ir_operands=32768, .target_declarations=4096, .traversal_items=193,
                .words=131072, .expression_depth=64, .control_depth=32, .type_syntax_depth=64},
            .entries=64, .entry_inputs=256, .entry_references=8192},
        .selection = {.mode=DOE_COMPILER_SELECT_NONE},
        .interface_attributes=4096, .stage_interfaces=1024, .sampling_pairs=32768,
        .compiler_identity=identity, .compiler_identity_size=sizeof(identity)-1
    };
    return options;
}

doe_shader_status native_compile_shader_target(doe_shader_target target, const char *source, size_t size,
    doe_shader **out, doe_shader_error *error)
{
    const doe_shader_options options = shader_options(target);
    return doe_shader_create(&options, source, size, out, error);
}

doe_shader_status native_compile_shader(const char *source, size_t size, doe_shader **out, doe_shader_error *error)
{
    return native_compile_shader_target(DOE_SHADER_SPIRV,source,size,out,error);
}

doe_shader_status native_check_shader_module(doe_shader_target target, const char *source, size_t size,
    doe_shader_error *error)
{
    /* The same whole-module validation and emission createShaderModule runs. */
    const doe_shader_options options = shader_options(target);
    doe_shader *module = NULL;
    doe_shader_status status = doe_shader_create_module(&options,options.capacities.typed.expressions,
        source,size,&module,error);
    if (module) doe_shader_release(module);
    return status;
}

static WGPUShaderModule create_shader_module(native_device *device, const WGPUShaderModuleDescriptor *descriptor)
{
    if (!descriptor || !descriptor->nextInChain || descriptor->nextInChain->next ||
        descriptor->nextInChain->sType != WGPUSType_ShaderSourceWGSL) {
        native_error(device, WGPUErrorType_Validation, "only a single WGSL shader source is supported");
        return NULL;
    }
    const WGPUShaderSourceWGSL *source = (const WGPUShaderSourceWGSL *)descriptor->nextInChain;
    size_t size;
    if (!native_string_size(source->code, &size) || size > 65536) {
        native_error(device, WGPUErrorType_Validation, "WGSL source exceeds admitted capacity");
        return NULL;
    }
    native_shader *payload = calloc(1, sizeof(*payload));
    if (!payload) {
        native_error(device, WGPUErrorType_OutOfMemory, "shader payload allocation failed");
        return NULL;
    }
    doe_shader_target target = device->backend.kind == WGPUBackendType_Metal ? DOE_SHADER_METAL :
        device->backend.kind == WGPUBackendType_D3D12 ? DOE_SHADER_HLSL : DOE_SHADER_SPIRV;
    doe_shader_options options = shader_options(target);
    if (native_feature_enabled(device, WGPUFeatureName_ShaderF16))
        options.semantic_flags &= ~DOE_COMPILER_DISABLE_F16;
    if (native_feature_enabled(device, WGPUFeatureName_Subgroups))
        options.semantic_flags &= ~DOE_COMPILER_DISABLE_SUBGROUPS;
    doe_shader_status status = doe_shader_create_module(&options,options.capacities.typed.expressions,
        source->code.data,size,&payload->module,&payload->error);
    if (status != DOE_SHADER_OK && !payload->error.diagnostic.message[0])
        memcpy(payload->error.diagnostic.message, "C shader validation failed", sizeof("C shader validation failed"));
    doe_resource_shader *shader = NULL;
    native_lock(device);
    doe_resource_status created = doe_resource_shader_create(&device->resources,
        (doe_resource_native){payload, NULL, release_shader}, &shader);
    native_unlock(device);
    if (created != DOE_RESOURCE_OK) {
        release_shader(NULL, payload);
        native_error(device, native_resource_error(created), "shader resource creation failed");
    } else {
        native_label(device, shader, descriptor->label);
        if (status != DOE_SHADER_OK)
            native_error(device, status == DOE_SHADER_OUT_OF_MEMORY ? WGPUErrorType_OutOfMemory : WGPUErrorType_Validation,
                payload->error.diagnostic.message);
    }
    return (WGPUShaderModule)shader;
}
WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice handle, const WGPUShaderModuleDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (native_device_dead(device)) return native_lost_shader(device, (descriptor ? descriptor->label : (WGPUStringView){NULL, 0}));
    native_device_retain(device);
    WGPUShaderModule shader = create_shader_module(device,descriptor);
    native_device_release(device);
    return shader;
}
void wgpuShaderModuleAddRef(WGPUShaderModule shader) { doe_resource_shader_retain((doe_resource_shader *)shader); }
void wgpuShaderModuleRelease(WGPUShaderModule shader) { doe_resource_shader_release((doe_resource_shader *)shader); }
