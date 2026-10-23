/* SPDX-License-Identifier: MIT
 * Instance-level public surfaces, capabilities and configuration. Native
 * doe_vk_surface objects are created on the configured device's own context,
 * which enables WSI only when its adapter was requested with compatibleSurface. */
#include "surface.h"
#include <stdlib.h>

enum { SURFACE_ALPHA_OPAQUE = 1, SURFACE_ALPHA_PREMULTIPLIED = 2,
    SURFACE_ALPHA_POSTMULTIPLIED = 4, SURFACE_ALPHA_INHERIT = 8 };
/* Advertised public usages are the exercised subset; others reject explicitly. */
static const WGPUTextureUsage surface_usages = WGPUTextureUsage_RenderAttachment |
    WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;

static bool source_from_chain(const WGPUChainedStruct *chain, doe_vk_surface_desc *out)
{
#if defined(__linux__)
    if (!chain || chain->next) return false;
    switch (chain->sType) {
    case WGPUSType_SurfaceSourceWaylandSurface: {
        const WGPUSurfaceSourceWaylandSurface *source = (const WGPUSurfaceSourceWaylandSurface *)chain;
        *out = (doe_vk_surface_desc){.platform=DOE_VK_SURFACE_WAYLAND,.display=source->display,
            .window.wayland_surface=source->surface};
        return source->display && source->surface;
    }
    case WGPUSType_SurfaceSourceXCBWindow: {
        const WGPUSurfaceSourceXCBWindow *source = (const WGPUSurfaceSourceXCBWindow *)chain;
        *out = (doe_vk_surface_desc){.platform=DOE_VK_SURFACE_XCB,.display=source->connection,
            .window.xcb_window=source->window};
        return source->connection && source->window;
    }
    case WGPUSType_SurfaceSourceXlibWindow: {
        const WGPUSurfaceSourceXlibWindow *source = (const WGPUSurfaceSourceXlibWindow *)chain;
        *out = (doe_vk_surface_desc){.platform=DOE_VK_SURFACE_XLIB,.display=source->display,
            .window.xlib_window=source->window};
        return source->display && source->window;
    }
    default: return false; /* Metal layer, HWND, Android and unknown chains. */
    }
#else
    (void)chain;
    (void)out;
    return false; /* Only the Linux Vulkan surface owner is composed. */
#endif
}

WGPUSurface wgpuInstanceCreateSurface(WGPUInstance handle, const WGPUSurfaceDescriptor *descriptor)
{
    doe_instance *instance = (doe_instance *)handle;
    doe_vk_surface_desc source;
    size_t label_size = 0;
    if (!instance || !descriptor || !source_from_chain(descriptor->nextInChain, &source) ||
        !native_string_size(descriptor->label, &label_size)) return NULL;
    native_surface *surface = calloc(1, sizeof(*surface));
    if (!surface) return NULL;
    doe_label_config labels = {.abi_version=DOE_LABELS_ABI_VERSION,
        .allocator={NULL,NULL,native_allocate,native_deallocate}};
    if (pthread_mutex_init(&surface->mutex, NULL)) { free(surface); return NULL; }
    if (doe_label_store_create(&labels, &surface->labels) != DOE_LABEL_OK ||
        doe_label_store_set(surface->labels, surface, (const unsigned char *)descriptor->label.data,
            label_size) != DOE_LABEL_OK) {
        if (surface->labels && doe_label_store_destroy(surface->labels) != DOE_LABEL_OK) abort();
        if (pthread_mutex_destroy(&surface->mutex)) abort();
        free(surface);
        return NULL;
    }
    atomic_init(&surface->references, 1);
    atomic_init(&surface->leases, 1);
    surface->source = source;
    surface->presented = NATIVE_OK;
    surface->instance = instance;
    doe_instance_retain(instance);
    return (WGPUSurface)surface;
}

uint32_t native_surface_platform(WGPUSurface handle)
{
    return handle ? ((native_surface *)handle)->source.platform : 0;
}

