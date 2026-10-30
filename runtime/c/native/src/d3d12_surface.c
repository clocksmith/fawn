/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "d3d12_texture_desc.h"
#include "d3d12_frontend.h"
#include <stdlib.h>
#if defined(_WIN32)
static const WGPUTextureUsage usages=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc|
    WGPUTextureUsage_CopyDst|WGPUTextureUsage_TextureBinding;
static WGPUTextureFormat public_format(doe_dx_format format) {
    switch(format){case DOE_DX_BGRA8_UNORM:return WGPUTextureFormat_BGRA8Unorm;
    case DOE_DX_BGRA8_UNORM_SRGB:return WGPUTextureFormat_BGRA8UnormSrgb;
    case DOE_DX_RGBA8_UNORM:return WGPUTextureFormat_RGBA8Unorm;
    case DOE_DX_RGBA8_UNORM_SRGB:return WGPUTextureFormat_RGBA8UnormSrgb;
    default:return WGPUTextureFormat_Undefined;}
}
WGPUStatus native_d3d12_surface_capabilities(native_surface *surface,native_adapter *adapter,WGPUSurfaceCapabilities *out) {
    if(!adapter||!out||out->nextInChain||adapter->backend.kind!=WGPUBackendType_D3D12)return WGPUStatus_Error;
    doe_dx_surface_caps caps={0};
    if(doe_mutex_lock(&adapter->mutex).status!=DOE_HOST_OK)abort();
    doe_dx_status status=doe_dx_surface_capabilities(adapter->backend.value.d3d12.context,
        surface->source.window.wayland_surface,&caps,NULL);
    if(doe_mutex_unlock(&adapter->mutex).status!=DOE_HOST_OK)abort();
    if(status!=DOE_DX_OK)return WGPUStatus_Error;
    WGPUTextureFormat *formats=calloc(caps.format_count,sizeof(*formats));
    WGPUPresentMode *modes=calloc(2,sizeof(*modes));WGPUCompositeAlphaMode *alpha=calloc(1,sizeof(*alpha));
    if(!formats||!modes||!alpha){free(formats);free(modes);free(alpha);return WGPUStatus_Error;}
    for(uint32_t i=0;i<caps.format_count;++i)formats[i]=public_format(caps.formats[i]);
    modes[0]=WGPUPresentMode_Fifo;modes[1]=WGPUPresentMode_Immediate;alpha[0]=WGPUCompositeAlphaMode_Opaque;
    *out=(WGPUSurfaceCapabilities){.usages=usages,.formatCount=caps.format_count,.formats=formats,
        .presentModeCount=caps.immediate?2u:1u,.presentModes=modes,.alphaModeCount=1,.alphaModes=alpha};
    return WGPUStatus_Success;
}
static bool configuration(const WGPUSurfaceConfiguration *config,native_device *device,doe_dx_surface_desc *out) {
    if(config->nextInChain||device->backend.kind!=WGPUBackendType_D3D12||!config->width||!config->height||
        config->width>device->backend.limits.maxTextureDimension2D||config->height>device->backend.limits.maxTextureDimension2D||
        !(config->usage&WGPUTextureUsage_RenderAttachment)||(config->usage&~usages)||
        (config->viewFormatCount&&!config->viewFormats))return false;
    for(size_t i=0;i<config->viewFormatCount;++i)if(config->viewFormats[i]!=config->format)return false;
    doe_dx_surface_desc desc={.width=config->width,.height=config->height,.usage=DOE_DX_TEXTURE_RENDER_ATTACHMENT};
    if(!native_d3d12_format(config->format,&desc.format)||public_format(desc.format)==WGPUTextureFormat_Undefined)return false;
    if(config->presentMode==WGPUPresentMode_Immediate)desc.immediate=true;
    else if(config->presentMode!=WGPUPresentMode_Undefined&&config->presentMode!=WGPUPresentMode_Fifo)return false;
    if(config->alphaMode!=WGPUCompositeAlphaMode_Auto&&config->alphaMode!=WGPUCompositeAlphaMode_Opaque)return false;
    if(config->usage&WGPUTextureUsage_CopySrc)desc.usage|=DOE_DX_TEXTURE_COPY_SRC;
    if(config->usage&WGPUTextureUsage_CopyDst)desc.usage|=DOE_DX_TEXTURE_COPY_DST;
    if(config->usage&WGPUTextureUsage_TextureBinding)desc.usage|=DOE_DX_TEXTURE_SAMPLED;
    *out=desc;return true;
}
static native_failure return_frame(native_surface *surface,doe_dx_texture *texture,bool present) {
    native_device *device=surface->device;native_command_handle work={0};
    native_failure failure=native_d3d12_prepare_recording(device);
    if(failure.status==NATIVE_OK)failure=native_command_create(&device->backend,&work);
    if(failure.status!=NATIVE_OK)return failure;
    doe_dx_error detail={0};
    doe_dx_status status=present?doe_dx_commands_present(work.value.d3d12.commands,surface->d3d12,texture,&detail):
        doe_dx_commands_discard_surface(work.value.d3d12.commands,surface->d3d12,texture,&detail);
    failure=native_failure_d3d12(status,&detail);
    if(failure.status==NATIVE_OK)return native_submit_backend_commands(device,work);
    if(native_command_release(&work)!=NATIVE_OK)abort();
    return failure;
}
native_pending *native_d3d12_surface_unconfigure_locked(native_surface *surface) {
    surface->configured=false;if(!surface->d3d12)return NULL;
    native_device *device=surface->device;native_lock(device);
    native_failure settled=native_settle_pending(device).failure;
    if(surface->current&&settled.status==NATIVE_OK){
        native_texture *texture=doe_resource_texture_native(surface->current);
        settled=return_frame(surface,texture->d3d12,false);
    }
    /* Unconfigure has no error delivery. Keep the first terminal receipt on
     * the existing queue owner for its next public observer; never invoke a
     * reentrant callback while surface/device mutexes are held. Pending work
     * already owns its later completion/error receipt and is not terminal. */
    if(settled.status!=NATIVE_OK&&settled.status!=NATIVE_PENDING&&device->retired_failure.status==NATIVE_OK)
        device->retired_failure=settled;
    native_surface_drop_current(surface);native_drain_pending(device);
    doe_dx_surface_release(surface->d3d12);
    native_pending *retired=surface->retirement;
    surface->d3d12=NULL;surface->device=NULL;surface->retirement=NULL;
    native_unlock(device);return retired;
}
void native_d3d12_surface_configure(native_surface *surface,const WGPUSurfaceConfiguration *config) {
    if(doe_mutex_lock(&surface->mutex).status!=DOE_HOST_OK)abort();
    native_device *device=config->device?(native_device *)config->device:surface->device;
    if(!device){if(doe_mutex_unlock(&surface->mutex).status!=DOE_HOST_OK)abort();return;}
    native_device_retain(device);doe_dx_surface_desc desc;
    native_failure failure=native_failure_status(config->device&&configuration(config,device,&desc)?NATIVE_OK:NATIVE_INVALID_ARGUMENT);
    native_pending *reservation=NULL;doe_dx_surface *created=NULL;bool existing=surface->d3d12!=NULL;
    if(existing&&surface->device!=device)failure=native_failure_status(NATIVE_UNSUPPORTED);
    native_lock(device);
    if(failure.status==NATIVE_OK)failure=native_failure_status(native_device_result(device));
    if(failure.status==NATIVE_OK)failure=native_settle_pending(device).failure;
    if(failure.status==NATIVE_OK&&!existing){reservation=calloc(1,sizeof(*reservation));
        if(!reservation)failure=native_failure_status(NATIVE_OUT_OF_MEMORY);}
    if(failure.status==NATIVE_OK){doe_dx_error detail={0};
        doe_dx_status status=existing?doe_dx_surface_reconfigure(surface->d3d12,&desc,&detail):
            doe_dx_surface_create(device->backend.value.d3d12.context,surface->source.window.wayland_surface,&desc,&created,&detail);
        failure=native_failure_d3d12(status,&detail);}
    if(failure.status==NATIVE_OK){
        if(!existing){*reservation=(native_pending){.device=device,.surface=surface};surface->retirement=reservation;
            surface->d3d12=created;surface->device=device;}
        surface->configured=true;surface->format=config->format;surface->usage=config->usage;
        surface->width=config->width;surface->height=config->height;surface->presented=NATIVE_OK;
    }else free(reservation);
    native_unlock(device);if(doe_mutex_unlock(&surface->mutex).status!=DOE_HOST_OK)abort();
    if(failure.status!=NATIVE_OK){char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure,"D3D12 surface configuration failed",message);
        native_error(device,native_result_error(failure.status),message);}
    if(existing||failure.status!=NATIVE_OK)native_device_release(device);
}
static void release_frame(void *userdata,void *handle) {
    native_texture *texture=handle;native_surface *surface=texture->surface;
    native_texture_release(userdata,handle);native_surface_release_lease(surface);
}
/* The caller retains its private texture reference until publication succeeds,
 * so failed allocation can return COPY_DEST to PRESENT before dropping it. */
