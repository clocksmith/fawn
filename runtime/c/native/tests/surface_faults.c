/* SPDX-License-Identifier: MIT
 * Surface robustness on a real XCB window (software Vulkan): configure,
 * getCurrentTexture, a rendered present, a window resize between acquire and
 * present, unconfigure and reconfigure, and the window destroyed between
 * acquire and present; then device destroy() between acquire and present.
 * - Fault sweep: every host allocation of the C owners (GNU --wrap) and every
 *   result-returning Vulkan call through the device context's function table
 *   fails in turn (creation out-of-memory, acquisition out-of-date, present
 *   surface-lost). Every case must settle without a crash or hang, statuses
 *   stay within the webgpu.h set, a frame-changing failure is reported (error
 *   or non-success status), and release reclaims everything (ASan).
 * - Concurrency: one thread presents and resizes while two threads run
 *   compute, copies and maps on the same device and another thread creates,
 *   presents and releases its own window, surface and device.
 * Needs a reachable X server (DISPLAY, plus XAUTHORITY when required); exits
 * 77 (skip, never a pass) without one. */
#include "../src/internal.h"
#include "../../backends/vulkan/src/internal.h" /* The device context's function table. */
#include "surface_consumer.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <xcb/xcb.h>

/* Host allocation boundary. */
static _Atomic bool counting;
static atomic_uint allocations, fail_at, failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
static bool inject(void)
{
    if (!atomic_load(&counting)) return false;
    const unsigned index = atomic_fetch_add(&allocations, 1) + 1;
    if (index != atomic_load(&fail_at)) return false;
    atomic_fetch_add(&failures, 1);
    errno = ENOMEM;
    return true;
}
void *__wrap_malloc(size_t size) { return inject() ? NULL : __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { return inject() ? NULL : __real_calloc(count, size); }

/* Native call boundary. */
static doe_vk_functions real;
static _Atomic bool native_counting;
static atomic_uint native_calls, native_fail_at, native_failures;
static bool native_inject(const char *name)
{
    if (!atomic_load(&native_counting)) return false;
    const unsigned index = atomic_fetch_add(&native_calls, 1) + 1;
    if (index != atomic_load(&native_fail_at)) return false;
    atomic_fetch_add(&native_failures, 1);
    if (getenv("SURFACE_FAULTS_TRACE")) fprintf(stderr, "  injected %s\n", name);
    return true;
}
enum { OUT_OF_DEVICE_MEMORY = -2, OUT_OF_HOST_MEMORY = -1, DEVICE_LOST = -4,
       SURFACE_LOST = -1000000000, OUT_OF_DATE = -1000001004 };
#define FAULT(name, code, params, args) \
    static VkResult fault_##name params { return native_inject(#name) ? code : real.name args; }
/* Injected loss first lets the real driver finish what it accepted: unlike a
 * lost device, a healthy one still owns submitted work and enqueued presents,
 * which loss permits the caller to destroy at once. Presents are tracked by
 * their swapchain-maintenance1 fences, under a lock: the progress worker
 * destroys fences too. */
static doe_vk_context *faulted;
static pthread_mutex_t presented_lock = PTHREAD_MUTEX_INITIALIZER;
static VkFence presented_fences[64];
static unsigned presented_count;
static void settle_real_device(void)
{
    (void)real.vkQueueWaitIdle(faulted->queue);
    pthread_mutex_lock(&presented_lock);
    for (unsigned i = 0; i < presented_count; ++i) /* Bounded; X may stop completing. */
        (void)real.vkWaitForFences(faulted->device, 1, &presented_fences[i], 1, UINT64_C(2000000000));
    presented_count = 0;
    pthread_mutex_unlock(&presented_lock);
}
#define LOSS_FAULT(name, params, args) \
    static VkResult fault_##name params { \
        if (!native_inject(#name)) return real.name args; \
        settle_real_device(); \
        return DEVICE_LOST; \
    }
FAULT(vkCreateSwapchainKHR, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkSwapchainCreateInfoKHR *i, const VkAllocationCallbacks *a, VkSwapchainKHR *o), (d, i, a, o))
FAULT(vkGetSwapchainImagesKHR, OUT_OF_HOST_MEMORY, (VkDevice d, VkSwapchainKHR s, uint32_t *n, VkImage *o), (d, s, n, o))
FAULT(vkAcquireNextImageKHR, OUT_OF_DATE, (VkDevice d, VkSwapchainKHR s, uint64_t t, VkSemaphore m, VkFence f, uint32_t *o), (d, s, t, m, f, o))
static VkResult fault_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info)
{
    /* SURFACE_LOST still enqueues the present (Vulkan), so the real one runs. */
    const bool lost = native_inject("vkQueuePresentKHR");
    const VkResult result = real.vkQueuePresentKHR(queue, info);
    const VkSwapchainPresentFenceInfoEXT *fences = info->pNext;
    pthread_mutex_lock(&presented_lock);
    if (fences && fences->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT && result != -1 && result != -2 &&
            presented_count < 64)
        presented_fences[presented_count++] = fences->pFences[0];
    pthread_mutex_unlock(&presented_lock);
    return lost && result != -1 && result != -2 ? SURFACE_LOST : result;
}
static void fault_vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator)
{
    pthread_mutex_lock(&presented_lock);
    for (unsigned i = 0; i < presented_count; ++i)
        if (presented_fences[i] == fence) presented_fences[i--] = presented_fences[--presented_count];
    real.vkDestroyFence(device, fence, allocator);
    pthread_mutex_unlock(&presented_lock);
}
FAULT(vkCreateSemaphore, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkSemaphoreCreateInfo *i, const VkAllocationCallbacks *a, VkSemaphore *o), (d, i, a, o))
FAULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, SURFACE_LOST, (VkPhysicalDevice p, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *o), (p, s, o))
FAULT(vkCreateImageView, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *o), (d, i, a, o))
FAULT(vkCreateFence, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *o), (d, i, a, o))
FAULT(vkCreateCommandPool, OUT_OF_DEVICE_MEMORY, (VkDevice d, const VkCommandPoolCreateInfo *i, const VkAllocationCallbacks *a, VkCommandPool *o), (d, i, a, o))
FAULT(vkBeginCommandBuffer, OUT_OF_DEVICE_MEMORY, (VkCommandBuffer c, const VkCommandBufferBeginInfo *i), (c, i))
FAULT(vkEndCommandBuffer, OUT_OF_DEVICE_MEMORY, (VkCommandBuffer c), (c))
FAULT(vkQueueSubmit, OUT_OF_HOST_MEMORY, (VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f), (q, n, s, f))
LOSS_FAULT(vkWaitForFences, (VkDevice d, uint32_t n, const VkFence *f, VkBool32 w, uint64_t t), (d, n, f, w, t))
LOSS_FAULT(vkGetFenceStatus, (VkDevice d, VkFence f), (d, f))
FAULT(vkResetFences, OUT_OF_DEVICE_MEMORY, (VkDevice d, uint32_t n, const VkFence *f), (d, n, f))

