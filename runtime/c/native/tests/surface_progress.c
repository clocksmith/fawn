/* SPDX-License-Identifier: MIT
 * Native-boundary liveness controls over real CPU-Vulkan WSI. Only fence-wait
 * visibility is obscured; actual acquire/present/discard and cleanup still run.
 * Borrowed XCB handles remain live until the last frontend/context is retired. */
#include "../src/surface.h"
#include "../../backends/vulkan/src/surface_internal.h"
#include "surface_consumer.h"
#include <errno.h>
#include <time.h>
#include <xcb/xcb.h>

static doe_vk_functions actual;
static _Atomic(VkFence) acquire_fence, present_fence;
static atomic_bool hold_acquire, hold_present, hold_discard;
static atomic_uint acquires, discards, commands_closed, swapchains_closed, surfaces_closed, contexts_closed, frontends_closed;
static _Atomic(doe_vk_commands *) tracked_commands;
static _Atomic(doe_vk_context *) tracked_context;
static _Atomic(void *) tracked_frontend;

static void pause_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) && errno == EINTR) {}
}

static VkResult gated_wait(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout)
{
    for (uint32_t i = 0; i < count; ++i) {
        if ((fences[i] == atomic_load(&acquire_fence) && atomic_load(&hold_acquire)) ||
            (fences[i] == atomic_load(&present_fence) && atomic_load(&hold_present))) return 2; /* VK_TIMEOUT. */
    }
    return actual.vkWaitForFences(device, count, fences, all, timeout);
}

static VkResult observe_acquire(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
    atomic_fetch_add(&acquires, 1);
    atomic_store(&acquire_fence, fence);
    return actual.vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, index);
}

static VkResult observe_present(VkQueue queue, const VkPresentInfoKHR *info)
{
    const VkSwapchainPresentFenceInfoEXT *fences = info->pNext;
    assert(fences && fences->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT && fences->swapchainCount == 1);
    atomic_store(&present_fence, fences->pFences[0]);
    native_surface *surface = atomic_load(&tracked_frontend);
    atomic_store(&tracked_commands, surface->vulkan->commands);
    return actual.vkQueuePresentKHR(queue, info);
}

static VkResult observe_discard(VkDevice device, const VkReleaseSwapchainImagesInfoEXT *info)
{
    if (atomic_load(&hold_discard)) return -1; /* Native allocation failure retains the acquired image. */
    atomic_fetch_add(&discards, 1);
    return actual.vkReleaseSwapchainImagesEXT(device, info);
}

static void observe_swapchain_close(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator)
{
    actual.vkDestroySwapchainKHR(device, swapchain, allocator);
    atomic_fetch_add(&swapchains_closed, 1);
}

static void observe_surface_close(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *allocator)
{
    actual.vkDestroySurfaceKHR(instance, surface, allocator);
    atomic_fetch_add(&surfaces_closed, 1);
}

doe_vk_status __real_doe_vk_commands_release(doe_vk_commands *commands);
doe_vk_status __wrap_doe_vk_commands_release(doe_vk_commands *commands)
{
    bool tracked = commands && commands == atomic_load(&tracked_commands);
    doe_vk_status status = __real_doe_vk_commands_release(commands);
    if (tracked && status == DOE_VK_OK) {
        atomic_store(&tracked_commands, NULL);
        atomic_fetch_add(&commands_closed, 1);
    }
    return status;
}

void __real_native_backend_close(native_backend *backend);
void __wrap_native_backend_close(native_backend *backend)
{
    bool tracked = backend->kind == WGPUBackendType_Vulkan &&
        backend->value.vulkan.context == atomic_load(&tracked_context);
    __real_native_backend_close(backend);
    if (tracked) atomic_fetch_add(&contexts_closed, 1);
}

void __real_free(void *allocation);
void __wrap_free(void *allocation)
{
    if (allocation && allocation == atomic_load(&tracked_frontend)) atomic_fetch_add(&frontends_closed, 1);
    __real_free(allocation);
}

