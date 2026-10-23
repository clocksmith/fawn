#ifndef DOE_VK_TEXTURE_H
#define DOE_VK_TEXTURE_H
#include "doe_vk.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_vk_texture doe_vk_texture;
typedef struct doe_vk_texture_view doe_vk_texture_view;
typedef struct doe_vk_sampler doe_vk_sampler;
/* Native Vulkan format numbers from retained vk_formats.zig, NOT WebGPU enum
 * numbers. Translate public format names explicitly at the native adapter. */
typedef enum doe_vk_texture_format {
    DOE_VK_FORMAT_R8_UNORM = 9,
    DOE_VK_FORMAT_RG8_UNORM = 16,
    DOE_VK_FORMAT_RGBA8_UNORM = 37,
    DOE_VK_FORMAT_RGBA8_SRGB = 43,
    DOE_VK_FORMAT_BGRA8_UNORM = 44,
    DOE_VK_FORMAT_BGRA8_SRGB = 50,
    DOE_VK_FORMAT_R16_FLOAT = 76,
    DOE_VK_FORMAT_RG16_FLOAT = 83,
    DOE_VK_FORMAT_RGBA16_FLOAT = 97,
    DOE_VK_FORMAT_R32_UINT = 98,
    DOE_VK_FORMAT_R32_SINT = 99,
    DOE_VK_FORMAT_R32_FLOAT = 100,
    DOE_VK_FORMAT_RG32_UINT = 101,
    DOE_VK_FORMAT_RG32_SINT = 102,
    DOE_VK_FORMAT_RG32_FLOAT = 103,
    DOE_VK_FORMAT_RGBA32_UINT = 107,
    DOE_VK_FORMAT_RGBA32_SINT = 108,
    DOE_VK_FORMAT_RGBA32_FLOAT = 109
} doe_vk_texture_format;
/* Optimal-tiling facts for admitted formats, not shader-feature enablement or
 * a claim that every dimension/usage/extent combination is supported. Failure
 * leaves output unchanged. Compiler/native admission must check shader features. */
typedef struct doe_vk_format_caps {
    uint32_t texel_bytes;
    bool sampled;
    bool storage;
    bool color_attachment;
    bool blendable;
    bool filterable;
    bool copy_src;
    bool copy_dst;
} doe_vk_format_caps;
doe_vk_status doe_vk_texture_format_caps(doe_vk_context *context,
    doe_vk_texture_format format, doe_vk_format_caps *out);
typedef enum doe_vk_texture_dimension {
    DOE_VK_TEXTURE_1D,
    DOE_VK_TEXTURE_2D,
    DOE_VK_TEXTURE_3D
} doe_vk_texture_dimension;
enum {
    DOE_VK_TEXTURE_COPY_SRC = 1,
    DOE_VK_TEXTURE_COPY_DST = 2,
    DOE_VK_TEXTURE_SAMPLED = 4,
    DOE_VK_TEXTURE_STORAGE = 8,
    DOE_VK_TEXTURE_COLOR_ATTACHMENT = 16
};
typedef struct doe_vk_texture_desc {
    doe_vk_texture_dimension dimension;
    doe_vk_texture_format format;
    uint32_t width;
    uint32_t height;
    uint32_t depth_or_layers;
    uint32_t mip_levels;
    uint32_t usage;
    bool cube_compatible;
} doe_vk_texture_desc;
/* Images start uninitialized/UNDEFINED. Native WebGPU initialization ownership
 * remains upstream. A texture may be used by only one unresolved command owner;
 * its final layout is published on established successful completion only. */
doe_vk_status doe_vk_texture_create(doe_vk_context *context,
    const doe_vk_texture_desc *desc, doe_vk_texture **out, doe_vk_error *error);
doe_vk_status doe_vk_texture_retain(doe_vk_texture *texture);
void doe_vk_texture_release(doe_vk_texture *texture);

typedef enum doe_vk_view_dimension {
    DOE_VK_VIEW_1D,
    DOE_VK_VIEW_2D,
    DOE_VK_VIEW_3D,
    DOE_VK_VIEW_CUBE,
    DOE_VK_VIEW_1D_ARRAY,
    DOE_VK_VIEW_2D_ARRAY,
    DOE_VK_VIEW_CUBE_ARRAY
} doe_vk_view_dimension;
typedef struct doe_vk_texture_view_desc {
    doe_vk_view_dimension dimension;
    uint32_t base_mip;
    uint32_t mip_count;
    uint32_t base_layer;
    uint32_t layer_count;
} doe_vk_texture_view_desc;
/* A view retains its parent image. Reinterpretation/swizzle not yet admitted. */
doe_vk_status doe_vk_texture_view_create(doe_vk_texture *texture,
    const doe_vk_texture_view_desc *desc, doe_vk_texture_view **out, doe_vk_error *error);
doe_vk_status doe_vk_texture_view_retain(doe_vk_texture_view *view);
void doe_vk_texture_view_release(doe_vk_texture_view *view);

typedef enum doe_vk_filter { DOE_VK_FILTER_NEAREST, DOE_VK_FILTER_LINEAR } doe_vk_filter;
typedef enum doe_vk_address { DOE_VK_ADDRESS_REPEAT, DOE_VK_ADDRESS_MIRROR, DOE_VK_ADDRESS_CLAMP } doe_vk_address;
typedef struct doe_vk_sampler_desc {
    doe_vk_filter min_filter;
    doe_vk_filter mag_filter;
    doe_vk_filter mipmap_filter;
    doe_vk_address address_u;
    doe_vk_address address_v;
    doe_vk_address address_w;
    float min_lod;
    float max_lod;
    bool compare_enabled;
    uint32_t compare_operation; /* Vulkan NEVER=0 through ALWAYS=7. */
} doe_vk_sampler_desc;
/* No anisotropy is advertised/enabled by this baseline device owner. */
doe_vk_status doe_vk_sampler_create(doe_vk_context *context,
    const doe_vk_sampler_desc *desc, doe_vk_sampler **out, doe_vk_error *error);
doe_vk_status doe_vk_sampler_retain(doe_vk_sampler *sampler);
void doe_vk_sampler_release(doe_vk_sampler *sampler);

typedef struct doe_vk_texture_region {
    uint32_t mip;
    uint32_t origin[3];
    uint32_t extent[3]; /* Third component means depth for 3D, array layers otherwise. */
} doe_vk_texture_region;
typedef struct doe_vk_texture_buffer_layout {
    uint64_t offset;
    uint32_t bytes_per_row; /* Zero selects tightly packed rows. */
    uint32_t rows_per_image; /* Zero selects tightly packed images. */
} doe_vk_texture_buffer_layout;
doe_vk_status doe_vk_commands_upload_texture(doe_vk_commands *commands,
    doe_vk_buffer *source, doe_vk_texture_buffer_layout layout,
    doe_vk_texture *destination, doe_vk_texture_region region);
doe_vk_status doe_vk_commands_read_texture(doe_vk_commands *commands,
    doe_vk_texture *source, doe_vk_texture_region region,
    doe_vk_buffer *destination, doe_vk_texture_buffer_layout layout);
doe_vk_status doe_vk_commands_copy_texture(doe_vk_commands *commands,
    doe_vk_texture *source, doe_vk_texture_region source_region,
    doe_vk_texture *destination, doe_vk_texture_region destination_region);
#ifdef __cplusplus
}
#endif
#endif
