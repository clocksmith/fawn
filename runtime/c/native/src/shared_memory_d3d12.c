/* SPDX-License-Identifier: MIT
 * Pinned Dawn DXGI ABI composed with canonical resources and pending commands.
 * Imported waits retain their native handles on the existing GPU command owner. */
#include "shared_memory_internal.h"
#include "d3d12_texture.h"
#include <stdlib.h>
#if defined(_WIN32)
_Static_assert(offsetof(WGPUSharedTextureMemoryDXGISharedHandleDescriptor,handle)==16,"pinned DXGI handle ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryDXGISharedHandleDescriptor,useKeyedMutex)==24,"pinned DXGI mutex ABI");
_Static_assert(sizeof(WGPUSharedTextureMemoryDXGISharedHandleDescriptor)==32,"pinned DXGI descriptor ABI");
_Static_assert(sizeof(WGPUSharedFenceDXGISharedHandleDescriptor)==24,"pinned DXGI fence ABI");
static native_failure result(doe_dx_status status,const doe_dx_error *detail)
{ return native_failure_d3d12(status,detail); }
static native_failure settle_texture(native_device *device,doe_dx_texture *texture)
{
    native_progress_pending(device);
    doe_dx_commands *owner=doe_dx_shared_texture_owner(texture);
    if (!owner) return native_failure_status(NATIVE_OK);
    struct timespec now,deadline;
    if (doe_monotonic_now(&now).status!=DOE_HOST_OK ||
        doe_time_add(&now,UINT64_C(5000000000),&deadline).status!=DOE_HOST_OK)
        return native_failure_status(NATIVE_FAILURE);
    return native_d3d12_wait_owner(device,owner,&deadline);
}
static native_failure queue_waits(native_device *device,WGPUSharedFence *fences,
    const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor)
{
    if (!descriptor->fenceCount) return native_failure_status(NATIVE_OK);
    native_failure failure=native_d3d12_prepare_recording(device);
    native_command_handle commands={0};
    if (failure.status==NATIVE_OK) failure=native_command_create(&device->backend,&commands);
    doe_dx_error detail={0};
    for (size_t i=0;failure.status==NATIVE_OK && i<descriptor->fenceCount;++i)
        failure=result(doe_dx_commands_wait_shared_fence(commands.value.d3d12.commands,
            fences[i]->d3d12,descriptor->signaledValues[i],&detail),&detail);
    if (failure.status==NATIVE_OK) return native_submit_backend_commands_async(device,commands);
    if (native_command_release(&commands)!=NATIVE_OK) abort();
    return failure;
}
static WGPUTextureFormat public_format(doe_dx_format format)
{
    switch (format) {
    case DOE_DX_RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
    case DOE_DX_BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
    case DOE_DX_RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
    case DOE_DX_RGB10A2_UNORM: return WGPUTextureFormat_RGB10A2Unorm;
    default: return WGPUTextureFormat_Undefined;
    }
}
#endif
WGPUSharedFence native_d3d12_import_shared_fence(native_device *device,const WGPUSharedFenceDescriptor *descriptor)
{
#if defined(_WIN32)
    const WGPUSharedFenceDXGISharedHandleDescriptor *source=(const void *)native_shared_find_chain(
        descriptor->nextInChain,DOE_SHARED_STYPE_DXGI_FENCE_DESCRIPTOR);
    if (!source || !source->handle || !native_feature_enabled(device,(WGPUFeatureName)DOE_SHARED_FEATURE_DXGI_FENCE)) return NULL;
    WGPUSharedFence fence=native_shared_allocate_fence(device);
    if (!fence) return NULL;
    native_lock(device);
    native_failure failure=native_failure_status(native_device_result(device));
    doe_dx_error detail={0};
    if (failure.status==NATIVE_OK) failure=result(doe_dx_shared_fence_import(device->backend.value.d3d12.context,
        source->handle,&fence->d3d12,&detail),&detail);
    native_unlock(device);
    if (failure.status!=NATIVE_OK) {
        native_shared_error(device,failure,"DXGI fence import failed"); wgpuSharedFenceRelease(fence); return NULL;
    }
    native_label(device,fence,descriptor->label); return fence;
#else
    (void)device; (void)descriptor; return NULL;
#endif
}
WGPUSharedTextureMemory native_d3d12_import_shared_memory(native_device *device,const WGPUSharedTextureMemoryDescriptor *descriptor)
{
#if defined(_WIN32)
    const WGPUSharedTextureMemoryDXGISharedHandleDescriptor *source=(const void *)native_shared_find_chain(
        descriptor->nextInChain,DOE_SHARED_STYPE_DXGI_MEMORY);
    if (!source || !source->handle || !native_feature_enabled(device,(WGPUFeatureName)DOE_SHARED_FEATURE_DXGI_MEMORY)) return NULL;
    WGPUSharedTextureMemory memory=calloc(1,sizeof(*memory));
    if (!memory) return NULL;
    memory->magic=SHARED_MEMORY_MAGIC; atomic_init(&memory->references,1);
    memory->device=device; native_device_retain(device);
    native_lock(device);
    native_failure failure=native_failure_status(native_device_result(device));
    doe_dx_error detail={0};
    if (failure.status==NATIVE_OK) failure=result(doe_dx_shared_texture_import(device->backend.value.d3d12.context,
        source->handle,source->useKeyedMutex!=0,&memory->d3d12,&detail),&detail);
    if (failure.status==NATIVE_OK) {
        doe_dx_texture_desc info;
        if (doe_dx_texture_description(memory->d3d12,&info)!=DOE_DX_OK) abort();
        memory->width=info.width; memory->height=info.height; memory->format=public_format(info.format);
        if (info.usage & DOE_DX_TEXTURE_COPY_SRC) memory->usage|=WGPUTextureUsage_CopySrc;
        if (info.usage & DOE_DX_TEXTURE_COPY_DST) memory->usage|=WGPUTextureUsage_CopyDst;
        if (info.usage & DOE_DX_TEXTURE_SAMPLED) memory->usage|=WGPUTextureUsage_TextureBinding;
        if (info.usage & DOE_DX_TEXTURE_STORAGE) memory->usage|=WGPUTextureUsage_StorageBinding;
        if (info.usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT) memory->usage|=WGPUTextureUsage_RenderAttachment;
    }
    native_unlock(device);
    if (failure.status!=NATIVE_OK) {
        native_shared_error(device,failure,"DXGI texture import failed"); wgpuSharedTextureMemoryRelease(memory); return NULL;
    }
    native_label(device,memory,descriptor->label); return memory;
#else
    (void)device; (void)descriptor; return NULL;
#endif
}
WGPUTexture native_d3d12_shared_create_texture(WGPUSharedTextureMemory memory,const WGPUTextureDescriptor *descriptor)
{
#if defined(_WIN32)
    native_device *device=memory->device;
    bool shape=!descriptor->nextInChain && descriptor->dimension==WGPUTextureDimension_2D &&
        descriptor->format==memory->format && descriptor->size.width==memory->width &&
        descriptor->size.height==memory->height && descriptor->size.depthOrArrayLayers==1 &&
        descriptor->mipLevelCount==1 && descriptor->sampleCount==1 && !(descriptor->usage & ~memory->usage) &&
        (!descriptor->viewFormatCount || descriptor->viewFormats);
    for (size_t i=0;shape && i<descriptor->viewFormatCount;++i) {
        WGPUTextureFormat view=descriptor->viewFormats[i];
        shape=view==memory->format ||
            (memory->format==WGPUTextureFormat_RGBA8Unorm && view==WGPUTextureFormat_RGBA8UnormSrgb) ||
            (memory->format==WGPUTextureFormat_BGRA8Unorm && view==WGPUTextureFormat_BGRA8UnormSrgb);
    }
    doe_resource_texture_desc desc={.dimension=WGPUTextureDimension_2D,.format=descriptor->format,
        .width=descriptor->size.width,.height=descriptor->size.height,.depth_or_layers=1,.mip_levels=1,
        .samples=1,.usage=descriptor->usage,.view_formats=descriptor->viewFormats,.view_format_count=descriptor->viewFormatCount};
    doe_resource_texture_limits limits={device->backend.limits.maxTextureDimension1D,device->backend.limits.maxTextureDimension2D,
        device->backend.limits.maxTextureDimension3D,device->backend.limits.maxTextureArrayLayers};
    if (!shape || doe_resource_texture_validate_with_features(&desc,&limits,device->resources.format_features)!=DOE_RESOURCE_OK) return NULL;
    shared_texture_payload *payload=calloc(1,sizeof(*payload));
    if (!payload) return NULL;
    payload->texture.device=device; payload->texture.shared_memory=memory;
    wgpuSharedTextureMemoryAddRef(memory);
    doe_resource_texture *texture=NULL;
    native_lock(device);
    doe_resource_status adopted=DOE_RESOURCE_STATE;
    if (native_device_result(device)==NATIVE_OK) {
        if (doe_dx_texture_retain(memory->d3d12)!=DOE_DX_OK) abort();
        payload->texture.d3d12=memory->d3d12;
        adopted=doe_resource_texture_create(&device->resources,&desc,&limits,
            (doe_resource_native){payload,NULL,native_shared_texture_finalize},&texture);
    }
    if (texture) payload->identity=texture;
    native_unlock(device);
    if (!texture) { native_shared_texture_finalize(NULL,payload); native_error(device,native_resource_error(adopted),"DXGI texture creation rejected"); }
    else native_label(device,texture,descriptor->label);
    return (WGPUTexture)texture;
#else
    (void)memory; (void)descriptor; return NULL;
#endif
}
WGPUStatus native_d3d12_shared_begin(WGPUSharedTextureMemory memory,doe_resource_texture *texture,
    const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor)
{
#if defined(_WIN32)
    const WGPUChainedStruct *chain=descriptor ? descriptor->nextInChain : NULL;
    unsigned chains=0,seen=0;
    while (chain) {
        unsigned bit=(uint32_t)chain->sType==DOE_SHARED_STYPE_D3D_SWAPCHAIN_BEGIN ? 1u :
            (uint32_t)chain->sType==DOE_SHARED_STYPE_D3D11_BEGIN ? 2u : 0u;
        if (!bit || (seen & bit) || ++chains>2) return WGPUStatus_Error;
        seen|=bit; chain=chain->next;
    }
    if (!descriptor || descriptor->fenceCount!=descriptor->signaledValueCount ||
        (descriptor->fenceCount && (!descriptor->fences || !descriptor->signaledValues))) return WGPUStatus_Error;
    size_t count=descriptor->fenceCount;
    if (count>SIZE_MAX/sizeof(WGPUSharedFence)) return WGPUStatus_Error;
    WGPUSharedFence *fences=count ? calloc(count,sizeof(*fences)) : NULL;
    if (count && !fences) return WGPUStatus_Error;
    size_t retained=0;
    for (;retained<count;++retained) {
        WGPUSharedFence fence=descriptor->fences[retained];
        if (!fence || fence->magic!=SHARED_FENCE_MAGIC || !fence->d3d12 ||
            fence->device->backend.kind!=WGPUBackendType_D3D12 || descriptor->signaledValues[retained]==UINT64_MAX) break;
        wgpuSharedFenceAddRef(fence); fences[retained]=fence;
    }
    wgpuSharedTextureMemoryAddRef(memory); doe_resource_texture_retain(texture);
    native_device *device=memory->device;
    native_lock(device);
    bool admitted=retained==count && memory->access==SHARED_IDLE && native_shared_matches_texture(memory,texture) &&
        native_device_result(device)==NATIVE_OK;
    if (admitted) memory->access=SHARED_WAITING;
    native_failure failure=native_failure_status(NATIVE_OK);
    if (admitted) failure=settle_texture(device,memory->d3d12);
    if (admitted && failure.status==NATIVE_OK) failure=result(doe_dx_shared_texture_end(memory->d3d12,NULL),NULL);
    native_unlock(device);
    doe_dx_error detail={0};
    if (admitted && failure.status==NATIVE_OK) failure=result(doe_dx_shared_texture_acquire(memory->d3d12,&detail),&detail);
    native_lock(device);
    bool success=admitted && failure.status==NATIVE_OK && native_shared_matches_texture(memory,texture) && native_device_result(device)==NATIVE_OK;
    if (success) {
        failure=queue_waits(device,fences,descriptor);
        if (failure.status==NATIVE_OK) failure=result(doe_dx_shared_texture_begin(memory->d3d12,descriptor->initialized!=0,descriptor->concurrentRead!=0,&detail),&detail);
        if (failure.status==NATIVE_OK && !descriptor->initialized) failure=native_d3d12_initialize_texture(device,memory->d3d12);
        success=failure.status==NATIVE_OK;
    }
    if (admitted) { memory->access=success ? SHARED_ACTIVE : SHARED_IDLE; memory->active_texture=success ? texture : NULL; }
    if (!success && admitted) (void)doe_dx_shared_texture_end(memory->d3d12,NULL);
    native_unlock(device);
    for (size_t i=0;i<retained;++i) wgpuSharedFenceRelease(fences[i]);
    free(fences); native_shared_error(device,failure,"DXGI BeginAccess failed");
    doe_resource_texture_release(texture); wgpuSharedTextureMemoryRelease(memory);
    return success ? WGPUStatus_Success : WGPUStatus_Error;
#else
    (void)memory; (void)texture; (void)descriptor; return WGPUStatus_Error;
#endif
}
WGPUStatus native_d3d12_shared_end(WGPUSharedTextureMemory memory,doe_resource_texture *texture,WGPUSharedTextureMemoryEndAccessState *state)
{
#if defined(_WIN32)
    if (!state || state->nextInChain) return WGPUStatus_Error;
    wgpuSharedTextureMemoryAddRef(memory); doe_resource_texture_retain(texture);
    native_device *device=memory->device;
    WGPUSharedFence fence=native_shared_allocate_fence(device);
    native_lock(device);
    bool admitted=fence && memory->access==SHARED_ACTIVE && memory->active_texture==texture &&
        native_shared_matches_texture(memory,texture);
    /* Reserve the access before wait_owner can unlock. Only this call may
     * finish or roll it back; concurrent End/Begin never borrow its admission. */
    if (admitted) memory->access=SHARED_ENDING;
    native_failure failure=native_failure_status(native_device_result(device));
    if (admitted && failure.status==NATIVE_OK && doe_dx_shared_texture_keyed(memory->d3d12))
        failure=settle_texture(device,memory->d3d12);
    bool reserved=admitted && memory->access==SHARED_ENDING && memory->active_texture==texture;
    if (reserved && failure.status==NATIVE_OK) failure=native_failure_status(native_device_result(device));
    admitted=reserved && native_shared_matches_texture(memory,texture);
    doe_dx_error detail={0}; bool initialized=false;
    if (admitted && failure.status==NATIVE_OK) failure=result(doe_dx_shared_texture_release_point(memory->d3d12,
        &fence->d3d12,&fence->end_value,&initialized,&detail),&detail);
    if (admitted && failure.status==NATIVE_OK) failure=result(doe_dx_shared_texture_end(memory->d3d12,&detail),&detail);
    bool success=admitted && failure.status==NATIVE_OK;
    if (success) {
        fence->end_fence=fence;
        state->initialized=initialized ? WGPU_TRUE : WGPU_FALSE;
        state->fenceCount=1; state->fences=&fence->end_fence;
        state->signaledValueCount=1; state->signaledValues=&fence->end_value;
        memory->access=SHARED_IDLE; memory->active_texture=NULL;
    } else if (reserved) {
        /* Timeout, loss or destruction never releases a keyed mutex or unknown
         * command obligation. Retain this access for retry/finalization. */
        memory->access=SHARED_ACTIVE;
    }
    native_unlock(device);
    if (!success) wgpuSharedFenceRelease(fence);
    native_shared_error(device,failure,"DXGI EndAccess failed");
    doe_resource_texture_release(texture); wgpuSharedTextureMemoryRelease(memory);
    return success ? WGPUStatus_Success : WGPUStatus_Error;
#else
    (void)memory; (void)texture; (void)state; return WGPUStatus_Error;
#endif
}
