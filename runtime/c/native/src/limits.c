/* SPDX-License-Identifier: MIT
 * WebGPU limits. Adapter limits are backend facts intersected with the bounds
 * this frontend enforces and executes; device limits resolve requiredLimits
 * against them and the WebGPU defaults. https://gpuweb.github.io/gpuweb/#limits */
#include "internal.h"
#include "../../backends/d3d12/include/doe_d3d12_dynamic.h"
#include "../../backends/d3d12/include/doe_d3d12_mesh.h"
#include "d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_texture.h"
#include <stddef.h>

typedef struct limit_field {
    size_t offset;
    bool wide;      /* uint64_t field, otherwise uint32_t. */
    bool alignment; /* Smaller is better; otherwise larger is better. */
    uint64_t fallback;
} limit_field;

#define LIMIT32(name, value) {offsetof(WGPULimits, name), false, false, value}
#define LIMIT64(name, value) {offsetof(WGPULimits, name), true, false, value}
#define ALIGNMENT(name, value) {offsetof(WGPULimits, name), false, true, value}

enum { GROUP_VERTEX_BUDGET = 24 };

/* WebGPU core defaults (https://gpuweb.github.io/gpuweb/#limits), including
 * maxImmediateSize 64. Every adapter reports at least these, so a device that
 * requires every adapter limit (as the CTS does) resolves. setImmediates and
 * shader immediates remain absent here (compiler); the limit is only reported. */
static const limit_field fields[] = {
    LIMIT32(maxTextureDimension1D, 8192), LIMIT32(maxTextureDimension2D, 8192),
    LIMIT32(maxTextureDimension3D, 2048), LIMIT32(maxTextureArrayLayers, 256),
    LIMIT32(maxBindGroups, 4), LIMIT32(maxBindGroupsPlusVertexBuffers, GROUP_VERTEX_BUDGET),
    LIMIT32(maxBindingsPerBindGroup, 1000),
    LIMIT32(maxDynamicUniformBuffersPerPipelineLayout, 8), LIMIT32(maxDynamicStorageBuffersPerPipelineLayout, 4),
    LIMIT32(maxSampledTexturesPerShaderStage, 16), LIMIT32(maxSamplersPerShaderStage, 16),
    LIMIT32(maxStorageBuffersPerShaderStage, 8), LIMIT32(maxStorageTexturesPerShaderStage, 4),
    LIMIT32(maxUniformBuffersPerShaderStage, 12),
    LIMIT64(maxUniformBufferBindingSize, 65536), LIMIT64(maxStorageBufferBindingSize, 134217728),
    ALIGNMENT(minUniformBufferOffsetAlignment, 256), ALIGNMENT(minStorageBufferOffsetAlignment, 256),
    LIMIT32(maxVertexBuffers, 8), LIMIT64(maxBufferSize, 268435456), LIMIT32(maxVertexAttributes, 16),
    LIMIT32(maxVertexBufferArrayStride, 2048), LIMIT32(maxInterStageShaderVariables, 16),
    LIMIT32(maxColorAttachments, 8), LIMIT32(maxColorAttachmentBytesPerSample, 32),
    LIMIT32(maxComputeWorkgroupStorageSize, 16384), LIMIT32(maxComputeInvocationsPerWorkgroup, 256),
    LIMIT32(maxComputeWorkgroupSizeX, 256), LIMIT32(maxComputeWorkgroupSizeY, 256),
    LIMIT32(maxComputeWorkgroupSizeZ, 64), LIMIT32(maxComputeWorkgroupsPerDimension, 65535),
    LIMIT32(maxImmediateSize, 64),
};

static uint64_t field_get(const WGPULimits *limits, const limit_field *field)
{
    const unsigned char *base = (const unsigned char *)limits + field->offset;
    return field->wide ? *(const uint64_t *)(const void *)base : *(const uint32_t *)(const void *)base;
}

static void field_set(WGPULimits *limits, const limit_field *field, uint64_t value)
{
    unsigned char *base = (unsigned char *)limits + field->offset;
    if (field->wide) *(uint64_t *)(void *)base = value;
    else *(uint32_t *)(void *)base = (uint32_t)value;
}

