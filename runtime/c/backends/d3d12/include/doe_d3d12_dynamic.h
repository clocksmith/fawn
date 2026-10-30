/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_DYNAMIC_H
#define DOE_D3D12_DYNAMIC_H
#include "doe_d3d12_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive private interface: legacy resource/static-sampler descriptions keep
 * their layouts and behavior. Constructors copy declarations, never descriptors
 * guessed from the first bound texture or sampler. All outputs are unchanged on
 * failed preflight. The existing context serialization/retirement contract holds. */
enum { DOE_DX_MAX_DYNAMIC_SAMPLERS = 16 };
typedef enum doe_dx_sample_class {
    DOE_DX_SAMPLE_EXACT = 0, DOE_DX_SAMPLE_FLOAT, DOE_DX_SAMPLE_UINT,
    DOE_DX_SAMPLE_FLOAT_MS, DOE_DX_SAMPLE_UINT_MS, DOE_DX_SAMPLE_SINT, DOE_DX_SAMPLE_SINT_MS, DOE_DX_SAMPLE_DEPTH, DOE_DX_SAMPLE_DEPTH_MS
} doe_dx_sample_class;
typedef struct doe_dx_texture_contract {
    doe_dx_sample_class sample_class; /* EXACT uses the legacy format field. */
    /* Logical storage usage, including READ_ONLY native SRV declarations. This
     * never grants public sampled usage or changes native descriptor access. */
    bool storage;
    uint32_t dimension; /* 0 retains the legacy 2D contract. */
} doe_dx_texture_contract;
typedef struct doe_dx_sampler_layout {
    uint32_t shader_register, register_space;
    bool comparison;
} doe_dx_sampler_layout;
typedef enum doe_dx_sampler_compare {
    DOE_DX_SAMPLER_NO_COMPARE, DOE_DX_SAMPLER_NEVER, DOE_DX_SAMPLER_LESS,
    DOE_DX_SAMPLER_EQUAL, DOE_DX_SAMPLER_LESS_EQUAL, DOE_DX_SAMPLER_GREATER,
    DOE_DX_SAMPLER_NOT_EQUAL, DOE_DX_SAMPLER_GREATER_EQUAL, DOE_DX_SAMPLER_ALWAYS
} doe_dx_sampler_compare;
typedef struct doe_dx_sampler_desc {
    doe_dx_filter min_filter, mag_filter, mip_filter;
    doe_dx_address address_u, address_v, address_w;
    float min_lod, max_lod;
    uint32_t max_anisotropy; /* Exactly 1..16; >1 requires all filters linear. */
    doe_dx_sampler_compare compare;
} doe_dx_sampler_desc;
typedef struct doe_dx_dynamic_pipeline_desc {
    doe_dx_resource_pipeline_desc resources;
    /* NULL preserves legacy exact-format rows. Otherwise texture_count rows. */
    const doe_dx_texture_contract *texture_contracts;
    const doe_dx_sampler_layout *samplers;
    size_t sampler_count;
    uint32_t immediate_bytes; /* Aligned extent, at most64 bytes, b1 space7. */
    uint64_t immediate_required_bytes;
    uint32_t logical_size_count; /* Canonical resource_count, b2 space7; max16384. */
    const uint32_t *logical_size_indices; /* One canonical index per buffer; storage rows consume it. */
} doe_dx_dynamic_pipeline_desc;
typedef struct doe_dx_dynamic_resources {
    const doe_dx_texture_view_desc *texture_views; /* NULL: legacy whole2D rows. */
    doe_dx_compute_resources resources;
    const doe_dx_sampler_desc *samplers; /* Pipeline runtime-sampler order. */
    size_t sampler_count;
    uint32_t immediate_words[16];
    uint64_t immediate_initialized_bytes;
} doe_dx_dynamic_resources;
/* Portable validation of normalized immutable sampler values. */
doe_dx_status doe_dx_sampler_validate(const doe_dx_sampler_desc *sampler);
doe_dx_status doe_dx_pipeline_create_dynamic(doe_dx_context *context,
    const doe_dx_dynamic_pipeline_desc *desc,doe_dx_pipeline **out,doe_dx_error *error);
/* Values are copied to command-owned descriptor heaps before native recording.
 * No private sampler object, refcount, queue or registry is introduced. Compatible
 * repeated texture SRV reads share one existing resource lease/state transition;
 * any alias containing a UAV remains rejected. */
doe_dx_status doe_dx_commands_dispatch_dynamic(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline,const doe_dx_dynamic_resources *resources,
    uint32_t x,uint32_t y,uint32_t z,doe_dx_error *error);
doe_dx_status doe_dx_commands_dispatch_indirect_dynamic(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline,const doe_dx_dynamic_resources *resources,
    doe_dx_buffer *arguments,uint64_t offset,doe_dx_error *error);
#ifdef __cplusplus
}
#endif
#endif