static void install(doe_vk_context *context)
{
    real = context->vk;
    faulted = context;
    pthread_mutex_lock(&presented_lock);
    presented_count = 0;
    pthread_mutex_unlock(&presented_lock);
#define INSTALL(name) if (context->vk.name) context->vk.name = fault_##name;
    INSTALL(vkCreateSwapchainKHR) INSTALL(vkGetSwapchainImagesKHR) INSTALL(vkAcquireNextImageKHR)
    INSTALL(vkQueuePresentKHR) INSTALL(vkCreateSemaphore) INSTALL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    INSTALL(vkCreateImageView) INSTALL(vkCreateFence) INSTALL(vkCreateCommandPool) INSTALL(vkBeginCommandBuffer)
    INSTALL(vkEndCommandBuffer) INSTALL(vkQueueSubmit) INSTALL(vkWaitForFences) INSTALL(vkGetFenceStatus)
    INSTALL(vkResetFences) INSTALL(vkDestroyFence)
#undef INSTALL
}

typedef struct window {
    xcb_connection_t *connection;
    xcb_window_t id;
    bool destroyed;
} window;

static void pause_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}
static void settle(window *w)
{
    for (int i = 0; i < 3; ++i) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(w->connection))) free(event);
        pause_ms(5);
    }
}
static bool open_window(window *w, uint32_t width, uint32_t height)
{
    memset(w, 0, sizeof(*w));
    const char *display = getenv("DISPLAY");
    if (!display || !*display) return false;
    int screen_index = 0;
    w->connection = xcb_connect(NULL, &screen_index);
    if (xcb_connection_has_error(w->connection)) {
        xcb_disconnect(w->connection);
        w->connection = NULL;
        return false;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(w->connection));
    for (int i = 0; i < screen_index; ++i) xcb_screen_next(&screens);
    xcb_screen_t *screen = screens.data;
    w->id = xcb_generate_id(w->connection);
    /* Override-redirect: no window manager resizes, constrains or delays the
     * geometry, so a resize is the surface's current extent once settled. */
    uint32_t values[3] = {screen->black_pixel, 1, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(w->connection, XCB_COPY_FROM_PARENT, w->id, screen->root, 0, 0, width, height, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values);
    xcb_map_window(w->connection, w->id);
    xcb_flush(w->connection);
    settle(w);
    return true;
}
static void resize(window *w, uint32_t width, uint32_t height)
{
    uint32_t values[2] = {width, height};
    xcb_configure_window(w->connection, w->id, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(w->connection);
    settle(w);
}
static void destroy_window(window *w)
{
    if (w->destroyed) return;
    xcb_destroy_window(w->connection, w->id);
    xcb_flush(w->connection);
    w->destroyed = true;
}
static void close_window(window *w)
{
    destroy_window(w);
    xcb_disconnect(w->connection);
}

typedef struct observed {
    atomic_uint errors, lost;
    unsigned presented, non_success;
    unsigned after_window_loss; /* Frames presented once the window is gone. */
} observed;
static void on_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)unused;
    atomic_fetch_add(&((observed *)userdata)->errors, 1);
    if (getenv("SURFACE_FAULTS_TRACE")) fprintf(stderr, "  error %d: %.*s\n", (int)type, (int)message.length, message.data);
}
static void on_lost(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)message; (void)unused;
    if (reason != WGPUDeviceLostReason_CallbackCancelled) atomic_fetch_add(&((observed *)userdata)->lost, 1);
}
static int adapter_status, device_status;
static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    adapter_status = (int)status;
    *(WGPUAdapter *)userdata = adapter;
}
static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    device_status = (int)status;
    *(WGPUDevice *)userdata = device;
}