static native_failure publish(native_surface *surface,doe_dx_texture *texture) {
    native_device *device=surface->device;native_texture *payload=calloc(1,sizeof(*payload));
    if(!payload)return native_failure_status(NATIVE_OUT_OF_MEMORY);
    payload->device=device;payload->d3d12=texture;payload->surface=surface;
    const WGPULimits *limits=&device->backend.limits;
    doe_resource_texture_limits bounds={limits->maxTextureDimension1D,limits->maxTextureDimension2D,
        limits->maxTextureDimension3D,limits->maxTextureArrayLayers};
    doe_resource_texture_desc desc={.dimension=WGPUTextureDimension_2D,.format=surface->format,
        .width=surface->width,.height=surface->height,.depth_or_layers=1,.mip_levels=1,.samples=1,.usage=surface->usage};
    doe_resource_status status=doe_resource_texture_create(&device->resources,&desc,&bounds,
        (doe_resource_native){payload,NULL,release_frame},&surface->current);
    if(status!=DOE_RESOURCE_OK){free(payload);return native_failure_status(status==DOE_RESOURCE_OUT_OF_MEMORY?
        NATIVE_OUT_OF_MEMORY:NATIVE_INVALID_STATE);}
    native_retain_count(&surface->leases);return native_failure_status(NATIVE_OK);
}
void native_d3d12_surface_acquire(native_surface *surface,WGPUSurfaceTexture *out) {
    if(doe_mutex_lock(&surface->mutex).status!=DOE_HOST_OK)abort();
    if(!surface->configured){if(doe_mutex_unlock(&surface->mutex).status!=DOE_HOST_OK)abort();return;}
    native_device *device=surface->device;native_device_retain(device);native_lock(device);
    native_failure failure=native_failure_status(native_device_result(device));
    if(failure.status==NATIVE_OK&&surface->presented!=NATIVE_OK)failure=native_failure_status(surface->presented);
    if(failure.status==NATIVE_OK&&!surface->current){
        failure=native_settle_pending(device).failure;native_command_handle work={0};doe_dx_texture *texture=NULL;
        if(failure.status==NATIVE_OK)failure=native_d3d12_prepare_recording(device);
        if(failure.status==NATIVE_OK)failure=native_command_create(&device->backend,&work);
        if(failure.status==NATIVE_OK){doe_dx_error detail={0};
            doe_dx_status status=doe_dx_surface_acquire(surface->d3d12,work.value.d3d12.commands,&texture,&detail);
            failure=native_failure_d3d12(status,&detail);
            if(failure.status==NATIVE_OK){status=doe_dx_commands_initialize_texture(work.value.d3d12.commands,texture,&detail);
                failure=native_failure_d3d12(status,&detail);}
            if(failure.status==NATIVE_OK)failure=native_submit_backend_commands(device,work);
            else if(native_command_release(&work)!=NATIVE_OK)abort();
        }
        if(texture){
            if(failure.status==NATIVE_OK){failure=publish(surface,texture);
                if(failure.status!=NATIVE_OK)(void)return_frame(surface,texture,false);}
            if(failure.status!=NATIVE_OK)doe_dx_texture_release(texture);
        }
        if(failure.status!=NATIVE_OK)surface->presented=failure.status;
    }
    if(failure.status==NATIVE_OK&&surface->current){doe_resource_texture_retain(surface->current);
        out->texture=(WGPUTexture)surface->current;out->status=WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    }else if(failure.status==NATIVE_PENDING)out->status=WGPUSurfaceGetCurrentTextureStatus_Timeout;
    native_unlock(device);if(doe_mutex_unlock(&surface->mutex).status!=DOE_HOST_OK)abort();
    if(failure.status!=NATIVE_OK&&failure.status!=NATIVE_PENDING){char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure,"D3D12 surface acquisition failed",message);
        native_error(device,native_result_error(failure.status),message);}
    native_device_release(device);
}
WGPUStatus native_d3d12_surface_present(native_surface *surface) {
    if(doe_mutex_lock(&surface->mutex).status!=DOE_HOST_OK)abort();
    if(!surface->configured||!surface->current){if(doe_mutex_unlock(&surface->mutex).status!=DOE_HOST_OK)abort();return WGPUStatus_Error;}
    native_device *device=surface->device;native_device_retain(device);native_lock(device);
    native_failure failure=native_failure_status(native_device_result(device));
    if(failure.status==NATIVE_OK)failure=native_settle_pending(device).failure;
    if(failure.status==NATIVE_OK){native_texture *texture=doe_resource_texture_native(surface->current);
        failure=return_frame(surface,texture->d3d12,true);}
    surface->presented=failure.status;native_surface_drop_current(surface);
    native_unlock(device);if(doe_mutex_unlock(&surface->mutex).status!=DOE_HOST_OK)abort();
    if(failure.status!=NATIVE_OK){char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure,"D3D12 surface presentation failed or unresolved",message);
        native_error(device,native_result_error(failure.status),message);}
    native_device_release(device);return failure.status==NATIVE_OK?WGPUStatus_Success:WGPUStatus_Error;
}
#else
WGPUStatus native_d3d12_surface_capabilities(native_surface *s,native_adapter *a,WGPUSurfaceCapabilities *out)
{(void)s;(void)a;(void)out;return WGPUStatus_Error;}
void native_d3d12_surface_configure(native_surface *s,const WGPUSurfaceConfiguration *c){(void)s;(void)c;}
native_pending *native_d3d12_surface_unconfigure_locked(native_surface *s){(void)s;return NULL;}
void native_d3d12_surface_acquire(native_surface *s,WGPUSurfaceTexture *out){(void)s;(void)out;}
WGPUStatus native_d3d12_surface_present(native_surface *s){(void)s;return WGPUStatus_Error;}
#endif
