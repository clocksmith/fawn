/* SPDX-License-Identifier: MIT
 * Obscure a real software submission at the native boundary, release every
 * public handle, then require the existing pending owner to finish retirement. */
#include "../src/internal.h"
#include "consumer.h"
#include <errno.h>
#include <time.h>

static atomic_bool arm, hold, fail_thread;
static _Atomic(doe_vk_commands *) observed_commands;
static _Atomic(doe_vk_context *) observed_context;
static _Atomic(void *) observed_buffer;
static atomic_uint contexts_closed, buffers_closed;
static _Atomic(doe_vk_status) terminal_status;

int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
    void *(*run)(void *), void *argument)
{
    if (atomic_exchange(&fail_thread, false)) return EAGAIN;
    return __real_pthread_create(thread, attributes, run, argument);
}

doe_vk_status __real_doe_vk_commands_submit(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_poll(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_recover(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_release(doe_vk_commands *);
doe_vk_status __wrap_doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error)
{
    if (atomic_exchange(&arm, false)) atomic_store(&observed_commands, commands);
    return __real_doe_vk_commands_submit(commands, error);
}
doe_vk_status __wrap_doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_recover(commands, error);
    if (commands != atomic_load(&observed_commands)) return actual;
    assert(actual == DOE_VK_OK);
    doe_vk_status status = atomic_load(&hold) ? DOE_VK_PENDING : atomic_load(&terminal_status);
    if (error) *error = (doe_vk_error){status, -771, "injected background fence"};
    return status;
}
doe_vk_status __wrap_doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_status actual = __real_doe_vk_commands_poll(commands, error);
    /* Submission no longer waits: the real work may still be running. */
    if (commands != atomic_load(&observed_commands) || actual == DOE_VK_PENDING) return actual;
    assert(actual == DOE_VK_OK);
    doe_vk_status status = atomic_load(&hold) ? DOE_VK_PENDING : atomic_load(&terminal_status);
    if (error) *error = (doe_vk_error){status, -771, "injected background fence"};
    return status;
}
doe_vk_status __wrap_doe_vk_commands_release(doe_vk_commands *commands)
{
    bool tracked = commands && commands == atomic_load(&observed_commands);
    if (tracked && atomic_load(&hold)) return DOE_VK_BUSY;
    doe_vk_status status = __real_doe_vk_commands_release(commands);
    if (tracked && status == DOE_VK_OK) atomic_store(&observed_commands, NULL);
    return status;
}

void __real_native_backend_close(native_backend *);
void __wrap_native_backend_close(native_backend *backend)
{
    bool tracked = backend->kind == WGPUBackendType_Vulkan &&
        backend->value.vulkan.context == atomic_load(&observed_context);
    __real_native_backend_close(backend);
    if (tracked) atomic_fetch_add(&contexts_closed, 1);
}
void __real_native_buffer_close(native_buffer *);
void __wrap_native_buffer_close(native_buffer *payload)
{
    bool tracked = payload == atomic_load(&observed_buffer);
    __real_native_buffer_close(payload);
    if (tracked) atomic_fetch_add(&buffers_closed, 1);
}

static void wait_for_retirement(void)
{
    for (unsigned i = 0; i < 500 && !atomic_load(&contexts_closed); ++i) {
        struct timespec interval = {.tv_sec = 0, .tv_nsec = 10000000};
        while (nanosleep(&interval, &interval) && errno == EINTR) {}
    }
    assert(atomic_load(&contexts_closed) == 1);
    assert(atomic_load(&buffers_closed) == 1);
}

static void release_all_pending(doe_vk_status terminal)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    atomic_store(&observed_context, device->backend.value.vulkan.context);
    atomic_store(&contexts_closed, 0);
    atomic_store(&buffers_closed, 0);
    atomic_store(&terminal_status, terminal);
    WGPUBuffer target = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    atomic_store(&observed_buffer, doe_resource_buffer_native((doe_resource_buffer *)target));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderClearBuffer(encoder, target, 0, 16);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    atomic_store(&hold, true);
    atomic_store(&arm, true);
    wgpuQueueSubmit(s.queue, 1, &commands);
    native_lock(device);
    assert(device->pending && device->pending_progress_ready);
    native_unlock(device);
    assert(s.errors == 0); /* Accepted, still running: not an error. */
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(target);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    assert(!atomic_load(&contexts_closed) && !atomic_load(&buffers_closed));
    atomic_store(&hold, false);
    wait_for_retirement();
    atomic_store(&observed_context, NULL);
    atomic_store(&observed_buffer, NULL);
}

static void failed_device(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPURequestDeviceStatus_Error && !device);
    ++*(unsigned *)userdata;
}

static void startup_and_synchronous_release(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    atomic_store(&observed_context, device->backend.value.vulkan.context);
    atomic_store(&contexts_closed, 0);
    unsigned failures = 0;
    WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = failed_device, .userdata1 = &failures};
    atomic_store(&fail_thread, true);
    assert(wgpuAdapterRequestDevice(s.adapter, NULL, callback).id);
    assert(failures == 1 && !atomic_load(&fail_thread));
    consumer_finish(&s);
    /* An idle device's worker has joined before ordinary final release returns. */
    assert(atomic_load(&contexts_closed) == 1);
    atomic_store(&observed_context, NULL);
}

static void cached_failure(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Internal);
    const char *expected = "Vulkan: injected background fence (native=-771)";
    assert(message.length == strlen(expected) && !memcmp(message.data, expected, message.length));
    ++*(unsigned *)userdata;
}

static void background_failure_is_observed_once(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    WGPUBuffer target = buffer(&s, 8, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderClearBuffer(encoder, target, 0, 8);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    atomic_store(&terminal_status, DOE_VK_NATIVE_FAILURE);
    atomic_store(&hold, true);
    atomic_store(&arm, true);
    wgpuQueueSubmit(s.queue, 1, &commands);
    assert(s.errors == 0);
    wgpuCommandBufferRelease(commands);
    atomic_store(&hold, false);
    bool complete = false;
    for (unsigned i = 0; i < 500 && !complete; ++i) {
        native_lock(device);
        complete = !device->pending;
        if (complete) {
            assert(device->retired_failure.status == NATIVE_FAILURE);
            assert(device->retired_failure.native_code == -771);
            assert(!strcmp(device->retired_failure.operation, "injected background fence"));
        }
        native_unlock(device);
        if (!complete) {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
            while (nanosleep(&delay, &delay) && errno == EINTR) {}
        }
    }
    assert(complete);
    uint64_t value = 42;
    unsigned errors = 0;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue, target, 0, &value, sizeof(value));
    WGPUPopErrorScopeCallbackInfo popped = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = cached_failure, .userdata1 = &errors};
    assert(wgpuDevicePopErrorScope(s.device, popped).id && errors == 1);
    assert(*(const uint64_t *)map(&s, target, 8) == 0);
    wgpuBufferUnmap(target);
    wgpuQueueWriteBuffer(s.queue, target, 0, &value, sizeof(value));
    assert(*(const uint64_t *)map(&s, target, 8) == value && s.errors == 0);
    wgpuBufferUnmap(target);
    wgpuBufferRelease(target);
    consumer_finish(&s);
}

int main(void)
{
    startup_and_synchronous_release();
    background_failure_is_observed_once();
    release_all_pending(DOE_VK_OK);
    release_all_pending(DOE_VK_NATIVE_FAILURE);
    puts("PASS pending progress: startup failure publishes no device; idle final release joins; release-all retains running work and the worker retires success/failure by fence without a caller; background terminal error is observed once");
}
