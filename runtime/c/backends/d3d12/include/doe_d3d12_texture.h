/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_TEXTURE_H
#define DOE_D3D12_TEXTURE_H
#include "doe_d3d12.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_dx_texture doe_dx_texture;
/* Explicit implementation ceiling, checked against the real Windows SDK. */
/* FL11+ resource bounds; actual format/allocation support is still queried.
 * https://learn.microsoft.com/en-us/windows/win32/direct3d12/constants */
enum { DOE_DX_MAX_TEXTURE_DIMENSION_1D = 16384, DOE_DX_MAX_TEXTURE_DIMENSION_2D = 16384,
    DOE_DX_MAX_TEXTURE_DIMENSION_3D = 2048, DOE_DX_MAX_TEXTURE_ARRAY_LAYERS = 2048 };
/* Backend formats, not WebGPU enum values. All admitted formats have 4-byte
 * texels; actual SDK format support is checked during native allocation. */
typedef enum doe_dx_format {
#define DOE_DX_FORMAT(name,public,sdk,bytes,block,kind) DOE_DX_##name,
#include "doe_d3d12_formats.inc"
#undef DOE_DX_FORMAT
} doe_dx_format;
enum {
    DOE_DX_TEXTURE_COPY_SRC = 1, DOE_DX_TEXTURE_COPY_DST = 2,
    DOE_DX_TEXTURE_SAMPLED = 4, DOE_DX_TEXTURE_STORAGE = 8,
    DOE_DX_TEXTURE_RENDER_ATTACHMENT = 16, /* RGBA8/BGRA8 UNORM. */
    DOE_DX_TEXTURE_DEPTH_ATTACHMENT = 32 /* D32_FLOAT only. */
};
typedef struct doe_dx_texture_desc {
    uint32_t width, height;
    doe_dx_format format;
    uint32_t usage;
} doe_dx_texture_desc;
typedef struct doe_dx_texture_view_desc {
    doe_dx_format format;
    uint32_t dimension; /* 1D=1,2D=2,2DArray=3,Cube=4,CubeArray=5,3D=6. */
    uint32_t base_mip,mip_count,base_layer,layer_count;
    uint32_t aspect; /* All=0,depth=1,stencil=2. */
} doe_dx_texture_view_desc;
doe_dx_status doe_dx_texture_view_validate(const doe_dx_texture *,const doe_dx_texture_view_desc *,doe_dx_texture_view_desc *);
typedef struct doe_dx_texture_shape {
    doe_dx_texture_desc image;
    uint32_t dimension; /* 1,2,3; immutable resource dimension, not view kind. */
    uint32_t depth_or_layers,mip_levels,samples;
} doe_dx_texture_shape;
doe_dx_status doe_dx_texture_create_shape(doe_dx_context *,const doe_dx_texture_shape *,doe_dx_texture **,doe_dx_error *);
doe_dx_status doe_dx_texture_shape_description(const doe_dx_texture *,doe_dx_texture_shape *);
/* Explicit subset: 2D, one mip, one layer, one sample, immutable same-format
 * whole-resource views. The additive samples constructor admits sample4 render/depth attachments;
 * no compressed, sRGB or reinterpretation yet.
 * D32_FLOAT requires depth attachment usage and permits only full copy usages
 * in addition; it cannot be a sampled/storage binding.
 * A full upload/copy initializes the texture; dispatch never implies full writes. */
doe_dx_status doe_dx_texture_create(doe_dx_context *context,
    const doe_dx_texture_desc *desc, doe_dx_texture **out, doe_dx_error *error);
/* Additive sample contract; old constructor remains exactly single-sample. */
doe_dx_status doe_dx_texture_create_samples(doe_dx_context *,const doe_dx_texture_desc *,
    uint32_t samples,doe_dx_texture **,doe_dx_error *);
uint32_t doe_dx_texture_samples(const doe_dx_texture *);
/* Real whole-allocation zeroing, including every mip/layer/sample and aspect. */
doe_dx_status doe_dx_commands_initialize_texture(doe_dx_commands *,doe_dx_texture *,doe_dx_error *);
doe_dx_status doe_dx_texture_retain(doe_dx_texture *texture);
void doe_dx_texture_release(doe_dx_texture *texture);
doe_dx_status doe_dx_texture_description(const doe_dx_texture *texture, doe_dx_texture_desc *out);