typedef struct session {
    window window;
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat format;
    uint32_t width, height;
    observed seen;
} session;

static bool open_session(session *s)
{
    memset(s, 0, sizeof(*s));
    s->width = 48;
    s->height = 32;
    if (!open_window(&s->window, s->width, s->height)) return false;
    s->instance = wgpuCreateInstance(NULL);
    assert(s->instance);
    WGPUSurfaceSourceXCBWindow xcb = WGPU_SURFACE_SOURCE_XCB_WINDOW_INIT;
    xcb.connection = s->window.connection;
    xcb.window = s->window.id;
    WGPUSurfaceDescriptor surface_desc = {.nextInChain = &xcb.chain};
    s->surface = wgpuInstanceCreateSurface(s->instance, &surface_desc);
    assert(s->surface);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    options.compatibleSurface = s->surface;
    wgpuInstanceRequestAdapter(s->instance, &options, (WGPURequestAdapterCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_adapter, .userdata1 = &s->adapter});
    assert(s->adapter);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = on_error, .userdata1 = &s->seen};
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = on_lost, .userdata1 = &s->seen};
    wgpuAdapterRequestDevice(s->adapter, &descriptor, (WGPURequestDeviceCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous, .callback = on_device, .userdata1 = &s->device});
    assert(s->device);
    s->queue = wgpuDeviceGetQueue(s->device);
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    assert(wgpuSurfaceGetCapabilities(s->surface, s->adapter, &caps) == WGPUStatus_Success);
    for (size_t i = 0; i < caps.formatCount && !s->format; ++i)
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)
            s->format = caps.formats[i];
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    assert(s->format);
    return true;
}
static void close_session(session *s)
{
    if (s->surface) wgpuSurfaceRelease(s->surface);
    wgpuQueueRelease(s->queue);
    wgpuDeviceRelease(s->device);
    wgpuAdapterRelease(s->adapter);
    wgpuInstanceRelease(s->instance);
    close_window(&s->window);
}

static void configure_surface(session *s)
{
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = s->device;
    config.format = s->format;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = s->width;
    config.height = s->height;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(s->surface, &config);
}

static bool success_status(WGPUSurfaceGetCurrentTextureStatus status)
{
    return status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
        status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
}
static bool known_status(WGPUSurfaceGetCurrentTextureStatus status)
{
    return success_status(status) || status == WGPUSurfaceGetCurrentTextureStatus_Timeout ||
        status == WGPUSurfaceGetCurrentTextureStatus_Outdated || status == WGPUSurfaceGetCurrentTextureStatus_Lost ||
        status == WGPUSurfaceGetCurrentTextureStatus_Error;
}

