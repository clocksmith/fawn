/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_TEXTURE_H
#define DOE_METAL_TEXTURE_H
#include "doe_metal.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_mtl_texture doe_mtl_texture;
typedef struct doe_mtl_texture_view doe_mtl_texture_view;
typedef struct doe_mtl_sampler doe_mtl_sampler;
/* Native Metal numbers, NOT WebGPU values. Native/public admission translates
 * formats and feature requirements before this independently owned boundary. */
typedef enum doe_mtl_format {
    DOE_MTL_R8_UNORM = 10, DOE_MTL_R8_SNORM = 12, DOE_MTL_R8_UINT = 13, DOE_MTL_R8_SINT = 14,
    DOE_MTL_R16_UNORM = 20, DOE_MTL_R16_SNORM = 22, DOE_MTL_R16_UINT = 23,
    DOE_MTL_R16_SINT = 24, DOE_MTL_R16_FLOAT = 25,
    DOE_MTL_RG8_UNORM = 30, DOE_MTL_RG8_SNORM = 32, DOE_MTL_RG8_UINT = 33, DOE_MTL_RG8_SINT = 34,
    DOE_MTL_R32_UINT = 53, DOE_MTL_R32_SINT = 54, DOE_MTL_R32_FLOAT = 55,
    DOE_MTL_RG16_UNORM = 60, DOE_MTL_RG16_SNORM = 62, DOE_MTL_RG16_UINT = 63,
    DOE_MTL_RG16_SINT = 64, DOE_MTL_RG16_FLOAT = 65,
    DOE_MTL_RGBA8_UNORM = 70, DOE_MTL_RGBA8_SRGB = 71, DOE_MTL_RGBA8_SNORM = 72,
    DOE_MTL_RGBA8_UINT = 73, DOE_MTL_RGBA8_SINT = 74,
    DOE_MTL_BGRA8_UNORM = 80, DOE_MTL_BGRA8_SRGB = 81,
    DOE_MTL_RGB10A2_UNORM = 90, DOE_MTL_RGB10A2_UINT = 91,
    DOE_MTL_RG11B10_FLOAT = 92, DOE_MTL_RGB9E5_FLOAT = 93,
    DOE_MTL_RG32_UINT = 103, DOE_MTL_RG32_SINT = 104, DOE_MTL_RG32_FLOAT = 105,
    DOE_MTL_RGBA16_UNORM = 110, DOE_MTL_RGBA16_SNORM = 112, DOE_MTL_RGBA16_UINT = 113,
    DOE_MTL_RGBA16_SINT = 114, DOE_MTL_RGBA16_FLOAT = 115,
    DOE_MTL_RGBA32_UINT = 123, DOE_MTL_RGBA32_SINT = 124, DOE_MTL_RGBA32_FLOAT = 125,
    DOE_MTL_BC1_RGBA = 130, DOE_MTL_BC1_RGBA_SRGB = 131,
    DOE_MTL_BC2_RGBA = 132, DOE_MTL_BC2_RGBA_SRGB = 133,
    DOE_MTL_BC3_RGBA = 134, DOE_MTL_BC3_RGBA_SRGB = 135,
    DOE_MTL_BC4_R_UNORM = 140, DOE_MTL_BC4_R_SNORM = 141,
    DOE_MTL_BC5_RG_UNORM = 142, DOE_MTL_BC5_RG_SNORM = 143,
    DOE_MTL_BC6H_RGB_FLOAT = 150, DOE_MTL_BC6H_RGB_UFLOAT = 151,
    DOE_MTL_BC7_RGBA = 152, DOE_MTL_BC7_RGBA_SRGB = 153,
    DOE_MTL_DEPTH16_UNORM = 250, DOE_MTL_DEPTH32_FLOAT = 252, DOE_MTL_STENCIL8 = 253
} doe_mtl_format;
typedef struct doe_mtl_format_info {
    uint32_t block_width, block_height, block_bytes;
    bool compressed, srgb, depth, stencil;
} doe_mtl_format_info;
/* Format layout facts, not hardware format-capability qualification. */
doe_mtl_status doe_mtl_texture_format_info(doe_mtl_format format, doe_mtl_format_info *out);
typedef enum doe_mtl_texture_type {
    DOE_MTL_TEXTURE_1D = 0, DOE_MTL_TEXTURE_1D_ARRAY = 1,
    DOE_MTL_TEXTURE_2D = 2, DOE_MTL_TEXTURE_2D_ARRAY = 3,
    DOE_MTL_TEXTURE_CUBE = 5, DOE_MTL_TEXTURE_CUBE_ARRAY = 6,
    DOE_MTL_TEXTURE_3D = 7
} doe_mtl_texture_type;
enum { DOE_MTL_TEXTURE_COPY_SRC = 1, DOE_MTL_TEXTURE_COPY_DST = 2,
    DOE_MTL_TEXTURE_SAMPLED = 4, DOE_MTL_TEXTURE_STORAGE = 8,
    DOE_MTL_TEXTURE_RENDER_TARGET = 16 };
typedef struct doe_mtl_texture_desc {
    doe_mtl_texture_type type;
    doe_mtl_format format;
    uint32_t width, height, depth_or_layers, mip_levels;
    uint32_t usage;
    bool allow_format_views;
} doe_mtl_texture_desc;
/* The common context serialization and borrowed-handle rules also apply here.
 * Private sample1 allocations, initially uninitialized. Cube layer counts are
 * face counts (6 per cube). Use staging-buffer transfers, including on Apple
 * unified memory; never pretend a private texture has CPU-mapped storage. */