static bool better(const limit_field *field, uint64_t a, uint64_t b)
{
    return field->alignment ? a < b : a > b;
}

static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }

/* Per-stage binding counts this frontend's pipeline storage holds for two
 * stages; Vulkan's own per-stage descriptor limits are larger on both
 * reference drivers and still bound the result. */
enum {
    SAMPLED_PER_STAGE = 16, SAMPLERS_PER_STAGE = 16, STORAGE_BUFFERS_PER_STAGE = 8,
    STORAGE_TEXTURES_PER_STAGE = 4, UNIFORM_BUFFERS_PER_STAGE = 12
};
_Static_assert(2 * (SAMPLED_PER_STAGE + SAMPLERS_PER_STAGE + STORAGE_BUFFERS_PER_STAGE +
    STORAGE_TEXTURES_PER_STAGE + UNIFORM_BUFFERS_PER_STAGE) <= NATIVE_MAX_PIPELINE_BINDINGS,
    "pipeline binding storage must hold the reported per-stage limits for two stages");
/* Vulkan raises the two smallest per-stage counts to what the same two-stage
 * binding storage still holds (also NATIVE_MAX_PIPELINE_BINDINGS, the Vulkan
 * backend's per-pipeline binding capacity). Sampled textures, samplers and
 * uniform buffers keep their defaults: the storage has no room for more. */
enum { VK_STORAGE_BUFFERS_PER_STAGE = 10, VK_STORAGE_TEXTURES_PER_STAGE = 8 };
_Static_assert(2 * (SAMPLED_PER_STAGE + SAMPLERS_PER_STAGE + VK_STORAGE_BUFFERS_PER_STAGE +
    VK_STORAGE_TEXTURES_PER_STAGE + UNIFORM_BUFFERS_PER_STAGE) <= NATIVE_MAX_PIPELINE_BINDINGS,
    "pipeline binding storage must hold the Vulkan per-stage limits for two stages");
/* WebGPU's largest render-target byte cost is 16 (rgba32 formats). Vulkan has
 * no per-sample color byte limit, so every combination of the admitted slots
 * fits: the slot count is the only bound. */
enum { MAX_COLOR_TARGET_BYTES = 16 };
/* The C compiler admits workgroup dimensions and their product only up to 256
 * (runtime/c/compiler/src/sema_declarations.c); larger Vulkan values would be
 * reported but never compile. */
enum { COMPILER_WORKGROUP_BOUND = 256 };
/* Dynamic offsets are applied by the resource owner; one bind group holds at
 * most DOE_RESOURCE_MAX_DYNAMIC_OFFSETS of them. */
enum { DYNAMIC_UNIFORM = 16, DYNAMIC_STORAGE = 16 };
_Static_assert(DYNAMIC_UNIFORM + DYNAMIC_STORAGE <= DOE_RESOURCE_MAX_DYNAMIC_OFFSETS,
    "one group may hold every dynamic buffer the limits admit");

