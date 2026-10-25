/* SPDX-License-Identifier: MIT
 * Vulkan queue submission returns once the queue accepts the work (owner
 * authorized 2026-10-24: async submit A+B, D2, fence-wait completion; futures
 * later; reconfirmed after the section 7 risk review). GNU wrappers at the real backend boundary count fence waits and can
 * withhold observed completion of one tracked submission, so each invariant
 * is checked deterministically on actual software Vulkan work:
 * - submit performs no fence wait; running work keeps its resource leases
 *   after every public handle is released, until it is proven terminal;
 * - settlement is oldest first, so the earliest submission's failure is the
 *   one reported, once;
 * - map and work-done callbacks keep request order and see completed work;
 * - device loss with work in flight retires it as failed without crashing. */
#include "../src/internal.h"
#include "../src/surface.h" /* native_pending */
#include "../../backends/vulkan/src/internal.h" /* Marks the context lost. */
#include "consumer.h"
#include <errno.h>
#include <string.h>
#include <time.h>

enum { TRACKED = 2 };
static atomic_bool arm, hold, refused_held, later_held, first_unobserved;
static atomic_uint later_blocks;
static atomic_uint armed_slot, waits, submits;
static _Atomic(doe_vk_commands *) tracked[TRACKED];
static _Atomic(doe_vk_status) injected[TRACKED];
static _Atomic(void *) observed_buffer;
static atomic_uint buffers_closed;

static int slot_of(const doe_vk_commands *commands)
{
    for (int i = 0; i < TRACKED; ++i) if (commands && atomic_load(&tracked[i]) == commands) return i;
    return -1;
}
doe_vk_status __real_doe_vk_commands_submit(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_wait(doe_vk_commands *, uint64_t, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_poll(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_recover(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_release(doe_vk_commands *);
doe_vk_status __wrap_doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error)
{
    if (atomic_exchange(&arm, false)) atomic_store(&tracked[atomic_load(&armed_slot)], commands);
    atomic_fetch_add(&submits, 1);
    return __real_doe_vk_commands_submit(commands, error);
}
doe_vk_status __wrap_doe_vk_commands_wait(doe_vk_commands *commands, uint64_t timeout, doe_vk_error *error)
{
    atomic_fetch_add(&waits, 1);
    return __real_doe_vk_commands_wait(commands, timeout, error);
}
/* A tracked submission reports running while held, then its injected
 * terminal status; untracked work and a lost device pass through. */
static doe_vk_status observed(doe_vk_commands *commands, doe_vk_status actual, doe_vk_error *error)
{
    int slot = slot_of(commands);
    if (slot < 0 || actual == DOE_VK_PENDING || actual == DOE_VK_DEVICE_LOST) return actual;
    if (slot == 0 && atomic_load(&refused_held)) return DOE_VK_PENDING; /* Recovery itself unresolved. */
    if (actual != DOE_VK_OK) return actual; /* A refused submission keeps its own failure. */
    doe_vk_status status = atomic_load(&hold) ? DOE_VK_PENDING : atomic_load(&injected[slot]);
    if (error && status != DOE_VK_OK)
        *error = (doe_vk_error){status, -900 - slot, slot ? "second injected fence" : "first injected fence"};
    return status;
}
doe_vk_status __wrap_doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error)
{
    /* Withheld before the real poll, so the record stays accepted, unobserved. */
    if (atomic_load(&later_held) && commands == atomic_load(&tracked[1])) return DOE_VK_PENDING;
    if (atomic_load(&first_unobserved) && commands == atomic_load(&tracked[0])) return DOE_VK_PENDING;
    return observed(commands, __real_doe_vk_commands_poll(commands, error), error);
}
static atomic_uint recovers; /* Counts native vkQueueWaitIdle calls. */
static VkResult (*real_idle)(VkQueue);
static VkResult counted_idle(VkQueue queue)
{
    atomic_fetch_add(&recovers, 1);
    return real_idle(queue);
}
doe_vk_status __wrap_doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error)
{
    return observed(commands, __real_doe_vk_commands_recover(commands, error), error);
}
doe_vk_status __wrap_doe_vk_commands_release(doe_vk_commands *commands)
{
    int slot = slot_of(commands);
    if (slot >= 0 && (atomic_load(&hold) || (slot == 0 && atomic_load(&refused_held)))) return DOE_VK_BUSY;
    doe_vk_status status = __real_doe_vk_commands_release(commands);
    if (slot >= 0 && status == DOE_VK_OK) atomic_store(&tracked[slot], NULL);
    return status;
}
doe_vk_status __real_doe_vk_commands_block(const doe_vk_commands *, uint64_t);
doe_vk_status __wrap_doe_vk_commands_block(const doe_vk_commands *commands, uint64_t timeout)
{
    if (commands == atomic_load(&tracked[1])) atomic_fetch_add(&later_blocks, 1);
    return __real_doe_vk_commands_block(commands, timeout);
}
static VkResult (*accept_submit)(VkQueue, uint32_t, const VkSubmitInfo *, VkFence);
static doe_vk_context *refusing;
static VkResult refuse_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence)
{
    (void)queue; (void)count; (void)info; (void)fence;
    refusing->vk.vkQueueSubmit = accept_submit; /* Refuse exactly once. */
    return -1; /* VK_ERROR_OUT_OF_HOST_MEMORY: nothing was submitted. */
}
void __real_native_buffer_close(native_buffer *);
void __wrap_native_buffer_close(native_buffer *payload)
{
    bool counted = payload == atomic_load(&observed_buffer);
    __real_native_buffer_close(payload);
    if (counted) atomic_fetch_add(&buffers_closed, 1);
}