static void install(state *s, WGPUSurface surface)
{
    native_device *device = (native_device *)s->device;
    native_lock(device);
    doe_vk_context *context = device->backend.value.vulkan.context;
    actual = context->vk;
    atomic_store(&tracked_context, context);
    atomic_store(&tracked_frontend, surface);
    atomic_store(&tracked_commands, NULL);
    atomic_store(&acquire_fence, 0);
    atomic_store(&present_fence, 0);
    atomic_store(&hold_acquire, false);
    atomic_store(&hold_present, false);
    atomic_store(&hold_discard, false);
    atomic_store(&acquires, 0);
    atomic_store(&discards, 0);
    atomic_store(&commands_closed, 0);
    atomic_store(&swapchains_closed, 0);
    atomic_store(&surfaces_closed, 0);
    atomic_store(&contexts_closed, 0);
    atomic_store(&frontends_closed, 0);
    context->vk.vkWaitForFences = gated_wait;
    context->vk.vkAcquireNextImageKHR = observe_acquire;
    context->vk.vkQueuePresentKHR = observe_present;
    context->vk.vkReleaseSwapchainImagesEXT = observe_discard;
    context->vk.vkDestroySwapchainKHR = observe_swapchain_close;
    context->vk.vkDestroySurfaceKHR = observe_surface_close;
    native_unlock(device);
}

static void wait_count(atomic_uint *counter)
{
    for (unsigned i = 0; i < 500 && !atomic_load(counter); ++i) pause_ms(10);
    assert(atomic_load(counter) == 1);
}

static void wait_closed(void)
{
    wait_count(&contexts_closed);
    assert(atomic_load(&frontends_closed) == 1);
    assert(atomic_load(&swapchains_closed) == 1);
    assert(atomic_load(&surfaces_closed) == 1);
}

static WGPUSurface setup(state *s, xcb_connection_t *connection, xcb_window_t window)
{
    s->instance = wgpuCreateInstance(NULL);
    WGPUSurfaceSourceXCBWindow xcb = WGPU_SURFACE_SOURCE_XCB_WINDOW_INIT;
    xcb.connection = connection;
    xcb.window = window;
    WGPUSurfaceDescriptor descriptor = {.nextInChain=&xcb.chain};
    WGPUSurface surface = wgpuInstanceCreateSurface(s->instance, &descriptor);
    assert(surface);
    request_adapter(s, surface);
    configure(s, surface, choose_format(surface, s->adapter), 64, 48);
    assert(!s->errors);
    install(s, surface);
    return surface;
}

static void release_handles(state *s, WGPUSurface surface)
{
    wgpuSurfaceRelease(surface);
    wgpuQueueRelease(s->queue);
    wgpuDeviceRelease(s->device);
    wgpuAdapterRelease(s->adapter);
    wgpuInstanceRelease(s->instance);
}

static void presentation_release_all(xcb_connection_t *connection, xcb_window_t window)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    WGPUSurfaceTexture frame = acquire(surface);
    assert(frame.texture);
    WGPUTextureView view = wgpuTextureCreateView(frame.texture, NULL);
    assert(view);
    atomic_store(&hold_present, true);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Error && s.errors == 1);
    assert(atomic_load(&tracked_commands));
    wgpuTextureRelease(frame.texture);
    release_handles(&s, surface);
    assert(!atomic_load(&contexts_closed) && !atomic_load(&frontends_closed));
    atomic_store(&hold_present, false);
    wait_count(&commands_closed); /* No foreground operation observes the fence. */
    assert(!atomic_load(&swapchains_closed) && !atomic_load(&frontends_closed));
    wgpuTextureViewRelease(view); /* Last caller frame lease, after every public owner. */
    wait_closed();
    assert(atomic_load(&acquires) == 1 && !atomic_load(&discards));
}

