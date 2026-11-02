/* SPDX-License-Identifier: MIT
 * Obscure a real software submission at the native boundary, release every
 * public handle, then require the existing pending owner to finish retirement. */
#include "../src/internal.h"
#include "../src/surface.h"
#include "consumer.h"
#include <errno.h>
#include <time.h>

static atomic_bool arm, hold, fail_thread;
static _Atomic(doe_vk_commands *) observed_commands;
static _Atomic(doe_vk_context *) observed_context;
static _Atomic(void *) observed_buffer;
static atomic_uint contexts_closed, buffers_closed;
static _Atomic(doe_vk_status) terminal_status;
/* The allocator retains one freed command allocation and deliberately reuses
 * its address. Native command construction/destruction and GPU work stay real. */
static atomic_bool reuse_enabled, reuse_in_use, reuse_first_admitted;
static atomic_bool reuse_first_blocked, reuse_second_blocked;
static atomic_bool reuse_pause_publish, reuse_publisher_paused, reuse_resume_publish;
static atomic_uint reuse_creations;
static _Atomic(void *) reuse_storage;
static _Atomic(pthread_cond_t *) reuse_condition;
static _Atomic(native_device *) reuse_device;
static size_t reuse_size;
static _Thread_local bool reuse_creating;

static void reuse_wait(atomic_bool *flag)
{
    for (unsigned i = 0; i < 5000 && !atomic_load(flag); ++i) {
        struct timespec interval = {.tv_sec = 0, .tv_nsec = 1000000};
        while (nanosleep(&interval, &interval) && errno == EINTR) {}
    }
    assert(atomic_load(flag));
}

void *__real_calloc(size_t, size_t);
void __real_free(void *);
void *__wrap_calloc(size_t count, size_t size)
{
    if (!reuse_creating) return __real_calloc(count, size);
    assert(count == 1 && !atomic_exchange(&reuse_in_use, true));
    void *storage = atomic_load(&reuse_storage);
    if (!storage) {
        storage = __real_calloc(count, size);
        assert(storage);
        reuse_size = size;
        atomic_store(&reuse_storage, storage);
    } else {
        assert(size == reuse_size);
        memset(storage, 0, size);
    }
    return storage;
}
void __wrap_free(void *storage)
{
    if (storage && storage == atomic_load(&reuse_storage)) {
        assert(atomic_exchange(&reuse_in_use, false));
        return;
    }
    __real_free(storage);
}

doe_vk_status __real_doe_vk_commands_create(doe_vk_context *, doe_vk_commands **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_commands_create(doe_vk_context *context,
    doe_vk_commands **out, doe_vk_error *error)
{
    if (!atomic_load(&reuse_enabled)) return __real_doe_vk_commands_create(context, out, error);
    assert(!reuse_creating);
    reuse_creating = true;
    doe_vk_status status = __real_doe_vk_commands_create(context, out, error);
    reuse_creating = false;
    assert(status == DOE_VK_OK && *out == atomic_load(&reuse_storage));
    atomic_fetch_add(&reuse_creations, 1);
    return status;
}

doe_vk_status __real_doe_vk_commands_block(const doe_vk_commands *, uint64_t);
doe_vk_status __wrap_doe_vk_commands_block(const doe_vk_commands *commands, uint64_t timeout)
{
    doe_vk_status status = __real_doe_vk_commands_block(commands, timeout);
    if (atomic_load(&reuse_enabled) && commands == atomic_load(&reuse_storage)) {
        if (status != DOE_VK_OK) return status;
        if (atomic_load(&reuse_creations) == 1) {
            /* Wait outside the device mutex: the consumer must still be able
             * to admit WorkDone while its actual GPU fence is observed. */
            reuse_wait(&reuse_first_admitted);
            atomic_store(&reuse_first_blocked, true);
        } else atomic_store(&reuse_second_blocked, true);
    }
    return status;
}

void __real_native_async_publish_ready(native_pending *);
void __wrap_native_async_publish_ready(native_pending *pending)
{
    bool pause = atomic_load(&reuse_enabled) && atomic_exchange(&reuse_pause_publish, false);
    __real_native_async_publish_ready(pending);
    if (pause) {
        /* Publication has dropped the device lock and the first native command
         * is already destroyed. Reuse its address before the worker reacquires. */
        atomic_store(&reuse_publisher_paused, true);
        reuse_wait(&reuse_resume_publish);
    }
}

int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int __wrap_pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
    const struct timespec *deadline)
{
    if (condition == atomic_load(&reuse_condition) && atomic_load(&reuse_creations) == 2)
        assert(atomic_load(&reuse_second_blocked) && "new submission must observe its own fence before fallback");
    return __real_pthread_cond_timedwait(condition, mutex, deadline);
}

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
    if (atomic_load(&reuse_enabled) && commands == atomic_load(&reuse_storage)) {
        unsigned generation = atomic_load(&reuse_creations);
        bool blocked = generation == 1 ? atomic_load(&reuse_first_blocked) :
            atomic_load(&reuse_second_blocked);
        /* Keep the real submitted command unobserved until the worker waits
         * its fence. Public admission must not settle it ahead of that wait. */
        native_device *device = atomic_load(&reuse_device);
        if (!blocked || !pthread_equal(pthread_self(), doe_thread_identity(&device->pending_thread))) {
            if (error) *error = (doe_vk_error){DOE_VK_PENDING, 0, "reused command awaiting fence observation"};
            return DOE_VK_PENDING;
        }
    }
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