static void track(unsigned slot, doe_vk_status terminal)
{
    atomic_store(&armed_slot, slot);
    atomic_store(&injected[slot], terminal);
    atomic_store(&arm, true);
}

static WGPUCommandBuffer copy_commands(state *s, WGPUBuffer source, WGPUBuffer destination, uint64_t size)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, destination, 0, size);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    return commands;
}

static bool linked(native_device *device)
{
    native_lock(device);
    bool result = device->pending != NULL;
    native_unlock(device);
    return result;
}

/* Submit returns without a fence wait; released handles keep their leases. */
static void leases_until_completion(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    const uint32_t words[4] = {11, 22, 33, 44};
    WGPUBuffer source = buffer(&s, sizeof(words), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer destination = buffer(&s, sizeof(words), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s.queue, source, 0, words, sizeof(words));
    atomic_store(&observed_buffer, doe_resource_buffer_native((doe_resource_buffer *)source));
    atomic_store(&buffers_closed, 0);
    WGPUCommandBuffer commands = copy_commands(&s, source, destination, sizeof(words));
    track(0, DOE_VK_OK);
    atomic_store(&hold, true);
    atomic_store(&waits, 0);
    wgpuQueueSubmit(s.queue, 1, &commands);
    assert(atomic_load(&waits) == 0 && linked(device) && !s.errors);
    wgpuCommandBufferRelease(commands);
    wgpuBufferDestroy(source);
    wgpuBufferRelease(source);
    assert(atomic_load(&buffers_closed) == 0); /* The running copy holds the source. */
    atomic_store(&hold, false);
    work_done(&s);
    assert(!linked(device) && atomic_load(&buffers_closed) == 1);
    assert(!memcmp(map(&s, destination, sizeof(words)), words, sizeof(words)));
    wgpuBufferUnmap(destination);
    wgpuBufferRelease(destination);
    atomic_store(&observed_buffer, NULL);
    consumer_finish(&s);
}

static void oom_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)userdata; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_OutOfMemory);
}
static void scoped_error(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success);
    const char *expected = userdata;
    if (!expected) {
        assert(type == WGPUErrorType_NoError);
        return;
    }
    assert(type == WGPUErrorType_Internal);
    assert(message.length >= strlen(expected) && strstr(message.data, expected));
}
static void expect_scope(state *s, const char *expected)
{
    WGPUPopErrorScopeCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = scoped_error, .userdata1 = (void *)expected};
    assert(wgpuDevicePopErrorScope(s->device, callback).id);
}