typedef struct doe_dx_texture_layout {
    uint64_t offset; /* 512-byte alignment. */
    uint32_t row_pitch; /* 0 chooses minimum; otherwise multiple of 256. */
} doe_dx_texture_layout;
typedef struct doe_dx_texture_footprint {
    uint64_t offset, span; /* Span ends at the last occupied byte, not final padding. */
    uint32_t row_pitch, row_bytes, rows;
} doe_dx_texture_footprint;
/* Pure normalized layout/capacity arithmetic; leaves output unchanged on failure. */
doe_dx_status doe_dx_texture_copy_footprint(const doe_dx_texture *texture,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out);
/* Full-resource transfers only. Upload requires an initialized UPLOAD buffer.
 * Readback requires READBACK memory. Padding is NOT written by native texture
 * copies: initialize a padded/partial destination with a full buffer copy first
 * (the same command may do so). Unpadded whole-buffer readback initializes it. */
doe_dx_status doe_dx_commands_upload_texture(doe_dx_commands *commands,
    doe_dx_buffer *source, doe_dx_texture_layout layout, doe_dx_texture *destination,
    doe_dx_error *error);
doe_dx_status doe_dx_commands_read_texture(doe_dx_commands *commands,
    doe_dx_texture *source, doe_dx_buffer *destination, doe_dx_texture_layout layout,
    doe_dx_error *error);
doe_dx_status doe_dx_commands_copy_texture(doe_dx_commands *commands,
    doe_dx_texture *source, doe_dx_texture *destination, doe_dx_error *error);

typedef struct doe_dx_texture_binding_layout {
    uint32_t shader_register, register_space;
    doe_dx_format format;
    doe_dx_binding_access access; /* READ_ONLY=SRV; READ_WRITE=typed UAV. */
} doe_dx_texture_binding_layout;
typedef enum doe_dx_filter { DOE_DX_NEAREST, DOE_DX_LINEAR } doe_dx_filter;
typedef enum doe_dx_address { DOE_DX_CLAMP, DOE_DX_REPEAT, DOE_DX_MIRROR } doe_dx_address;
typedef struct doe_dx_static_sampler {
    uint32_t shader_register, register_space;
    doe_dx_filter filter;
    doe_dx_address address_u, address_v;
} doe_dx_static_sampler;
/* Static samplers are copied into the pipeline/root signature, not borrowed
 * mutable objects. Noncomparison, anisotropy=1, LOD=0; dynamic samplers excluded.
 * Shader/sample-type/filter compatibility is still the compiler/frontend's job. */
typedef struct doe_dx_resource_pipeline_desc {
    doe_dx_pipeline_desc buffers;
    const doe_dx_texture_binding_layout *textures;
    size_t texture_count;
    const doe_dx_static_sampler *samplers;
    size_t sampler_count;
} doe_dx_resource_pipeline_desc;
doe_dx_status doe_dx_pipeline_create_resources(doe_dx_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error);

typedef struct doe_dx_compute_resources {
    const doe_dx_buffer_binding *buffers;
    size_t buffer_count;
    doe_dx_texture *const *textures; /* Pipeline texture-layout order. */
    size_t texture_count;
} doe_dx_compute_resources;
/* Total buffer + texture bindings <=16. No resource aliasing, including distinct
 * read-only slots. Every sampled/storage texture must already be initialized. */
doe_dx_status doe_dx_commands_dispatch_resources(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_compute_resources *resources,
    uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error);
/* One native DISPATCH argument record: three uint32 group dimensions at a
 * 4-byte-aligned offset in initialized DEFAULT memory. The argument buffer must
 * not alias any shader binding. Contents stay GPU-owned: the caller/front end
 * proves dimensions <=65535 and ordering; this backend never reads or clamps
 * arguments on the CPU. Zero dimensions retain the native no-work meaning. */
doe_dx_status doe_dx_commands_dispatch_indirect(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_compute_resources *resources,
    doe_dx_buffer *arguments, uint64_t offset, doe_dx_error *error);
#ifdef __cplusplus
}
#endif
#endif