static void reused_done(WGPUQueueWorkDoneStatus status, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Success);
    ++*(unsigned *)userdata;
}

static WGPUFuture reused_submit(state *s, WGPUBuffer target, unsigned *done)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderClearBuffer(encoder, target, 0, 8);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    WGPUQueueWorkDoneCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = reused_done, .userdata1 = done};
    return wgpuQueueOnSubmittedWorkDone(s->queue, callback);
}

static void reused_address_is_a_new_submission(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    WGPUBuffer target = buffer(&s, 8, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    atomic_store(&reuse_device, device);
    atomic_store(&reuse_condition, &device->pending_changed.native);
    atomic_store(&reuse_pause_publish, true);
    atomic_store(&reuse_enabled, true);
    unsigned first = 0, second = 0;
    WGPUFuture first_future = reused_submit(&s, target, &first);
    assert(first_future.id);
    atomic_store(&reuse_first_admitted, true);
    wait_future(&s, first_future);
    assert(first == 1);
    reuse_wait(&reuse_publisher_paused);
    assert(atomic_load(&reuse_creations) == 1 && !atomic_load(&reuse_in_use));

    /* The first publisher is outside every device lock. A second complete
     * public submit/admission reuses its now-freed native allocation exactly. */
    WGPUFuture second_future = reused_submit(&s, target, &second);
    assert(second_future.id && atomic_load(&reuse_creations) == 2);
    native_lock(device);
    uint64_t serial = 0;
    for (native_pending *record = device->pending; record; record = record->next) {
        if (record->kind != NATIVE_PENDING_COMMANDS) continue;
        assert(record->native.kind == WGPUBackendType_Vulkan);
        assert(record->native.value.vulkan == atomic_load(&reuse_storage));
        serial = record->serial;
    }
    assert(serial == device->submitted_serial && serial > 1);
    native_unlock(device);
    atomic_store(&reuse_resume_publish, true);
    wait_future(&s, second_future);
    assert(second == 1 && atomic_load(&reuse_second_blocked));
    assert(!atomic_load(&reuse_in_use));
    atomic_store(&reuse_condition, NULL);
    atomic_store(&reuse_enabled, false);
    assert(*(const uint64_t *)map(&s, target, 8) == 0);
    wgpuBufferUnmap(target);
    wgpuBufferRelease(target);
    consumer_finish(&s);
    void *storage = atomic_exchange(&reuse_storage, NULL);
    __real_free(storage);
    atomic_store(&reuse_device, NULL);
}

int main(void)
{
    startup_and_synchronous_release();
    background_failure_is_observed_once();
    release_all_pending(DOE_VK_OK);
    release_all_pending(DOE_VK_NATIVE_FAILURE);
    reused_address_is_a_new_submission();
    printf("PASS pending progress: startup/idle join, release-all fence retirement, exact failure once, and reused command address waits its new submission fence before timed fallback (%s)\n", adapter_class_scope());
}
