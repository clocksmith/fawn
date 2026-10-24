/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_TEXTURE_H
#define DOE_NATIVE_D3D12_TEXTURE_H
#include "d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_texture_regions.h"
/* Canonical resource owners retain descriptors/view parents/sampler lifetime.
 * Constructors publish only after real GPU zero initialization is terminal;
 * unknown work remains private on the existing native pending owner. */
WGPUTexture native_d3d12_create_texture(native_device *device,const WGPUTextureDescriptor *descriptor);
WGPUTextureView native_d3d12_create_view(doe_resource_texture *texture,const WGPUTextureViewDescriptor *descriptor);
WGPUSampler native_d3d12_create_sampler(native_device *device,const WGPUSamplerDescriptor *descriptor);
native_failure native_d3d12_texture_copy(native_command_handle *commands,const doe_resource_command *record);
void native_d3d12_write_texture(native_device *device,const WGPUTexelCopyTextureInfo *destination,
    const void *data,size_t data_size,const WGPUTexelCopyBufferLayout *layout,const WGPUExtent3D *size);
#endif