/* Two failing submissions: settlement is oldest first, so the earliest's
 * error is the one reported, and only once. */
static void first_error_kept(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    WGPUBuffer source = buffer(&s, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer first = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    WGPUBuffer second = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    atomic_store(&hold, true);
    WGPUCommandBuffer commands[2] = {copy_commands(&s, source, first, 16), copy_commands(&s, source, second, 16)};
    track(0, DOE_VK_NATIVE_FAILURE);
    wgpuQueueSubmit(s.queue, 1, &commands[0]);
    track(1, DOE_VK_NATIVE_FAILURE);
    wgpuQueueSubmit(s.queue, 1, &commands[1]);
    assert(!s.errors && linked(device));
    atomic_store(&hold, false);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Internal);
    uint64_t value = 7;
    wgpuQueueWriteBuffer(s.queue, source, 0, &value, sizeof(value));
    expect_scope(&s, "first injected fence");
    assert(!linked(device));
    /* Consumed once: the next operation reports nothing. */
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Internal);
    wgpuQueueWriteBuffer(s.queue, source, 0, &value, sizeof(value));
    expect_scope(&s, NULL);
    wgpuCommandBufferRelease(commands[0]);
    wgpuCommandBufferRelease(commands[1]);
    wgpuBufferRelease(source);
    wgpuBufferRelease(first);
    wgpuBufferRelease(second);
    consumer_finish(&s);
}

typedef struct order_log { char events[8]; unsigned count; } order_log;
static void ordered_map(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    assert(status == WGPUMapAsyncStatus_Success);
    order_log *log = userdata;
    log->events[log->count++] = unused ? 'b' : 'a';
}
static void ordered_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Success);
    order_log *log = userdata;
    log->events[log->count++] = 'd';
}

/* Map, work done, map: callbacks keep request order and see completed work. */
static void map_and_work_done_order(void)
{
    state s = {0};
    consumer_init(&s);
    const uint32_t words[4] = {5, 6, 7, 8};
    WGPUBuffer source = buffer(&s, sizeof(words), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer a = buffer(&s, sizeof(words), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUBuffer b = buffer(&s, sizeof(words), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s.queue, source, 0, words, sizeof(words));
    WGPUCommandBuffer commands[2] = {copy_commands(&s, source, a, sizeof(words)), copy_commands(&s, source, b, sizeof(words))};
    wgpuQueueSubmit(s.queue, 2, commands);
    order_log log = {0};
    WGPUBufferMapCallbackInfo map_a = {.mode = WGPUCallbackMode_AllowProcessEvents, .callback = ordered_map, .userdata1 = &log};
    WGPUBufferMapCallbackInfo map_b = map_a;
    map_b.userdata2 = &log;
    WGPUQueueWorkDoneCallbackInfo done = {.mode = WGPUCallbackMode_AllowProcessEvents, .callback = ordered_done, .userdata1 = &log};
    assert(wgpuBufferMapAsync(a, WGPUMapMode_Read, 0, sizeof(words), map_a).id);
    assert(wgpuQueueOnSubmittedWorkDone(s.queue, done).id);
    assert(wgpuBufferMapAsync(b, WGPUMapMode_Read, 0, sizeof(words), map_b).id);
    wgpuInstanceProcessEvents(s.instance);
    assert(log.count == 3 && !memcmp(log.events, "adb", 3));
    assert(!memcmp(wgpuBufferGetConstMappedRange(a, 0, sizeof(words)), words, sizeof(words)));
    assert(!memcmp(wgpuBufferGetConstMappedRange(b, 0, sizeof(words)), words, sizeof(words)));
    wgpuBufferUnmap(a);
    wgpuBufferUnmap(b);
    wgpuCommandBufferRelease(commands[0]);
    wgpuCommandBufferRelease(commands[1]);
    wgpuBufferRelease(source);
    wgpuBufferRelease(a);
    wgpuBufferRelease(b);
    consumer_finish(&s);
}

static void loss_reply(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)device; (void)message; (void)unused;
    if (reason == WGPUDeviceLostReason_Unknown) ++*(unsigned *)userdata;
}
static void failed_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUQueueWorkDoneStatus_Error);
    ++*(unsigned *)userdata;
}