native_result native_surface_compatible(WGPUSurface handle, const native_backend *adapter)
{
    if (!handle || adapter->kind != WGPUBackendType_Vulkan) return NATIVE_UNSUPPORTED;
    doe_vk_surface *probe = NULL;
    doe_vk_status status = doe_vk_surface_create(adapter->value.vulkan.context,
        &((native_surface *)handle)->source, &probe, NULL);
    if (status == DOE_VK_OK && doe_vk_surface_release(probe) != DOE_VK_OK) abort(); /* Never configured. */
    return native_from_vulkan(status);
}

void native_surface_drop_current(native_surface *surface)
{
    doe_resource_texture *current = surface->current;
    surface->current = NULL;
    if (!current) return;
    doe_resource_texture_destroy(current);
    doe_resource_texture_release(current);
}

void native_surface_unconfigure_locked(native_surface *surface)
{
    surface->configured = false;
    native_device *device = surface->device;
    if (!surface->vulkan) return;
    bool released = false;
    native_lock(device);
    native_surface_drop_current(surface);
    /* Retires only the existing frame (present fence or maintenance1 discard);
     * failure keeps its obligations. Then release present commands it unblocked. */
    (void)doe_vk_surface_retire_frame(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
    (void)native_settle_pending(device);
    /* BUSY keeps the swapchain while caller frame texture/view leases remain. */
    if (doe_vk_surface_release(surface->vulkan) == DOE_VK_OK) {
        surface->vulkan = NULL;
        surface->device = NULL;
        released = true;
    }
    native_unlock(device);
    if (released) native_device_release(device);
}

static void surface_teardown(native_surface *surface)
{
    if (surface->vulkan) {
        native_device *device = surface->device;
        native_lock(device);
        /* No native_settle_pending here: teardown may run from a resource
         * finalizer inside settlement. Unresolved work keeps the native surface. */
        (void)doe_vk_surface_retire_frame(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
        doe_vk_status status = doe_vk_surface_release(surface->vulkan);
        native_unlock(device);
        /* Unknown presentation retains surface, device and borrowed platform
         * obligations rather than freeing state the native queue may use. */
        if (status != DOE_VK_OK) return;
        /* Lease guarantee: public teardown runs with no owner lock held. When a
         * frame payload finalizer runs this inside settlement under the device
         * mutex, the finalizing resource still holds its own device lease
         * (released after its native payload), so this drop cannot finalize the
         * device or join its progress worker. */
        native_device_release(device);
    }
    if (doe_label_store_destroy(surface->labels) != DOE_LABEL_OK) abort();
    if (pthread_mutex_destroy(&surface->mutex)) abort();
    doe_instance *instance = surface->instance;
    free(surface);
    doe_instance_release(instance);
}

void native_surface_release_lease(native_surface *surface)
{
    if (native_release_count(&surface->leases)) surface_teardown(surface);
}

void wgpuSurfaceAddRef(WGPUSurface handle)
{
    if (handle) native_retain_count(&((native_surface *)handle)->references);
}

void wgpuSurfaceRelease(WGPUSurface handle)
{
    native_surface *surface = (native_surface *)handle;
    if (!surface || !native_release_count(&surface->references)) return;
    if (pthread_mutex_lock(&surface->mutex)) abort();
    native_surface_unconfigure_locked(surface);
    if (pthread_mutex_unlock(&surface->mutex)) abort();
    native_surface_release_lease(surface);
}

void wgpuSurfaceSetLabel(WGPUSurface handle, WGPUStringView label)
{
    native_surface *surface = (native_surface *)handle;
    size_t size;
    if (!surface || !native_string_size(label, &size)) return;
    /* The store serializes itself; failure preserves the previous label. */
    (void)doe_label_store_set(surface->labels, surface, (const unsigned char *)label.data, size);
}

static bool public_format(uint32_t native, WGPUTextureFormat *out)
{
    switch (native) {
    case DOE_VK_FORMAT_BGRA8_UNORM: *out = WGPUTextureFormat_BGRA8Unorm; return true;
    case DOE_VK_FORMAT_BGRA8_SRGB: *out = WGPUTextureFormat_BGRA8UnormSrgb; return true;
    case DOE_VK_FORMAT_RGBA8_UNORM: *out = WGPUTextureFormat_RGBA8Unorm; return true;
    case DOE_VK_FORMAT_RGBA8_SRGB: *out = WGPUTextureFormat_RGBA8UnormSrgb; return true;
    default: return false;
    }
}

static bool public_present_mode(uint32_t native, WGPUPresentMode *out)
{
    switch (native) {
    case DOE_VK_PRESENT_FIFO: *out = WGPUPresentMode_Fifo; return true;
    case DOE_VK_PRESENT_FIFO_RELAXED: *out = WGPUPresentMode_FifoRelaxed; return true;
    case DOE_VK_PRESENT_IMMEDIATE: *out = WGPUPresentMode_Immediate; return true;
    case DOE_VK_PRESENT_MAILBOX: *out = WGPUPresentMode_Mailbox; return true;
    default: return false;
    }
}

static WGPUStatus fill_capabilities(const doe_vk_surface_caps *caps, WGPUSurfaceCapabilities *out)
{
    const uint32_t all_formats[4] = {DOE_VK_FORMAT_BGRA8_UNORM, DOE_VK_FORMAT_BGRA8_SRGB,
        DOE_VK_FORMAT_RGBA8_UNORM, DOE_VK_FORMAT_RGBA8_SRGB};
    WGPUTextureFormat *formats = calloc(4, sizeof(*formats)); /* Deduplicated public subset. */
    WGPUPresentMode *modes = calloc(DOE_VK_MAX_PRESENT_MODES, sizeof(*modes));
    WGPUCompositeAlphaMode *alphas = calloc(4, sizeof(*alphas));
    size_t format_count = 0, mode_count = 0, alpha_count = 0;
    bool fifo = false;
    for (uint32_t i = 0; formats && i < caps->format_count; ++i) {
        if (caps->formats[i].color_space != 0) continue; /* SRGB_NONLINEAR only. */
        size_t candidates = caps->formats[i].format == 0 ? 4 : 1; /* Native UNDEFINED: any. */
        for (size_t j = 0; j < candidates; ++j) {
            WGPUTextureFormat format;
            if (!public_format(candidates == 4 ? all_formats[j] : caps->formats[i].format, &format)) continue;
            bool duplicate = false;
            for (size_t k = 0; k < format_count; ++k) duplicate |= formats[k] == format;
            if (!duplicate) formats[format_count++] = format;
        }
    }
    for (uint32_t i = 0; modes && i < caps->present_mode_count; ++i) {
        WGPUPresentMode mode;
        if (!public_present_mode(caps->present_modes[i], &mode)) continue;
        fifo |= mode == WGPUPresentMode_Fifo;
        modes[mode_count++] = mode;
    }
    /* Inherit first: Auto aliases the first element and native Auto prefers Inherit. */
    if (alphas && (caps->supported_alpha & SURFACE_ALPHA_INHERIT)) alphas[alpha_count++] = WGPUCompositeAlphaMode_Inherit;
    if (alphas && (caps->supported_alpha & SURFACE_ALPHA_OPAQUE)) alphas[alpha_count++] = WGPUCompositeAlphaMode_Opaque;
    if (alphas && (caps->supported_alpha & SURFACE_ALPHA_PREMULTIPLIED)) alphas[alpha_count++] = WGPUCompositeAlphaMode_Premultiplied;
    if (alphas && (caps->supported_alpha & SURFACE_ALPHA_POSTMULTIPLIED)) alphas[alpha_count++] = WGPUCompositeAlphaMode_Unpremultiplied;
    if (!formats || !modes || !alphas || !caps->present_supported || !format_count || !fifo || !alpha_count ||
        !(caps->supported_usage & DOE_VK_TEXTURE_COLOR_ATTACHMENT)) {
        free(formats);
        free(modes);
        free(alphas);
        return WGPUStatus_Error;
    }
    WGPUTextureUsage usages = WGPUTextureUsage_RenderAttachment;
    if (caps->supported_usage & DOE_VK_TEXTURE_COPY_SRC) usages |= WGPUTextureUsage_CopySrc;
    if (caps->supported_usage & DOE_VK_TEXTURE_COPY_DST) usages |= WGPUTextureUsage_CopyDst;
    *out = (WGPUSurfaceCapabilities){.usages=usages & surface_usages,
        .formatCount=format_count,.formats=formats,.presentModeCount=mode_count,.presentModes=modes,
        .alphaModeCount=alpha_count,.alphaModes=alphas};
    return WGPUStatus_Success;
}

/* Error, never empty success, when this adapter was not opened with WSI for
 * the surface's platform (no compatibleSurface) or cannot present to it. */
WGPUStatus wgpuSurfaceGetCapabilities(WGPUSurface handle, WGPUAdapter adapter_handle,
    WGPUSurfaceCapabilities *out)
{
    native_surface *surface = (native_surface *)handle;
    native_adapter *adapter = (native_adapter *)adapter_handle;
    if (!surface || !adapter || !out || out->nextInChain || adapter->backend.kind != WGPUBackendType_Vulkan ||
        !(adapter->backend.value.vulkan.caps.surface_platforms & surface->source.platform))
        return WGPUStatus_Error;
    doe_vk_surface_caps caps;
    doe_vk_surface *probe = NULL;
    if (pthread_mutex_lock(&adapter->mutex)) abort();
    doe_vk_status status = doe_vk_surface_create(adapter->backend.value.vulkan.context, &surface->source, &probe, NULL);
    if (status == DOE_VK_OK) {
        status = doe_vk_surface_capabilities(probe, &caps, NULL);
        if (doe_vk_surface_release(probe) != DOE_VK_OK) abort(); /* Never configured. */
    }
    if (pthread_mutex_unlock(&adapter->mutex)) abort();
    return status == DOE_VK_OK ? fill_capabilities(&caps, out) : WGPUStatus_Error;
}

void wgpuSurfaceCapabilitiesFreeMembers(WGPUSurfaceCapabilities capabilities)
{
    free((void *)capabilities.formats);
    free((void *)capabilities.presentModes);
    free((void *)capabilities.alphaModes);
}

static const char *convert_configuration(const WGPUSurfaceConfiguration *config,
    const doe_vk_surface_caps *caps, doe_vk_surface_config *out)
{
    doe_vk_surface_config native = {.width=config->width,.height=config->height,
        .usage=DOE_VK_TEXTURE_COLOR_ATTACHMENT}; /* Clear-initialization is internal usage. */
    switch (config->format) {
    case WGPUTextureFormat_BGRA8Unorm: native.format = DOE_VK_FORMAT_BGRA8_UNORM; break;
    case WGPUTextureFormat_BGRA8UnormSrgb: native.format = DOE_VK_FORMAT_BGRA8_SRGB; break;
    case WGPUTextureFormat_RGBA8Unorm: native.format = DOE_VK_FORMAT_RGBA8_UNORM; break;
    case WGPUTextureFormat_RGBA8UnormSrgb: native.format = DOE_VK_FORMAT_RGBA8_SRGB; break;
    default: return "surface format unsupported";
    }
    if (!config->usage || (config->usage & ~surface_usages)) return "surface usage unsupported";
    if (config->usage & WGPUTextureUsage_CopySrc) native.usage |= DOE_VK_TEXTURE_COPY_SRC;
    if (config->usage & WGPUTextureUsage_CopyDst) native.usage |= DOE_VK_TEXTURE_COPY_DST;
    if (config->viewFormatCount && !config->viewFormats) return "surface view formats missing";
    for (size_t i = 0; i < config->viewFormatCount; ++i)
        if (config->viewFormats[i] != config->format) return "surface format reinterpretation unsupported";
    switch (config->presentMode) {
    case WGPUPresentMode_Undefined:
    case WGPUPresentMode_Fifo: native.present_mode = DOE_VK_PRESENT_FIFO; break;
    case WGPUPresentMode_FifoRelaxed: native.present_mode = DOE_VK_PRESENT_FIFO_RELAXED; break;
    case WGPUPresentMode_Immediate: native.present_mode = DOE_VK_PRESENT_IMMEDIATE; break;
    case WGPUPresentMode_Mailbox: native.present_mode = DOE_VK_PRESENT_MAILBOX; break;
    default: return "surface present mode unknown";
    }
    switch (config->alphaMode) {
    case WGPUCompositeAlphaMode_Auto:
        native.alpha_mode = (caps->supported_alpha & SURFACE_ALPHA_INHERIT) ? SURFACE_ALPHA_INHERIT : SURFACE_ALPHA_OPAQUE;
        break;
    case WGPUCompositeAlphaMode_Opaque: native.alpha_mode = SURFACE_ALPHA_OPAQUE; break;
    case WGPUCompositeAlphaMode_Premultiplied: native.alpha_mode = SURFACE_ALPHA_PREMULTIPLIED; break;
    case WGPUCompositeAlphaMode_Unpremultiplied: native.alpha_mode = SURFACE_ALPHA_POSTMULTIPLIED; break;
    case WGPUCompositeAlphaMode_Inherit: native.alpha_mode = SURFACE_ALPHA_INHERIT; break;
    default: return "surface alpha mode unknown";
    }
    *out = native;
    return NULL;
}

void wgpuSurfaceConfigure(WGPUSurface handle, const WGPUSurfaceConfiguration *config)
{
    native_surface *surface = (native_surface *)handle;
    if (!surface || !config || !config->device) return; /* No device to report to. */
    native_device *device = (native_device *)config->device;
    native_device_retain(device);
    WGPUErrorType type = WGPUErrorType_Validation;
    const char *message = NULL;
    if (pthread_mutex_lock(&surface->mutex)) abort();
    native_surface_unconfigure_locked(surface);
    if (config->nextInChain) message = "surface configuration extension unsupported";
    else if (device->backend.kind != WGPUBackendType_Vulkan) message = "surfaces are composed for Vulkan only";
    else if (!(device->backend.value.vulkan.caps.surface_platforms & surface->source.platform))
        message = "device adapter was not requested with a compatibleSurface of this platform";
    else if (surface->vulkan) message = "previous configuration's frame texture or views are still referenced";
    native_lock(device);
    doe_vk_status status = message ? DOE_VK_INVALID_ARGUMENT : native_device_status(device);
    if (status == DOE_VK_OK) status = native_to_vulkan(native_settle_pending(device).status);
    doe_vk_surface *native = NULL;
    if (status == DOE_VK_OK)
        status = doe_vk_surface_create(device->backend.value.vulkan.context, &surface->source, &native, NULL);
    doe_vk_surface_caps caps;
    if (status == DOE_VK_OK) status = doe_vk_surface_capabilities(native, &caps, NULL);
    doe_vk_surface_config converted;
    if (status == DOE_VK_OK && (message = convert_configuration(config, &caps, &converted)))
        status = DOE_VK_INVALID_ARGUMENT;
    if (status == DOE_VK_OK) status = doe_vk_surface_configure(native, &converted, NULL);
    if (status == DOE_VK_OK) {
        surface->vulkan = native;
        surface->device = device; /* Adopts the reference taken above. */
        surface->configured = true;
        surface->format = config->format;
        surface->usage = config->usage;
        surface->width = config->width;
        surface->height = config->height;
        surface->presented = NATIVE_OK;
    } else if (native && doe_vk_surface_release(native) != DOE_VK_OK) abort(); /* Unconfigured. */
    native_unlock(device);
    if (pthread_mutex_unlock(&surface->mutex)) abort();
    if (status == DOE_VK_OK) return;
    if (!message) {
        type = native_backend_error(status);
        message = status == DOE_VK_UNSUPPORTED ? "surface configuration unsupported by the native surface" :
            "surface configuration failed";
    }
    native_error(device, type, message);
    native_device_release(device);
}

void wgpuSurfaceUnconfigure(WGPUSurface handle)
{
    native_surface *surface = (native_surface *)handle;
    if (!surface) return;
    if (pthread_mutex_lock(&surface->mutex)) abort();
    native_surface_unconfigure_locked(surface);
    if (pthread_mutex_unlock(&surface->mutex)) abort();
}
