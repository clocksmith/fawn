/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_SHADER_SOURCES_H
#define DOE_NATIVE_SHADER_SOURCES_H
#include "webgpu.h"

/* Retained Doe shader-descriptor chain values. These are contextual native
 * extensions, NOT additions to the upstream WGPUSType namespace: values 3/5
 * have different meanings on render-pass and surface descriptors. */
#define DOE_NATIVE_STYPE_SHADER_SOURCE_MSL ((WGPUSType)3u)
#define DOE_NATIVE_STYPE_SHADER_SOURCE_HLSL ((WGPUSType)5u)
#define DOE_NATIVE_MSL_BINDINGS_PER_GROUP 32u
typedef struct DoeNativeShaderSourceMSL {
    WGPUChainedStruct chain;
    WGPUStringView code;
    uint32_t workgroup_size_x, workgroup_size_y, workgroup_size_z;
} DoeNativeShaderSourceMSL;
typedef struct DoeNativeShaderSourceHLSL {
    WGPUChainedStruct chain;
    WGPUStringView code;
    uint32_t workgroup_size_x, workgroup_size_y, workgroup_size_z;
} DoeNativeShaderSourceHLSL;

/* Use one source chain with wgpuDeviceCreateShaderModule on its matching native
 * backend. SPIR-V uses upstream WGPUShaderSourceSPIRV (codeSize is word count).
 * Source bytes are copied. MSL library validation occurs at module creation;
 * HLSL target compilation occurs at pipeline creation.
 * These are native/degraded imports, not WGSL translation or robustness proofs.
 * No bind-group, sampling-pair or immediate-data metadata is inferred. Explicit
 * layouts describe resources; automatic layout is empty, not an inference that
 * the source uses no resources. Overrides and generated WGSL target carriers do
 * not apply. MSL/HLSL zero workgroup dimensions retain the legacy value one.
 * Caller-supplied native code must obey the selected backend binding contract. */
#endif