/* Loss while work runs: the record retires as failed, its leases settle and
 * the device reports loss; nothing waits forever or frees early. */
static void loss_with_work_in_flight(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = adapter_reply, .userdata1 = &s};
    assert(wgpuInstanceRequestAdapter(s.instance, &options, adapter).id && s.adapter);
    unsigned losses = 0;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.deviceLostCallbackInfo = (WGPUDeviceLostCallbackInfo){.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = loss_reply, .userdata1 = &losses};
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = error_reply, .userdata1 = &s};
    WGPURequestDeviceCallbackInfo requested = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = device_reply, .userdata1 = &s};
    assert(wgpuAdapterRequestDevice(s.adapter, &descriptor, requested).id && s.device);
    s.queue = wgpuDeviceGetQueue(s.device);
    native_device *device = (native_device *)s.device;
    WGPUBuffer source = buffer(&s, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer destination = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    atomic_store(&observed_buffer, doe_resource_buffer_native((doe_resource_buffer *)source));
    atomic_store(&buffers_closed, 0);
    WGPUCommandBuffer commands = copy_commands(&s, source, destination, 16);
    track(0, DOE_VK_OK);
    atomic_store(&hold, true);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuBufferRelease(source);
    native_lock(device);
    doe_vk_commands *running = atomic_load(&tracked[0]);
    assert(running && device->pending);
    /* The real work has finished on the CPU device; the loss arrives first. */
    assert(doe_vk_commands_block(running, UINT64_MAX) == DOE_VK_OK);
    device->backend.value.vulkan.context->lost = true;
    native_unlock(device);
    atomic_store(&hold, false);
    unsigned failed = 0;
    WGPUQueueWorkDoneCallbackInfo done = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = failed_done, .userdata1 = &failed};
    assert(wgpuQueueOnSubmittedWorkDone(s.queue, done).id && failed == 1);
    assert(!linked(device) && atomic_load(&buffers_closed) == 1 && !atomic_load(&tracked[0]));
    uint64_t value = 1;
    wgpuQueueWriteBuffer(s.queue, destination, 0, &value, sizeof(value)); /* Reports the loss. */
    assert(losses == 1);
    wgpuBufferRelease(destination);
    atomic_store(&observed_buffer, NULL);
    s.errors = 0;
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
}

/* A refused submission whose recovery stays unresolved (its fence never
 * signals) must not strand later work: the worker skips it and retires the
 * next accepted submission by waiting on that fence, with no caller. */
