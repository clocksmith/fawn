/* SPDX-License-Identifier: MIT
 * Doe shader binding reflection extension ABI (not webgpu.h). Signatures and
 * record layout match the retained Zig exports consumed by
 * runtime/bridge/webgpu-addon/doe_napi_shader.c (DoeShaderBindingInfo).
 * Listed in ../EXTENSION_SYMBOLS.txt, never resolved by wgpuGetProcAddress. */
#ifndef DOE_NATIVE_REFLECTION_H
#define DOE_NATIVE_REFLECTION_H
#include <stddef.h>
#include <stdint.h>
#include "webgpu.h"
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

/* Retained value codes (addon doe_binding_*_name). */
#define DOE_NATIVE_BINDING_BUFFER 0u
#define DOE_NATIVE_BINDING_SAMPLER 1u
#define DOE_NATIVE_BINDING_TEXTURE 2u
#define DOE_NATIVE_BINDING_STORAGE_TEXTURE 3u
#define DOE_NATIVE_SPACE_UNIFORM 3u
#define DOE_NATIVE_SPACE_STORAGE 4u
#define DOE_NATIVE_SPACE_HANDLE 5u
#define DOE_NATIVE_ACCESS_READ 0u
#define DOE_NATIVE_ACCESS_WRITE 1u
#define DOE_NATIVE_ACCESS_READ_WRITE 2u
/* Failure: the calls return this and set the diagnostic read by
 * doeNativeCopyLastErrorMessage (doe_native_shader_check.h). */
#define DOE_NATIVE_REFLECTION_FAILED SIZE_MAX

typedef struct DoeNativeShaderBinding {
    uint32_t group, binding, kind, addr_space, access;
} DoeNativeShaderBinding;

/* Bindings of every entry point of a valid module, each (group, binding) once,
 * in entry then declaration order. Returns the full count and writes at most
 * out_count records; a module that failed validation reports failure. Resources
 * no entry point uses are not reported: reflection comes from the compiler's
 * per-entry resource closure. */
DOE_NATIVE_EXTENSION size_t doeNativeShaderModuleGetBindings(WGPUShaderModule module,
    DoeNativeShaderBinding *out, size_t out_count);
/* Bindings the named entry point statically uses. An unknown name, or an entry
 * whose overrides have no default, reports failure. The name need not be NUL
 * terminated. */
DOE_NATIVE_EXTENSION size_t doeNativeShaderModuleGetBindingsForEntryPoint(WGPUShaderModule module,
    const char *entry_point, size_t entry_point_size, DoeNativeShaderBinding *out, size_t out_count);
#ifdef __cplusplus
}
#endif
#endif