WGPUStatus native_vulkan_limits(const doe_vk_caps *caps, WGPULimits *out)
{
    if (!out || out->nextInChain) return WGPUStatus_Error;
    uint32_t dispatch = min32(min32(caps->max_workgroup_count[0], caps->max_workgroup_count[1]),
        caps->max_workgroup_count[2]);
    /* One Vulkan allocation backs one buffer; bindings never exceed the buffer. */
    uint64_t buffer_size = caps->max_memory_allocation_size ? caps->max_memory_allocation_size :
        caps->max_storage_buffer_range;
    uint32_t vertex_buffers = min32(caps->max_vertex_input_bindings, DOE_VK_MAX_VERTEX_BUFFERS);
    uint32_t bind_groups = min32(caps->max_bind_groups, DOE_RESOURCE_MAX_GROUPS);
    uint32_t color_bytes = min32(caps->max_color_attachments, DOE_VK_MAX_COLOR_ATTACHMENTS) * MAX_COLOR_TARGET_BYTES;
    *out = (WGPULimits){
        .maxTextureDimension1D = caps->max_texture_dimension_1d,
        .maxTextureDimension2D = caps->max_texture_dimension_2d,
        .maxTextureDimension3D = caps->max_texture_dimension_3d,
        .maxTextureArrayLayers = caps->max_texture_array_layers,
        .maxBindGroups = bind_groups,
        /* Every admitted group/vertex-buffer combination fits; never fewer than
         * the WebGPU default this frontend already satisfies structurally. */
        .maxBindGroupsPlusVertexBuffers = bind_groups + vertex_buffers > GROUP_VERTEX_BUDGET ? bind_groups + vertex_buffers : GROUP_VERTEX_BUDGET,
        .maxVertexBuffers = vertex_buffers,
        .maxVertexAttributes = min32(caps->max_vertex_input_attributes, DOE_VK_MAX_VERTEX_ATTRIBUTES),
        .maxVertexBufferArrayStride = caps->max_vertex_input_binding_stride,
        /* Real native slot count bounded by the frontend's eight-slot storage. */
        .maxColorAttachments = min32(caps->max_color_attachments, DOE_VK_MAX_COLOR_ATTACHMENTS),
        .maxColorAttachmentBytesPerSample = color_bytes > 32 ? color_bytes : 32,
        .maxInterStageShaderVariables = min32(caps->max_vertex_output_components,
            caps->max_fragment_input_components) / 4,
        .maxBindingsPerBindGroup = DOE_RESOURCE_MAX_BINDING_NUMBER,
        .maxDynamicUniformBuffersPerPipelineLayout = DYNAMIC_UNIFORM,
        .maxDynamicStorageBuffersPerPipelineLayout = DYNAMIC_STORAGE,
        .maxStorageBuffersPerShaderStage = min32(caps->max_storage_buffers_per_stage, VK_STORAGE_BUFFERS_PER_STAGE),
        .maxUniformBuffersPerShaderStage = min32(caps->max_uniform_buffers_per_stage, UNIFORM_BUFFERS_PER_STAGE),
        .maxSampledTexturesPerShaderStage = min32(caps->max_sampled_textures_per_stage, SAMPLED_PER_STAGE),
        .maxSamplersPerShaderStage = min32(caps->max_samplers_per_stage, SAMPLERS_PER_STAGE),
        .maxStorageTexturesPerShaderStage = min32(caps->max_storage_textures_per_stage, VK_STORAGE_TEXTURES_PER_STAGE),
        .maxComputeWorkgroupStorageSize = caps->max_workgroup_storage_size,
        /* Binding sizes are multiples of 4 (WebGPU requires it of the limit). */
        .maxUniformBufferBindingSize = min64(caps->max_uniform_buffer_range, buffer_size) & ~(uint64_t)3,
        .maxStorageBufferBindingSize = min64(caps->max_storage_buffer_range, buffer_size) & ~(uint64_t)3,
        .minUniformBufferOffsetAlignment = (uint32_t)caps->uniform_offset_alignment,
        .minStorageBufferOffsetAlignment = (uint32_t)caps->storage_offset_alignment,
        .maxBufferSize = buffer_size,
        .maxComputeInvocationsPerWorkgroup = min32(caps->max_workgroup_invocations, COMPILER_WORKGROUP_BOUND),
        .maxComputeWorkgroupSizeX = min32(caps->max_workgroup_size[0], COMPILER_WORKGROUP_BOUND),
        .maxComputeWorkgroupSizeY = min32(caps->max_workgroup_size[1], COMPILER_WORKGROUP_BOUND),
        .maxComputeWorkgroupSizeZ = min32(caps->max_workgroup_size[2], COMPILER_WORKGROUP_BOUND),
        .maxComputeWorkgroupsPerDimension = dispatch,
        .maxImmediateSize = 64
    };
    return WGPUStatus_Success;
}