doe_mtl_status doe_mtl_texture_create(doe_mtl_context *context,
    const doe_mtl_texture_desc *desc, doe_mtl_texture **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_texture_retain(doe_mtl_texture *texture);
void doe_mtl_texture_release(doe_mtl_texture *texture);
void *doe_mtl_texture_native(const doe_mtl_texture *texture);
const doe_mtl_texture_desc *doe_mtl_texture_description(const doe_mtl_texture *texture);

typedef enum doe_mtl_swizzle {
    DOE_MTL_SWIZZLE_ZERO, DOE_MTL_SWIZZLE_ONE, DOE_MTL_SWIZZLE_RED,
    DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA
} doe_mtl_swizzle;
typedef struct doe_mtl_texture_view_desc {
    doe_mtl_texture_type type;
    doe_mtl_format format;
    uint32_t base_mip, mip_count, base_layer, layer_count;
    doe_mtl_swizzle red, green, blue, alpha;
} doe_mtl_texture_view_desc;
/* Explicit counts; a view retains its parent allocation. Reinterpretation is
 * limited to same-format and admitted linear/sRGB pairs, with creation opt-in. */
doe_mtl_status doe_mtl_texture_view_create(doe_mtl_texture *texture,
    const doe_mtl_texture_view_desc *desc, doe_mtl_texture_view **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_texture_view_retain(doe_mtl_texture_view *view);
void doe_mtl_texture_view_release(doe_mtl_texture_view *view);
void *doe_mtl_texture_view_native(const doe_mtl_texture_view *view);

typedef enum doe_mtl_filter { DOE_MTL_FILTER_NEAREST, DOE_MTL_FILTER_LINEAR } doe_mtl_filter;
typedef enum doe_mtl_address {
    DOE_MTL_ADDRESS_CLAMP = 0, DOE_MTL_ADDRESS_REPEAT = 2, DOE_MTL_ADDRESS_MIRROR = 3
} doe_mtl_address;
typedef struct doe_mtl_sampler_desc {
    doe_mtl_filter min_filter, mag_filter, mip_filter;
    doe_mtl_address address_u, address_v, address_w;
    float min_lod, max_lod;
    uint32_t compare; /* Native NEVER=0 through ALWAYS=7; zero for noncomparison. */
    uint32_t max_anisotropy; /* 1..16; values >1 require all three linear filters. */
} doe_mtl_sampler_desc;
doe_mtl_status doe_mtl_sampler_create(doe_mtl_context *context,
    const doe_mtl_sampler_desc *desc, doe_mtl_sampler **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_sampler_retain(doe_mtl_sampler *sampler);
void doe_mtl_sampler_release(doe_mtl_sampler *sampler);
void *doe_mtl_sampler_native(const doe_mtl_sampler *sampler);

typedef struct doe_mtl_texture_region {
    uint32_t mip;
    uint32_t x, y, z; /* z is a face/array slice except for 3D texture depth. */
    uint32_t width, height, depth_or_layers;
} doe_mtl_texture_region;
typedef struct doe_mtl_texture_buffer_layout {
    uint64_t offset;
    uint32_t bytes_per_row; /* 0 chooses a 256-byte-aligned packed block row. */
    uint32_t rows_per_image; /* In block rows, 0 selects region block height. */
} doe_mtl_texture_buffer_layout;
/* Nonempty regions only. Row pitches must be 256-byte aligned; image padding
 * is not copied and does not initialize private-buffer padding bytes.
 * Uploads initialize only complete selected mip/layer subresources. Partial
 * updates require prior initialization. Texture completion uses the existing
 * command owner; unknown retains everything, failure poisons the allocation. */
doe_mtl_status doe_mtl_commands_upload_texture(doe_mtl_commands *commands,
    doe_mtl_buffer *source, doe_mtl_texture_buffer_layout layout,
    doe_mtl_texture *destination, doe_mtl_texture_region region, doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_read_texture(doe_mtl_commands *commands,
    doe_mtl_texture *source, doe_mtl_texture_region region,
    doe_mtl_buffer *destination, doe_mtl_texture_buffer_layout layout, doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_copy_texture(doe_mtl_commands *commands,
    doe_mtl_texture *source, doe_mtl_texture_region source_region,
    doe_mtl_texture *destination, doe_mtl_texture_region destination_region, doe_mtl_error *error);

enum { DOE_MTL_TEXTURE_SLOTS = 128, DOE_MTL_SAMPLER_SLOTS = 16 };
typedef struct doe_mtl_texture_binding {
    uint32_t slot;
    doe_mtl_texture_view *view;
    bool storage; /* Explicit upstream shader layout fact, not inferred. */
} doe_mtl_texture_binding;
typedef struct doe_mtl_sampler_binding { uint32_t slot; doe_mtl_sampler *sampler; } doe_mtl_sampler_binding;
/* Separate Metal buffer/texture/sampler index spaces share one dispatch owner.
 * Shader format, sample type, access and filtering compatibility stay upstream. */
doe_mtl_status doe_mtl_commands_dispatch_resources(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *buffers, size_t buffer_count,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count,
    doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error);
#ifdef __cplusplus
}
#endif
#endif
