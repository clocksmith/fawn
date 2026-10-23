/* SPDX-License-Identifier: MIT
 * WebGPU limits. Adapter limits are backend facts intersected with the bounds
 * this frontend enforces and executes; device limits resolve requiredLimits
 * against them and the WebGPU defaults. https://gpuweb.github.io/gpuweb/#limits */
#include "internal.h"
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

/* WebGPU core defaults. maxImmediateSize is not a core limit; zero matches
 * the immediates this frontend does not admit. */
static const limit_field fields[] = {
    LIMIT32(maxTextureDimension1D, 8192), LIMIT32(maxTextureDimension2D, 8192),
    LIMIT32(maxTextureDimension3D, 2048), LIMIT32(maxTextureArrayLayers, 256),
    LIMIT32(maxBindGroups, 4), LIMIT32(maxBindGroupsPlusVertexBuffers, 24),
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
    LIMIT32(maxImmediateSize, 0),
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
    *out = (WGPULimits){
        .maxTextureDimension1D = caps->max_texture_dimension_1d,
        .maxTextureDimension2D = caps->max_texture_dimension_2d,
        .maxTextureDimension3D = caps->max_texture_dimension_3d,
        .maxTextureArrayLayers = caps->max_texture_array_layers,
        .maxBindGroups = bind_groups,
        /* Every admitted group/vertex-buffer combination fits; never fewer than
         * the WebGPU default this frontend already satisfies structurally. */
        .maxBindGroupsPlusVertexBuffers = bind_groups + vertex_buffers > 24 ? bind_groups + vertex_buffers : 24,
        .maxVertexBuffers = vertex_buffers,
        .maxVertexAttributes = min32(caps->max_vertex_input_attributes, DOE_VK_MAX_VERTEX_ATTRIBUTES),
        .maxVertexBufferArrayStride = caps->max_vertex_input_binding_stride,
        /* Real native slot count bounded by the frontend's eight-slot storage;
         * the WebGPU baseline byte budget (Vulkan has no per-sample byte limit). */
        .maxColorAttachments = min32(caps->max_color_attachments, DOE_VK_MAX_COLOR_ATTACHMENTS),
        .maxColorAttachmentBytesPerSample = 32,
        .maxInterStageShaderVariables = min32(caps->max_vertex_output_components,
            caps->max_fragment_input_components) / 4,
        .maxBindingsPerBindGroup = DOE_RESOURCE_MAX_BINDING_NUMBER,
        .maxDynamicUniformBuffersPerPipelineLayout = DYNAMIC_UNIFORM,
        .maxDynamicStorageBuffersPerPipelineLayout = DYNAMIC_STORAGE,
        .maxStorageBuffersPerShaderStage = min32(caps->max_storage_buffers_per_stage, STORAGE_BUFFERS_PER_STAGE),
        .maxUniformBuffersPerShaderStage = min32(caps->max_uniform_buffers_per_stage, UNIFORM_BUFFERS_PER_STAGE),
        .maxSampledTexturesPerShaderStage = min32(caps->max_sampled_textures_per_stage, SAMPLED_PER_STAGE),
        .maxSamplersPerShaderStage = min32(caps->max_samplers_per_stage, SAMPLERS_PER_STAGE),
        .maxStorageTexturesPerShaderStage = min32(caps->max_storage_textures_per_stage, STORAGE_TEXTURES_PER_STAGE),
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
        .maxComputeWorkgroupsPerDimension = dispatch
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