WGPUStatus native_metal_limits(const doe_mtl_caps *caps, WGPULimits *out)
{
    if (!out || out->nextInChain) return WGPUStatus_Error;
    /* Frontend admission envelope, not queried hardware guarantees. Real per-
     * pipeline maxTotalThreadsPerThreadgroup remains checked by the backend.
     * 256-byte offsets are an explicit conservative subset across Metal families. */
    uint64_t binding_size = (caps->max_buffer_length < UINT32_MAX ?
        caps->max_buffer_length : UINT32_MAX) & ~(uint64_t)3;
    uint32_t dimensions[3];
    for (size_t i = 0; i < 3; ++i)
        dimensions[i] = caps->max_threads_per_dimension[i] < 256 ?
            (uint32_t)caps->max_threads_per_dimension[i] : 256;
    *out = (WGPULimits){.maxBufferSize=caps->max_buffer_length,
        .maxBindGroups=4,.maxBindGroupsPlusVertexBuffers=GROUP_VERTEX_BUDGET,
        .maxBindingsPerBindGroup=DOE_RESOURCE_MAX_BINDING_NUMBER,
        /* One shared Metal buffer namespace: twelve uniforms, eight storage
         * buffers, eight vertex slots and the compiler sizes buffer fit in 31.
         * Group/vertex counts independently bound every admitted combination. */
        .maxDynamicUniformBuffersPerPipelineLayout=DYNAMIC_UNIFORM,.maxDynamicStorageBuffersPerPipelineLayout=DYNAMIC_STORAGE,
        .maxUniformBuffersPerShaderStage=UNIFORM_BUFFERS_PER_STAGE,.maxStorageBuffersPerShaderStage=STORAGE_BUFFERS_PER_STAGE,
        .maxSampledTexturesPerShaderStage=SAMPLED_PER_STAGE,.maxSamplersPerShaderStage=SAMPLERS_PER_STAGE,
        .maxStorageTexturesPerShaderStage=STORAGE_TEXTURES_PER_STAGE,
        .maxTextureDimension1D=caps->max_texture_1d,.maxTextureDimension2D=caps->max_texture_2d,
        .maxTextureDimension3D=caps->max_texture_3d,.maxTextureArrayLayers=caps->max_texture_layers,
        /* Conservative frontend graphics envelope; native creation still owns
         * real format/sample/pipeline support. These are not queried maxima. */
        .maxVertexBuffers=8,.maxVertexAttributes=16,.maxVertexBufferArrayStride=2048,
        .maxInterStageShaderVariables=15,.maxColorAttachments=8,.maxColorAttachmentBytesPerSample=32,
        .maxUniformBufferBindingSize=binding_size < 65536 ? binding_size : 65536,
        .maxStorageBufferBindingSize=binding_size,
        .minUniformBufferOffsetAlignment=256,.minStorageBufferOffsetAlignment=256,
        .maxComputeWorkgroupStorageSize=caps->max_threadgroup_memory_length > UINT32_MAX ?
            UINT32_MAX : (uint32_t)caps->max_threadgroup_memory_length,
        .maxComputeInvocationsPerWorkgroup=256,.maxComputeWorkgroupSizeX=dimensions[0],
        .maxComputeWorkgroupSizeY=dimensions[1],.maxComputeWorkgroupSizeZ=dimensions[2],
        .maxComputeWorkgroupsPerDimension=65535,.maxImmediateSize=64};
    return WGPUStatus_Success;
}

