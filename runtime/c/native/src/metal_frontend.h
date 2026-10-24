/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_FRONTEND_H
#define DOE_NATIVE_METAL_FRONTEND_H
#include "internal.h"
WGPUTexture native_metal_create_texture(native_device *device, const WGPUTextureDescriptor *descriptor);
WGPUTextureView native_metal_create_view(doe_resource_texture *texture, const WGPUTextureViewDescriptor *descriptor);
WGPUSampler native_metal_create_sampler(native_device *device, const WGPUSamplerDescriptor *descriptor);
WGPURenderPipeline native_metal_create_render_pipeline(native_device *device,
    const WGPURenderPipelineDescriptor *descriptor, const native_stage *stages, size_t stage_count,
    native_pipeline_error *error);
native_failure native_metal_record_commands(doe_mtl_commands *commands, doe_resource_commands *resources);
void native_metal_write_texture(native_device *device, const WGPUTexelCopyTextureInfo *destination,
    const void *data, size_t data_size, const WGPUTexelCopyBufferLayout *layout, const WGPUExtent3D *size);
void native_view_release(void *userdata, void *handle);
void native_sampler_release(void *userdata, void *handle);
bool native_metal_format(uint32_t features, WGPUTextureFormat format, doe_mtl_format *out);
#endif
