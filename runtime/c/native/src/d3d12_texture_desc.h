/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_TEXTURE_DESC_H
#define DOE_NATIVE_D3D12_TEXTURE_DESC_H
#include "../../resources/include/doe_resources.h"
#include "../../backends/d3d12/include/doe_d3d12_texture_regions.h"
/* Call after canonical validation; this maps only composed native shapes/uses.
 * Outputs are unchanged on rejection. Actual device format queries occur during
 * private allocation; this conversion never fabricates format support. */
bool native_d3d12_texture_descriptor(const doe_resource_texture_desc *source,doe_dx_texture_desc *out);
bool native_d3d12_texture_region(const doe_resource_texture_copy *copy,doe_dx_texture_region *out);
bool native_d3d12_sampler_supported(const WGPUSamplerDescriptor *normalized);
#endif