static void acquisition_release_all(xcb_connection_t *connection, xcb_window_t window)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    atomic_store(&hold_acquire, true);
    WGPUSurfaceTexture frame = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(surface, &frame);
    assert(frame.status == WGPUSurfaceGetCurrentTextureStatus_Timeout && !frame.texture && !s.errors);
    assert(atomic_load(&acquires) == 1);
    release_handles(&s, surface);
    assert(!atomic_load(&contexts_closed) && !atomic_load(&frontends_closed));
    atomic_store(&hold_acquire, false);
    wait_closed();
    assert(atomic_load(&acquires) == 1); /* Wait existing acquire; never create another. */
    assert(atomic_load(&discards) == 1 && !atomic_load(&commands_closed));
}

static void discard_release_all(xcb_connection_t *connection, xcb_window_t window)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    WGPUSurfaceTexture frame = acquire(surface);
    assert(frame.texture);
    atomic_store(&hold_discard, true);
    wgpuTextureRelease(frame.texture);
    release_handles(&s, surface); /* No present command owns this acquired image. */
    assert(!atomic_load(&contexts_closed) && !atomic_load(&frontends_closed));
    assert(!atomic_load(&discards));
    atomic_store(&hold_discard, false);
    wait_closed();
    assert(atomic_load(&discards) == 1 && atomic_load(&acquires) == 1 && !atomic_load(&commands_closed));
}

static void presentation_retry(xcb_connection_t *connection, xcb_window_t window)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    WGPUSurfaceTexture first = acquire(surface);
    assert(first.texture);
    atomic_store(&hold_present, true);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Error && s.errors == 1);
    wgpuTextureRelease(first.texture);
    atomic_store(&hold_present, false);
    WGPUSurfaceTexture next = acquire(surface);
    assert(next.texture); /* Wait WSI before pending settlement can reject retry. */
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Success);
    wgpuTextureRelease(next.texture);
    release_handles(&s, surface);
    wait_closed();
    assert(atomic_load(&acquires) == 2 && atomic_load(&commands_closed) == 2);
}

static void completed_work(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Success && !message.length);
    ++*(unsigned *)userdata;
}

static void no_cleanup_error(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
    ++*(unsigned *)userdata;
}

static void cleanup_does_not_block_work_done(xcb_connection_t *connection, xcb_window_t window, bool acquisition)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    WGPUTextureView view = NULL;
    if (acquisition) {
        atomic_store(&hold_acquire, true);
        WGPUSurfaceTexture frame = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(surface, &frame);
        assert(frame.status == WGPUSurfaceGetCurrentTextureStatus_Timeout && !frame.texture);
    } else {
        WGPUSurfaceTexture frame = acquire(surface);
        assert(frame.texture);
        view = wgpuTextureCreateView(frame.texture, NULL);
        assert(view);
        wgpuTextureRelease(frame.texture);
    }
    wgpuSurfaceRelease(surface); /* Only cleanup ownership survives. */
    native_device *device = (native_device *)s.device;
    native_lock(device);
    assert(device->pending && !device->queue_failed);
    for (native_pending *pending = device->pending; pending; pending = pending->next)
        assert(pending->kind == NATIVE_PENDING_SURFACE);
    native_unlock(device);
    unsigned completed = 0, popped = 0;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Internal);
    WGPUQueueWorkDoneCallbackInfo work = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=completed_work,.userdata1=&completed};
    assert(wgpuQueueOnSubmittedWorkDone(s.queue, work).id && completed == 1);
    WGPUPopErrorScopeCallbackInfo error = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=no_cleanup_error,.userdata1=&popped};
    assert(wgpuDevicePopErrorScope(s.device, error).id && popped == 1 && !s.errors);
    assert(!atomic_load(&frontends_closed) && !atomic_load(&contexts_closed));
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    if (view) wgpuTextureViewRelease(view);
    atomic_store(&hold_acquire, false);
    wait_closed();
    assert(atomic_load(&acquires) == 1 && atomic_load(&discards) == 1);
}