static void refused_submission_does_not_strand_later_work(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    WGPUBuffer source = buffer(&s, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer first = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    WGPUBuffer second = buffer(&s, 16, WGPUBufferUsage_CopyDst);
    /* Refused work poisons the buffers it used, so the later copy has its own. */
    WGPUBuffer later_source = buffer(&s, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUCommandBuffer commands[2] = {copy_commands(&s, source, first, 16), copy_commands(&s, later_source, second, 16)};
    refusing = device->backend.value.vulkan.context;
    accept_submit = refusing->vk.vkQueueSubmit;
    refusing->vk.vkQueueSubmit = refuse_submit;
    atomic_store(&refused_held, true);
    track(0, DOE_VK_OK);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_OutOfMemory);
    wgpuQueueSubmit(s.queue, 1, &commands[0]);
    WGPUPopErrorScopeCallbackInfo popped = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = oom_scope};
    assert(wgpuDevicePopErrorScope(s.device, popped).id);
    assert(atomic_load(&tracked[0]) && linked(device)); /* Unresolved: still linked with its leases. */
    atomic_store(&later_blocks, 0);
    atomic_store(&later_held, true);
    track(1, DOE_VK_OK);
    wgpuQueueSubmit(s.queue, 1, &commands[1]);
    doe_vk_commands *later = atomic_load(&tracked[1]);
    assert(later);
    /* The worker passes over the refused record and waits on the later fence. */
    for (unsigned i = 0; i < 500 && !atomic_load(&later_blocks); ++i) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
    }
    assert(atomic_load(&later_blocks) >= 1);
    atomic_store(&later_held, false);
    bool retired = false;
    for (unsigned i = 0; i < 500 && !retired; ++i) {
        native_lock(device);
        retired = true;
        for (native_pending *pending = device->pending; pending; pending = pending->next)
            if (pending->native.value.vulkan == later) retired = false;
        native_unlock(device);
        if (!retired) {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
            while (nanosleep(&delay, &delay) && errno == EINTR) {}
        }
    }
    assert(retired && linked(device));
    atomic_store(&refused_held, false);
    native_lock(device);
    (void)native_settle_pending(device); /* Recovery resolves the refused record. */
    native_unlock(device);
    assert(!linked(device) && !atomic_load(&tracked[0]));
    wgpuCommandBufferRelease(commands[0]);
    wgpuCommandBufferRelease(commands[1]);
    wgpuBufferRelease(source);
    wgpuBufferRelease(later_source);
    wgpuBufferRelease(first);
    wgpuBufferRelease(second);
    s.errors = 0;
    consumer_finish(&s);
}

/* Host access waits only for work that uses the buffer (phase C):
 * a write or map of an unrelated buffer neither waits nor observes running
 * work; a write to a buffer an in-flight copy reads waits for that copy
 * first; work done waits each record's own fence, never queue idle. */
