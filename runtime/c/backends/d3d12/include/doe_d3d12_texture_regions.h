/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_TEXTURE_REGIONS_H
#define DOE_D3D12_TEXTURE_REGIONS_H
#include "doe_d3d12_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Single-mip/layer, four-byte color formats only. The caller owns public copy
 * admission; native offsets/pitches retain 512/256 alignment. Empty regions are
 * rejected. All outputs remain untouched on failure. */
typedef struct doe_dx_texture_region {
    uint32_t x, y, width, height;
} doe_dx_texture_region;
typedef struct doe_dx_texture_region_ex {
    doe_dx_texture_region rect;
    uint32_t mip,layer,z,aspect; /* One slice, aspect All=0/Depth=1/Stencil=2. */
} doe_dx_texture_region_ex;
doe_dx_status doe_dx_texture_region_layout(const doe_dx_texture *,doe_dx_texture_region_ex,
    doe_dx_texture_layout,doe_dx_texture_footprint *);
doe_dx_status doe_dx_commands_texture_region_ex(doe_dx_commands *,doe_dx_buffer *,doe_dx_texture_layout,
    doe_dx_texture *,doe_dx_texture_region_ex,bool,doe_dx_error *);
doe_dx_status doe_dx_commands_blit_texture_ex(doe_dx_commands *,doe_dx_texture *,doe_dx_texture_region_ex,
    doe_dx_texture *,doe_dx_texture_region_ex,doe_dx_error *);
doe_dx_status doe_dx_texture_region_footprint(const doe_dx_texture_desc *texture,
    doe_dx_texture_region region, doe_dx_texture_layout layout, doe_dx_texture_footprint *out);
/* Initialized DEFAULT or UPLOAD sources; DEFAULT or READBACK destinations.
 * Partial uploads require initialized textures, and partial/padded downloads
 * require initialized destination buffers. One existing command owns all leases. */
doe_dx_status doe_dx_commands_texture_region(doe_dx_commands *commands,
    doe_dx_buffer *buffer, doe_dx_texture_layout layout, doe_dx_texture *texture,
    doe_dx_texture_region region, bool upload, doe_dx_error *error);
doe_dx_status doe_dx_commands_blit_texture(doe_dx_commands *commands,
    doe_dx_texture *source, doe_dx_texture_region from, doe_dx_texture *destination,
    uint32_t destination_x, uint32_t destination_y, doe_dx_error *error);
#ifdef __cplusplus
}
#endif
#endif