static void configuration_generations(xcb_connection_t *connection, xcb_window_t window)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    WGPUSurfaceTexture first = acquire(surface);
    assert(first.texture);
    WGPUTextureView old_view = wgpuTextureCreateView(first.texture, NULL);
    assert(old_view);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Success);
    wgpuTextureRelease(first.texture);
    configure(&s, surface, choose_format(surface, s.adapter), 64, 48);
    assert(!s.errors && !atomic_load(&swapchains_closed) && !atomic_load(&surfaces_closed));
    WGPUSurfaceTexture second = acquire(surface);
    assert(second.texture);
    assert(wgpuSurfacePresent(surface) == WGPUStatus_Success);
    wgpuTextureRelease(second.texture);
    release_handles(&s, surface);
    wait_count(&swapchains_closed); /* New generation closes; old view still owns the first. */
    assert(!atomic_load(&surfaces_closed) && !atomic_load(&contexts_closed));
    wgpuTextureViewRelease(old_view);
    /* All work is already terminal: the caller can now destroy its window and
     * disconnect XCB without racing a deferred vkDestroySurfaceKHR. */
    assert(atomic_load(&swapchains_closed) == 2 && atomic_load(&surfaces_closed) == 1);
    wait_count(&contexts_closed);
    assert(atomic_load(&swapchains_closed) == 2 && atomic_load(&surfaces_closed) == 1);
    assert(atomic_load(&frontends_closed) == 1 && atomic_load(&acquires) == 2);
}

static void cached_error(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    const char *expected = "Vulkan: surface unconfigure cache (native=-994)";
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Internal);
    assert(message.length == strlen(expected) && !memcmp(message.data, expected, message.length));
    ++*(unsigned *)userdata;
}

static void unconfigure_preserves_failure(xcb_connection_t *connection, xcb_window_t window)
{
    state s = {0};
    WGPUSurface surface = setup(&s, connection, window);
    WGPUBuffer destination = buffer(&s, 8, WGPUBufferUsage_CopyDst);
    native_device *device = (native_device *)s.device;
    native_lock(device);
    device->retired_failure = (native_failure){.status=NATIVE_FAILURE,.backend=WGPUBackendType_Vulkan,
        .native_code=-994};
    strcpy(device->retired_failure.operation, "surface unconfigure cache");
    native_unlock(device);
    wgpuSurfaceUnconfigure(surface);
    unsigned errors = 0;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Internal);
    uint64_t value = 5;
    wgpuQueueWriteBuffer(s.queue, destination, 0, &value, sizeof(value));
    WGPUPopErrorScopeCallbackInfo popped = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=cached_error,.userdata1=&errors};
    assert(wgpuDevicePopErrorScope(s.device, popped).id && errors == 1 && !s.errors);
    wgpuBufferRelease(destination);
    release_handles(&s, surface);
    wait_closed();
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!getenv("DISPLAY")) return 77;
    int screen_number = 0;
    xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        fputs("SKIP surface progress: no reachable X display\n", stderr);
        return 77;
    }
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < screen_number; ++i) xcb_screen_next(&screens);
    xcb_screen_t *screen = screens.data;
    xcb_window_t window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 64, 48, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
    xcb_map_window(connection, window);
    xcb_flush(connection);
    pause_ms(200);
    presentation_release_all(connection, window);
    acquisition_release_all(connection, window);
    discard_release_all(connection, window);
    presentation_retry(connection, window);
    unconfigure_preserves_failure(connection, window);
    configuration_generations(connection, window);
    cleanup_does_not_block_work_done(connection, window, false);
    cleanup_does_not_block_work_done(connection, window, true);
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
    printf("PASS surface progress: real WSI fence visibility gates; release-all presentation, acquire-only and failed-discard retirement, "
        "held-view lifetime across two configuration generations, exactly-once native/frontend cleanup, retry ordering, cached-error delivery, WorkDone independent of view/acquire cleanup %s\n", adapter_class_scope());
}