static void targeted_host_access(void)
{
    state s = {0};
    consumer_init(&s);
    native_device *device = (native_device *)s.device;
    doe_vk_context *context = device->backend.value.vulkan.context;
    real_idle = context->vk.vkQueueWaitIdle;
    context->vk.vkQueueWaitIdle = counted_idle;
    const uint32_t before[4] = {1, 2, 3, 4}, after[4] = {9, 8, 7, 6};
    WGPUBuffer source = buffer(&s, sizeof(before), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer destination = buffer(&s, sizeof(before), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUBuffer unrelated = buffer(&s, sizeof(before), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s.queue, source, 0, before, sizeof(before));
    wgpuQueueWriteBuffer(s.queue, unrelated, 0, after, sizeof(after));
    WGPUCommandBuffer commands = copy_commands(&s, source, destination, sizeof(before));
    track(0, DOE_VK_OK);
    atomic_store(&hold, true);
    atomic_store(&first_unobserved, true); /* Its buffer uses stay counted until a wait. */
    wgpuQueueSubmit(s.queue, 1, &commands);
    atomic_store(&waits, 0);
    atomic_store(&recovers, 0);
    wgpuQueueWriteBuffer(s.queue, unrelated, 0, after, sizeof(after));
    assert(!memcmp(map(&s, unrelated, sizeof(after)), after, sizeof(after)));
    wgpuBufferUnmap(unrelated);
    assert(atomic_load(&waits) == 0 && atomic_load(&recovers) == 0 && linked(device) && !s.errors);
    /* The copy reads source: writing source waits for it, so the copy saw the old bytes. */
    wgpuQueueWriteBuffer(s.queue, source, 0, after, sizeof(after));
    assert(atomic_load(&waits) >= 1 && atomic_load(&recovers) == 0 && !s.errors);
    atomic_store(&first_unobserved, false);
    atomic_store(&hold, false);
    atomic_store(&recovers, 0);
    work_done(&s);
    assert(!linked(device) && atomic_load(&recovers) == 0);
    assert(!memcmp(map(&s, destination, sizeof(before)), before, sizeof(before)));
    wgpuBufferUnmap(destination);
    WGPUCommandBuffer again = copy_commands(&s, source, destination, sizeof(after));
    wgpuQueueSubmit(s.queue, 1, &again);
    assert(!memcmp(map(&s, destination, sizeof(after)), after, sizeof(after)));
    wgpuBufferUnmap(destination);
    assert(atomic_load(&recovers) == 0);
    wgpuCommandBufferRelease(commands);
    wgpuCommandBufferRelease(again);
    wgpuBufferRelease(source);
    wgpuBufferRelease(destination);
    wgpuBufferRelease(unrelated);
    consumer_finish(&s);
}

static WGPUTexture zero_texture(state *s, WGPUTextureFormat format, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){4, 4, 1};
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(texture);
    return texture;
}

/* New textures record zero initialization into one device batch (D2): no
 * submission until the next queue submission, which runs the batch first,
 * so the first use already sees zeros. Upload (copy-dst), color attachment
 * clear and depth attachment clear paths are covered, plus discarding an
 * unsubmitted batch at final release. */
static void zero_initialization_before_first_use(void)
{
    state s = {0};
    consumer_init(&s);
    atomic_store(&submits, 0);
    WGPUTexture upload = zero_texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc);
    WGPUTexture color = zero_texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);
    WGPUTexture depth = zero_texture(&s, WGPUTextureFormat_Depth32Float, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);
    WGPUTexture unused = zero_texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst);
    wgpuTextureRelease(unused); /* Released before its batch ever runs. */
    assert(atomic_load(&submits) == 0);
    enum { ROW = 256, BYTES = ROW * 4 };
    WGPUBuffer out[3];
    for (int i = 0; i < 3; ++i) out[i] = buffer(&s, BYTES, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUTextureView view = wgpuTextureCreateView(color, NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Load; /* Reads the initialized contents. */
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &attachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUTexture sources[3] = {upload, color, depth};
    for (int i = 0; i < 3; ++i) {
        WGPUTexelCopyTextureInfo from = {.texture = sources[i],
            .aspect = i == 2 ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All};
        WGPUTexelCopyBufferInfo to = {.buffer = out[i], .layout = {.bytesPerRow = ROW, .rowsPerImage = 4}};
        WGPUExtent3D extent = {4, 4, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &extent);
    }
    submit(&s, encoder);
    assert(atomic_load(&submits) == 2); /* The initialization batch, then the work. */
    for (int i = 0; i < 3; ++i) {
        const unsigned char *bytes = map(&s, out[i], BYTES);
        for (int row = 0; row < 4; ++row)
            for (int b = 0; b < 16; ++b) assert(bytes[row * ROW + b] == 0);
        wgpuBufferUnmap(out[i]);
        wgpuBufferRelease(out[i]);
    }
    wgpuTextureViewRelease(view);
    for (int i = 0; i < 3; ++i) wgpuTextureRelease(sources[i]);
    WGPUTexture pending = zero_texture(&s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst);
    wgpuTextureRelease(pending);
    consumer_finish(&s); /* Final release discards the never-submitted batch. */
    assert(atomic_load(&submits) == 2);
}

int main(void)
{
    leases_until_completion();
    first_error_kept();
    map_and_work_done_order();
    loss_with_work_in_flight();
    refused_submission_does_not_strand_later_work();
    targeted_host_access();
    zero_initialization_before_first_use();
    printf("PASS async submit: no fence wait at Vulkan submit, released handles keep leases until completion, oldest-first settlement keeps the first error once, map/work-done callbacks in request order over completed work, loss with work in flight retires it as failed, a refused unresolved submission does not strand later work the worker retires by fence, host access waits only for work using its buffer and work done never waits for queue idle, batched zero initialization runs before first use with no submission of its own %s\n",
        adapter_class_scope());
}