/* Acquires (bounded Timeout retries); NULL texture unless a success status. */
static WGPUTexture acquire_frame(session *s, WGPUSurfaceGetCurrentTextureStatus *status)
{
    WGPUSurfaceTexture frame = WGPU_SURFACE_TEXTURE_INIT;
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        wgpuSurfaceGetCurrentTexture(s->surface, &frame);
        if (frame.status != WGPUSurfaceGetCurrentTextureStatus_Timeout) break;
    }
    assert(known_status(frame.status));
    assert(success_status(frame.status) == (frame.texture != NULL));
    *status = frame.status;
    if (!success_status(frame.status) && !s->window.destroyed) ++s->seen.non_success;
    return frame.texture;
}

/* Clears the acquired frame in one submission (or does nothing). */
static void draw(session *s, WGPUTexture texture)
{
    WGPUTextureView view = wgpuTextureCreateView(texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    if (view && encoder) {
        WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = view;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = (WGPUColor){0.0, 0.5, 1.0, 1.0};
        WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 1, .colorAttachments = &attachment};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        if (pass) {
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        if (commands) {
            wgpuQueueSubmit(s->queue, 1, &commands);
            wgpuCommandBufferRelease(commands);
        }
    }
    if (encoder) wgpuCommandEncoderRelease(encoder);
    if (view) wgpuTextureViewRelease(view);
}

/* One frame: acquire, draw, present. A resize callback runs between draw and
 * present when given. Outdated reconfigures once, as an application would. */
static void frame(session *s, void (*between)(session *))
{
    WGPUSurfaceGetCurrentTextureStatus status;
    WGPUTexture texture = acquire_frame(s, &status);
    if (getenv("SURFACE_FAULTS_TRACE")) fprintf(stderr, "  acquire status %d\n", (int)status);
    /* Outdated reconfigures while the window exists; Lost, or any status once the
     * window is gone, means the window and surface must be recreated. Configuring
     * for a destroyed X window is an application error: Mesa's X11 WSI reports
     * no queue support for it (its window query fails), which Doe reports. */
    if (status == WGPUSurfaceGetCurrentTextureStatus_Outdated && !s->window.destroyed) {
        configure_surface(s);
        texture = acquire_frame(s, &status);
    }
    if (!texture) {
        assert(wgpuSurfacePresent(s->surface) == WGPUStatus_Error); /* Nothing acquired. */
        return;
    }
    draw(s, texture);
    if (between) between(s);
    const WGPUStatus presented = wgpuSurfacePresent(s->surface);
    /* Whether presentation to a destroyed window still succeeds (and whether
     * later acquisition reports Lost) depends on the X server and WSI, so
     * frames from window loss on are counted apart, never compared. */
    if (s->window.destroyed) s->seen.after_window_loss += presented == WGPUStatus_Success;
    else if (presented == WGPUStatus_Success) ++s->seen.presented;
    else ++s->seen.non_success;
    wgpuTextureRelease(texture);
}

static void resize_mid_frame(session *s)
{
    s->width = s->width == 48 ? 40 : 48;
    resize(&s->window, s->width, s->height);
}
static void lose_window_mid_frame(session *s) { destroy_window(&s->window); }
static void destroy_device_mid_frame(session *s) { wgpuDeviceDestroy(s->device); }

/* The surface workload; the fault-free probe fixes its expected outcome. */
static unsigned expected_presented, expected_non_success;
static void workload(session *s)
{
    configure_surface(s);
    frame(s, NULL);
    frame(s, NULL);
    frame(s, resize_mid_frame); /* Resized between acquire and present. */
    frame(s, NULL);             /* Suboptimal or outdated: reconfigured at the new size. */
    wgpuSurfaceUnconfigure(s->surface);
    WGPUSurfaceGetCurrentTextureStatus status;
    assert(!acquire_frame(s, &status) && status == WGPUSurfaceGetCurrentTextureStatus_Error);
    --s->seen.non_success; /* Expected: unconfigured. */
    configure_surface(s);
    frame(s, NULL);
    frame(s, lose_window_mid_frame); /* The window goes away between acquire and present. */
    frame(s, NULL);                  /* Lost or error; nothing presented. */
    wgpuSurfaceUnconfigure(s->surface);
}

/* Device destroy() between acquire and present: nothing hangs or crashes,
 * later acquisition fails, unconfigure and release still work. */
static void device_loss(session *s)
{
    configure_surface(s);
    frame(s, NULL);
    frame(s, destroy_device_mid_frame);
    WGPUSurfaceGetCurrentTextureStatus status;
    WGPUTexture texture = acquire_frame(s, &status);
    assert(!texture);
    assert(wgpuSurfacePresent(s->surface) == WGPUStatus_Error);
    wgpuSurfaceUnconfigure(s->surface);
    assert(atomic_load(&s->seen.lost) == 1);
}

static void check(const session *s, bool injected, const char *kind, unsigned index)
{
    const bool changed = s->seen.presented != expected_presented || s->seen.non_success != expected_non_success;
    const bool reported = atomic_load(&s->seen.errors) || atomic_load(&s->seen.lost) || s->seen.non_success;
    if (changed && !(injected && reported)) {
        fprintf(stderr, "%s %u: presented %u, non-success %u, injected %u, errors %u, lost %u\n", kind, index,
            s->seen.presented, s->seen.non_success, injected, atomic_load(&s->seen.errors), atomic_load(&s->seen.lost));
        abort();
    }
}

/* Concurrency: a presenting thread resizes and presents while two threads run
 * compute/copy/map rounds on the same device, and another thread runs its own
 * window, surface and device. */
static const char *const compute_code =
    "@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) { d[id.x] = d[id.x] * 2u + 3u; }\n";
typedef struct compute_worker { session *s; WGPUComputePipeline pipeline; unsigned seed, rounds; } compute_worker;
static void mapped_reply(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    *(WGPUMapAsyncStatus *)userdata = status;
}
static void *run_compute(void *userdata)
{
    compute_worker *w = userdata;
    session *s = w->s;
    for (unsigned round = 0; round < 60; ++round) {
        uint32_t input[64];
        for (unsigned i = 0; i < 64; ++i) input[i] = w->seed * 97u + round + i;
        WGPUBufferDescriptor storage_desc = {.size = 256, .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
        WGPUBufferDescriptor readback_desc = {.size = 256, .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst};
        WGPUBuffer storage = wgpuDeviceCreateBuffer(s->device, &storage_desc);
        WGPUBuffer readback = wgpuDeviceCreateBuffer(s->device, &readback_desc);
        wgpuQueueWriteBuffer(s->queue, storage, 0, input, sizeof(input));
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(w->pipeline, 0);
        WGPUBindGroupEntry entry = {.binding = 0, .buffer = storage, .size = 256};
        WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &group_desc);
        wgpuBindGroupLayoutRelease(layout);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        wgpuComputePassEncoderSetPipeline(pass, w->pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, readback, 0, 256);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(s->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuBindGroupRelease(group);
        wgpuBufferRelease(storage);
        WGPUMapAsyncStatus status = (WGPUMapAsyncStatus)0;
        WGPUBufferMapCallbackInfo map = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = mapped_reply, .userdata1 = &status};
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, 256, map);
        wgpuInstanceProcessEvents(s->instance);
        assert(status == WGPUMapAsyncStatus_Success);
        const uint32_t *output = wgpuBufferGetConstMappedRange(readback, 0, 256);
        for (unsigned i = 0; i < 64; ++i) assert(output[i] == input[i] * 2u + 3u);
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        ++w->rounds;
    }
    return NULL;
}
typedef struct presenter { session *s; unsigned frames; } presenter;
static void *run_presenter(void *userdata)
{
    presenter *p = userdata;
    configure_surface(p->s);
    for (unsigned i = 0; i < 40; ++i) frame(p->s, i % 7 == 3 ? resize_mid_frame : NULL);
    wgpuSurfaceUnconfigure(p->s->surface);
    p->frames = p->s->seen.presented;
    return NULL;
}
static void *run_own_session(void *userdata)
{
    unsigned *presented = userdata;
    for (unsigned i = 0; i < 4; ++i) {
        session own;
        assert(open_session(&own));
        configure_surface(&own);
        for (unsigned f = 0; f < 5; ++f) frame(&own, NULL);
        wgpuSurfaceUnconfigure(own.surface);
        *presented += own.seen.presented;
        assert(!atomic_load(&own.seen.errors));
        close_session(&own);
    }
    return NULL;
}