WGPUStatus native_d3d12_limits(const doe_dx_caps *caps, WGPULimits *out)
{
    if (!caps || !out || out->nextInChain || !caps->limits.uniform_offset_alignment ||
        !caps->limits.max_buffer_size || !caps->limits.max_descriptors) return WGPUStatus_Error;
    const doe_dx_limits *native = &caps->limits;
    uint64_t buffer = native->max_buffer_size & ~UINT64_C(3);
    /* Explicit 2D/single-layer implementation envelope, checked against the
     * actual format/footprint queries at allocation. Procedural graphics uses
     * one color attachment; presentation/indexed/depth/MSAA remain uncomposed. */
    *out = (WGPULimits){
        .maxTextureDimension2D = DOE_DX_MAX_TEXTURE_DIMENSION_2D,
        .maxTextureArrayLayers = 1,
        /* Single-color direct vertex/instance graphics envelope. These chosen
         * input bounds match the private8-slot/16-attribute owner and checked
         * 2048-byte strides; SDK input-format support is queried per pipeline.
         * Stage descriptors pack128/32; compute keeps its existing64/16. */
        /* WebGPU's UNORM render-target cost is8, independently of its4-byte
         * texture storage. The canonical resource owner enforces that cost. */
        .maxColorAttachments=1,.maxColorAttachmentBytesPerSample=8,.maxInterStageShaderVariables=16,
        .maxBufferSize = buffer,
        .maxBindGroups = DOE_RESOURCE_MAX_GROUPS,
        .maxBindGroupsPlusVertexBuffers = GROUP_VERTEX_BUDGET,
        .maxVertexBuffers=DOE_DX_MAX_VERTEX_BUFFERS,.maxVertexAttributes=DOE_DX_MAX_VERTEX_ATTRIBUTES,
        .maxVertexBufferArrayStride=2048,
        .maxBindingsPerBindGroup = DOE_RESOURCE_MAX_BINDING_NUMBER,
        .maxDynamicUniformBuffersPerPipelineLayout = DYNAMIC_UNIFORM,
        .maxDynamicStorageBuffersPerPipelineLayout = DYNAMIC_STORAGE,
        .maxUniformBuffersPerShaderStage = min32(UNIFORM_BUFFERS_PER_STAGE, native->max_cbvs),
        /* Reserve the real combined UAV budget for every admitted stage
         * combination, rather than advertising independently incompatible caps. */
        .maxStorageBuffersPerShaderStage = min32(STORAGE_BUFFERS_PER_STAGE,
            min32(native->max_srvs, native->max_uavs > STORAGE_TEXTURES_PER_STAGE ?
                native->max_uavs - STORAGE_TEXTURES_PER_STAGE : 0)),
        .maxSampledTexturesPerShaderStage = min32(SAMPLED_PER_STAGE,native->max_srvs),
        .maxStorageTexturesPerShaderStage = min32(STORAGE_TEXTURES_PER_STAGE,native->max_uavs),
        .maxSamplersPerShaderStage = DOE_DX_MAX_DYNAMIC_SAMPLERS,
        .maxUniformBufferBindingSize = min64(buffer, native->max_uniform_bytes),
        .maxStorageBufferBindingSize = buffer,
        .minUniformBufferOffsetAlignment = native->uniform_offset_alignment,
        .minStorageBufferOffsetAlignment = 256,
        .maxComputeWorkgroupStorageSize = native->max_compute_shared_bytes,
        .maxComputeInvocationsPerWorkgroup = min32(COMPILER_WORKGROUP_BOUND, native->max_compute_threads),
        .maxComputeWorkgroupSizeX = min32(COMPILER_WORKGROUP_BOUND, native->max_compute_size[0]),
        .maxComputeWorkgroupSizeY = min32(COMPILER_WORKGROUP_BOUND, native->max_compute_size[1]),
        .maxComputeWorkgroupSizeZ = min32(COMPILER_WORKGROUP_BOUND, native->max_compute_size[2]),
        .maxComputeWorkgroupsPerDimension = min32(native->max_dispatch_groups[0],
            min32(native->max_dispatch_groups[1], native->max_dispatch_groups[2]))
    };
    return WGPUStatus_Success;
}

/* https://gpuweb.github.io/gpuweb/#dom-gpuadapter-requestdevice: a required
 * value better than the adapter's rejects; each device limit is the better of
 * the required value and the default. An adapter below a default keeps its own
 * value: a device limit never exceeds what the adapter enforces. */
bool native_resolve_limits(const WGPULimits *supported, const WGPULimits *required, WGPULimits *out)
{
    if (required && required->nextInChain) return false;
    WGPULimits result = {0};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const limit_field *field = &fields[i];
        const uint64_t undefined = field->wide ? WGPU_LIMIT_U64_UNDEFINED : WGPU_LIMIT_U32_UNDEFINED;
        uint64_t adapter = field_get(supported, field);
        uint64_t value = field->fallback;
        uint64_t requested = required ? field_get(required, field) : undefined;
        if (requested != undefined) {
            if (field->alignment && (!requested || (requested & (requested - 1)))) return false;
            if (better(field, requested, adapter)) return false;
            if (better(field, requested, value)) value = requested;
        }
        if (better(field, value, adapter)) value = adapter;
        field_set(&result, field, value);
    }
    *out = result;
    return true;
}