static void concurrency(void)
{
    session s;
    assert(open_session(&s));
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){compute_code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain = &wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device, &shader_desc);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module = module;
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(s.device, &pipeline_desc);
    wgpuShaderModuleRelease(module);
    assert(pipeline);
    presenter present = {.s = &s};
    compute_worker workers[2] = {{.s = &s, .pipeline = pipeline, .seed = 1}, {.s = &s, .pipeline = pipeline, .seed = 2}};
    unsigned own_presented = 0;
    pthread_t threads[4];
    assert(!pthread_create(&threads[0], NULL, run_presenter, &present));
    assert(!pthread_create(&threads[1], NULL, run_compute, &workers[0]));
    assert(!pthread_create(&threads[2], NULL, run_compute, &workers[1]));
    assert(!pthread_create(&threads[3], NULL, run_own_session, &own_presented));
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    assert(workers[0].rounds == 60 && workers[1].rounds == 60 && present.frames > 30 && own_presented > 15);
    assert(!atomic_load(&s.seen.errors));
    wgpuComputePipelineRelease(pipeline);
    close_session(&s);
    printf("surface concurrency: %u frames presented with resizes beside two compute threads; %u frames on 4 other sessions\n",
        present.frames, own_presented);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    session s;
    if (!open_session(&s)) {
        fprintf(stderr, "SKIP surface faults: no reachable X display (DISPLAY unset or connection refused)\n");
        return 77;
    }
    /* Probe without faults: counts and the exact expected outcome. */
    install(((native_device *)s.device)->backend.value.vulkan.context);
    atomic_store(&counting, true);
    atomic_store(&native_counting, true);
    workload(&s);
    atomic_store(&counting, false);
    atomic_store(&native_counting, false);
    expected_presented = s.seen.presented;
    expected_non_success = s.seen.non_success;
    assert(!atomic_load(&s.seen.errors) && expected_presented >= 5);
    printf("surface workload: %u frames presented, %u non-success statuses, %u of 2 presented after window loss, without faults\n",
        expected_presented, expected_non_success, s.seen.after_window_loss);
    const unsigned total_allocations = atomic_load(&allocations), total_calls = atomic_load(&native_calls);
    close_session(&s);
    assert(open_session(&s));
    device_loss(&s);
    close_session(&s);

    unsigned changed_allocations = 0, changed_calls = 0;
    /* Debug aids: SURFACE_FAULTS_FROM=<allocation>,<native call>[,<cases>] starts each
     * sweep there (and runs at most that many cases of each); SURFACE_FAULTS_SWEEP_ONLY
     * skips the concurrency phase. */
    unsigned first_allocation = 1, first_call = 1, cases = UINT_MAX;
    if (getenv("SURFACE_FAULTS_FROM"))
        sscanf(getenv("SURFACE_FAULTS_FROM"), "%u,%u,%u", &first_allocation, &first_call, &cases);
    const unsigned last_allocation = first_allocation - 1 + (cases < total_allocations + 4 ? cases : total_allocations + 4);
    const unsigned last_call = first_call - 1 + (cases < total_calls + 4 ? cases : total_calls + 4);
    for (unsigned index = first_allocation; index <= total_allocations + 4 && index <= last_allocation; ++index) {
        if (getenv("SURFACE_FAULTS_TRACE")) fprintf(stderr, "allocation %u\n", index);
        assert(open_session(&s));
        atomic_store(&allocations, 0);
        atomic_store(&failures, 0);
        atomic_store(&fail_at, index);
        atomic_store(&counting, true);
        workload(&s);
        atomic_store(&counting, false);
        check(&s, atomic_load(&failures) != 0, "allocation", index);
        changed_allocations += s.seen.presented != expected_presented;
        close_session(&s);
    }
    for (unsigned index = first_call; index <= total_calls + 4 && index <= last_call; ++index) {
        if (getenv("SURFACE_FAULTS_TRACE")) fprintf(stderr, "native call %u\n", index);
        assert(open_session(&s));
        install(((native_device *)s.device)->backend.value.vulkan.context);
        atomic_store(&native_calls, 0);
        atomic_store(&native_failures, 0);
        atomic_store(&native_fail_at, index);
        atomic_store(&native_counting, true);
        workload(&s);
        atomic_store(&native_counting, false);
        check(&s, atomic_load(&native_failures) != 0, "native call", index);
        changed_calls += s.seen.presented != expected_presented;
        close_session(&s);
    }
    printf("surface faults: %u host allocations (%u changed presentation, each reported), %u native calls (%u changed it, each reported); "
        "resize and window loss between acquire and present, device destroy() mid-frame\n",
        total_allocations, changed_allocations, total_calls, changed_calls);
    if (!getenv("SURFACE_FAULTS_SWEEP_ONLY")) concurrency();
    return 0;
}
