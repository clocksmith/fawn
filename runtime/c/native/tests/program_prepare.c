/* SPDX-License-Identifier: MIT
 * Failed unpublished preparation must restore the same public command buffer.
 * GNU wrappers inject at real Vulkan allocation/record/seal procedures; fallback
 * executes through CPU Vulkan and checks independent scalar expected values. */
#include "../src/internal.h"
#include "../../backends/vulkan/src/internal.h"
#include "consumer.h"
#include "doe_native_programs.h"
#include <errno.h>
#include <time.h>

typedef enum preparation_fault { FAULT_NONE, FAULT_ALLOCATION, FAULT_RECORD, FAULT_SEAL } preparation_fault;
static preparation_fault fault;
static atomic_uint submits, faults, closed_recordings, closed_contexts;
static atomic_bool arm_unknown, hold_unknown;
static _Atomic(doe_vk_commands *) unknown;
static _Atomic(doe_vk_context *) observed_context;
static _Thread_local unsigned queue_allocation_countdown, queue_allocation_calls;
static atomic_uint queue_allocation_faults, command_pool_allocations;
static VkResult (*command_pool_create)(VkDevice, const VkCommandPoolCreateInfo *,
    const VkAllocationCallbacks *, VkCommandPool *);

void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size)
{
    if (queue_allocation_countdown) {
        ++queue_allocation_calls;
        if (!--queue_allocation_countdown) {
            atomic_fetch_add(&queue_allocation_faults, 1);
            errno = ENOMEM;
            return NULL;
        }
    }
    return __real_calloc(count, size);
}

static VkResult counted_command_pool(VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *out)
{
    atomic_fetch_add(&command_pool_allocations, 1);
    return command_pool_create(device, info, allocator, out);
}

static void count_command_pools(state *s, bool enable)
{
    native_device *device = (native_device *)s->device;
    native_lock(device);
    doe_vk_context *context = device->backend.value.vulkan.context;
    if (enable) {
        command_pool_create = context->vk.vkCreateCommandPool;
        context->vk.vkCreateCommandPool = counted_command_pool;
        atomic_store(&command_pool_allocations, 0);
    } else context->vk.vkCreateCommandPool = command_pool_create;
    native_unlock(device);
}

static VkResult allocation_failure(VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *out)
{
    (void)device; (void)info; (void)allocator; (void)out;
    atomic_fetch_add(&faults, 1);
    return -1; /* VK_ERROR_OUT_OF_HOST_MEMORY, from the allocation procedure. */
}

static VkResult descriptor_failure(VkDevice device, const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorPool *out)
{
    (void)device; (void)info; (void)allocator; (void)out;
    atomic_fetch_add(&faults, 1);
    return -1;
}

/* Descriptor sets come from long-lived context blocks: set allocation is the
 * recording-time native allocation whenever a block already exists. */
static VkResult set_failure(VkDevice device, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *out)
{
    (void)device; (void)info; (void)out;
    atomic_fetch_add(&faults, 1);
    return -1;
}

static VkResult sealing_failure(VkCommandBuffer command)
{
    (void)command;
    atomic_fetch_add(&faults, 1);
    return -1;
}

doe_vk_status __real_doe_vk_commands_create_reusable(doe_vk_context *, doe_vk_commands **, doe_vk_error *);
doe_vk_status __wrap_doe_vk_commands_create_reusable(doe_vk_context *context, doe_vk_commands **out, doe_vk_error *error)
{
    doe_vk_functions saved = context->vk;
    if (fault == FAULT_ALLOCATION) context->vk.vkCreateCommandPool = allocation_failure;
    doe_vk_status status = __real_doe_vk_commands_create_reusable(context, out, error);
    context->vk.vkCreateCommandPool = saved.vkCreateCommandPool;
    return status;
}

doe_vk_status __real_doe_vk_commands_dispatch_resources(doe_vk_commands *, doe_vk_pipeline *,
    const doe_vk_binding_resource *, size_t, uint32_t, uint32_t, uint32_t);
doe_vk_status __wrap_doe_vk_commands_dispatch_resources(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t count, uint32_t x, uint32_t y, uint32_t z)
{
    doe_vk_context *context = commands->context;
    doe_vk_functions saved = context->vk;
    if (fault == FAULT_RECORD) {
        context->vk.vkCreateDescriptorPool = descriptor_failure;
        context->vk.vkAllocateDescriptorSets = set_failure;
    }
    doe_vk_status status = __real_doe_vk_commands_dispatch_resources(commands, pipeline, bindings, count, x, y, z);
    context->vk.vkCreateDescriptorPool = saved.vkCreateDescriptorPool;
    context->vk.vkAllocateDescriptorSets = saved.vkAllocateDescriptorSets;
    return status;
}

doe_vk_status __real_doe_vk_commands_seal(doe_vk_commands *, doe_vk_error *);
doe_vk_status __wrap_doe_vk_commands_seal(doe_vk_commands *commands, doe_vk_error *error)
{
    doe_vk_context *context = commands->context;
    doe_vk_functions saved = context->vk;
    if (fault == FAULT_SEAL) context->vk.vkEndCommandBuffer = sealing_failure;
    doe_vk_status status = __real_doe_vk_commands_seal(commands, error);
    context->vk.vkEndCommandBuffer = saved.vkEndCommandBuffer;
    return status;
}

doe_vk_status __real_doe_vk_commands_submit(doe_vk_commands *, doe_vk_error *);
doe_vk_status __wrap_doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error)
{
    atomic_fetch_add(&submits, 1);
    if (atomic_exchange(&arm_unknown, false)) atomic_store(&unknown, commands);
    return __real_doe_vk_commands_submit(commands, error);
}

doe_vk_status __real_doe_vk_commands_poll(doe_vk_commands *, doe_vk_error *);
doe_vk_status __real_doe_vk_commands_recover(doe_vk_commands *, doe_vk_error *);
static doe_vk_status visible_completion(doe_vk_commands *commands, doe_vk_status actual, doe_vk_error *error)
{
    if (commands != atomic_load(&unknown)) return actual;
    assert(actual == DOE_VK_OK); /* Real fence already established native completion. */
    if (!atomic_load(&hold_unknown)) return actual;
    if (error) *error = (doe_vk_error){DOE_VK_PENDING, 0, "program fence visibility withheld"};
    return DOE_VK_PENDING;
}
doe_vk_status __wrap_doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error)
{
    return visible_completion(commands, __real_doe_vk_commands_poll(commands, error), error);
}
doe_vk_status __wrap_doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error)
{
    return visible_completion(commands, __real_doe_vk_commands_recover(commands, error), error);
}

doe_vk_status __real_doe_vk_commands_release(doe_vk_commands *);
doe_vk_status __wrap_doe_vk_commands_release(doe_vk_commands *commands)
{
    bool tracked = commands && commands == atomic_load(&unknown);
    if (tracked && atomic_load(&hold_unknown)) return DOE_VK_BUSY;
    doe_vk_status status = __real_doe_vk_commands_release(commands);
    if (tracked && status == DOE_VK_OK) {
        atomic_store(&unknown, NULL);
        atomic_fetch_add(&closed_recordings, 1);
    }
    return status;
}

void __real_native_backend_close(native_backend *);
void __wrap_native_backend_close(native_backend *backend)
{
    bool tracked = backend->kind == WGPUBackendType_Vulkan &&
        backend->value.vulkan.context == atomic_load(&observed_context);
    __real_native_backend_close(backend);
    if (tracked) atomic_fetch_add(&closed_contexts, 1);
}

enum { WORDS = 64, BYTES = WORDS * 4, INCREMENT = 19 };
typedef struct fixture {
    WGPUComputePipeline pipeline;
    WGPUBindGroup group;
    WGPUBuffer output, readback;
    WGPUTexture image;
    WGPUTextureView view;
} fixture;

static fixture create_fixture(state *s, bool image_binding)
{
    fixture f = {0};
    f.pipeline = pipeline(s, image_binding ?
        "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
        "@group(0) @binding(1) var image:texture_2d<f32>;"
        "@compute @workgroup_size(16) fn main(@builtin(global_invocation_id) i:vec3u){"
        "out[i.x]+=19u+u32(textureLoad(image,vec2i(0),0).r);}"
        : "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(16) fn main(@builtin(global_invocation_id) i:vec3u){out[i.x]+=19u;}");
    f.output = buffer(s, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    f.readback = buffer(s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    uint32_t initial[WORDS];
    for (uint32_t i = 0; i < WORDS; ++i) initial[i] = i * 7u;
    wgpuQueueWriteBuffer(s->queue, f.output, 0, initial, sizeof(initial));
    WGPUBindGroupEntry entries[2] = {{.binding=0,.buffer=f.output,.size=BYTES}};
    if (image_binding) {
        f.image = texture(s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_TextureBinding, 1);
        f.view = wgpuTextureCreateView(f.image, NULL);
        assert(f.view);
        entries[1] = (WGPUBindGroupEntry){.binding=1,.textureView=f.view};
    }
    f.group = group(s, f.pipeline, entries, image_binding ? 2 : 1);
    return f;
}

static void release_fixture(fixture *f)
{
    if (f->view) wgpuTextureViewRelease(f->view);
    if (f->image) wgpuTextureRelease(f->image);
    wgpuBindGroupRelease(f->group);
    wgpuComputePipelineRelease(f->pipeline);
    wgpuBufferRelease(f->output);
    wgpuBufferRelease(f->readback);
}

static WGPUCommandBuffer record(state *s, fixture *f)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    dispatch(encoder, f->pipeline, f->group);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, f->output, 0, f->readback, 0, BYTES);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    assert(commands);
    return commands;
}

static void expect_output(state *s, fixture *f, uint32_t runs)
{
    const uint32_t *data = map(s, f->readback, BYTES);
    for (uint32_t i = 0; i < WORDS; ++i) assert(data[i] == i * 7u + runs * INCREMENT);
    wgpuBufferUnmap(f->readback);
}

typedef struct expected_error { WGPUErrorType type; const char *detail; unsigned count; bool exact; } expected_error;
static void failed_preparation(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    expected_error *expected = userdata;
    assert(status == WGPUPopErrorScopeStatus_Success && type == expected->type && message.length);
    if (expected->detail) {
        char bounded[NATIVE_FAILURE_MESSAGE_CAPACITY];
        assert(message.length < sizeof(bounded));
        memcpy(bounded, message.data, message.length);
        bounded[message.length] = 0;
        if (expected->exact) assert(!strcmp(bounded, expected->detail));
        else assert(strstr(bounded, expected->detail));
    }
    ++expected->count;
}

static void bookkeeping_failure(state *s, unsigned allocation)
{
    fixture f = create_fixture(s, false);
    WGPUCommandBuffer commands[2] = {record(s, &f), record(s, &f)};
    count_command_pools(s, true);
    expected_error expected = {.type=WGPUErrorType_OutOfMemory,
        .detail="submission ownership allocation failed", .exact=true};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_OutOfMemory);
    atomic_store(&submits, 0);
    atomic_store(&queue_allocation_faults, 0);
    queue_allocation_calls = 0;
    queue_allocation_countdown = allocation;
    wgpuQueueSubmit(s->queue, 2, commands);
    assert(!queue_allocation_countdown && queue_allocation_calls == allocation);
    assert(atomic_load(&queue_allocation_faults) == 1);
    WGPUPopErrorScopeCallbackInfo popped = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=failed_preparation,.userdata1=&expected};
    assert(wgpuDevicePopErrorScope(s->device, popped).id && expected.count == 1 && !s->errors);
    for (size_t i = 0; i < 2; ++i) {
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        wgpuQueueSubmit(s->queue, 1, &commands[i]);
        expect_validation(s);
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        assert(!doeNativeComputeProgramPrepareHostReplay(s->queue, commands[i]));
        expect_validation(s);
        wgpuCommandBufferRelease(commands[i]);
    }
    assert(!atomic_load(&submits) && !atomic_load(&command_pool_allocations));
    count_command_pools(s, false);
    release_fixture(&f);
}

typedef struct bookkeeping_release {
    state *owner;
    fixture *resources;
    WGPUCommandBuffer commands[2];
    unsigned errors;
} bookkeeping_release;

static void release_in_bookkeeping_error(WGPUDevice const *device, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)device; (void)unused;
    static const char detail[] = "submission ownership allocation failed";
    bookkeeping_release *release = userdata;
    state *s = release->owner;
    assert(type == WGPUErrorType_OutOfMemory && !release->errors++);
    assert(message.length == sizeof(detail) - 1 && !memcmp(message.data, detail, sizeof(detail) - 1));
    assert(!atomic_load(&submits) && !atomic_load(&command_pool_allocations));
    /* Invalidation must precede foreign error delivery, so reentrant program
     * admission cannot acquire a reservation from either fresh batch member. */
    for (size_t i = 0; i < 2; ++i) {
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        assert(!doeNativeComputeProgramPrepareHostReplay(s->queue, release->commands[i]));
        expect_validation(s);
        wgpuCommandBufferRelease(release->commands[i]);
        release->commands[i] = NULL;
    }
    count_command_pools(s, false);
    release_fixture(release->resources);
    wgpuQueueRelease(s->queue);
    s->queue = NULL;
    wgpuDeviceRelease(s->device);
    s->device = NULL;
    /* Full-call/error leases protect the device even after this callback drops
     * every public device, queue, command and dependent resource reference. */
    assert(!atomic_load(&closed_contexts));
}

static void reentrant_bookkeeping_release(void)
{
    state s = {0};
    consumer_init(&s);
    fixture f = create_fixture(&s, false);
    bookkeeping_release release = {.owner=&s,.resources=&f,
        .commands={record(&s, &f), record(&s, &f)}};
    native_device *device = (native_device *)s.device;
    native_lock(device);
    device->uncaptured = (WGPUUncapturedErrorCallbackInfo){
        .callback=release_in_bookkeeping_error,.userdata1=&release};
    atomic_store(&observed_context, device->backend.value.vulkan.context);
    native_unlock(device);
    count_command_pools(&s, true);
    atomic_store(&closed_contexts, 0);
    atomic_store(&submits, 0);
    atomic_store(&queue_allocation_faults, 0);
    queue_allocation_calls = 0;
    queue_allocation_countdown = 2;
    wgpuQueueSubmit(s.queue, 2, release.commands);
    assert(!queue_allocation_countdown && queue_allocation_calls == 2);
    assert(atomic_load(&queue_allocation_faults) == 1 && release.errors == 1);
    assert(!s.queue && !s.device && !release.commands[0] && !release.commands[1] && !s.errors);
    assert(!atomic_load(&submits) && !atomic_load(&command_pool_allocations));
    for (unsigned i = 0; i < 500 && !atomic_load(&closed_contexts); ++i) {
        struct timespec delay = {.tv_sec=0,.tv_nsec=10000000};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
    }
    assert(atomic_load(&closed_contexts) == 1);
    atomic_store(&observed_context, NULL);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
}

static void preparation_fallback(state *s, preparation_fault selected, bool host_replay, bool image_binding)
{
    fixture f = create_fixture(s, image_binding);
    WGPUCommandBuffer commands = record(s, &f);
    expected_error expected = {.type=image_binding ? WGPUErrorType_Validation : WGPUErrorType_OutOfMemory,
        .detail=selected == FAULT_ALLOCATION ? "vkCreateCommandPool (native=-1)" :
            selected == FAULT_SEAL ? "vkEndCommandBuffer (native=-1)" : NULL};
    wgpuDevicePushErrorScope(s->device, image_binding ? WGPUErrorFilter_Validation : WGPUErrorFilter_OutOfMemory);
    atomic_store(&submits, 0);
    atomic_store(&faults, 0);
    fault = selected;
    assert(!doeNativeComputeProgramPrepare(s->queue, commands));
    fault = FAULT_NONE;
    assert(!atomic_load(&submits)); /* Preparation never dispatches work, including failure. */
    assert(atomic_load(&faults) == (image_binding ? 0u : 1u));
    WGPUPopErrorScopeCallbackInfo popped = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=failed_preparation,.userdata1=&expected};
    assert(wgpuDevicePopErrorScope(s->device, popped).id && expected.count == 1 && !s->errors);
    if (host_replay) {
        void *program = doeNativeComputeProgramPrepareHostReplay(s->queue, commands);
        assert(program && !atomic_load(&submits));
        assert(doeNativeComputeProgramSubmit(program) == 1);
        expect_output(s, &f, 1);
        assert(doeNativeComputeProgramSubmit(program) == 1);
        expect_output(s, &f, 2);
        doeNativeComputeProgramRelease(program);
        /* Publication is permanent even after the last program lease drops. */
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        wgpuQueueSubmit(s->queue, 1, &commands);
        expect_validation(s);
    } else {
        wgpuQueueSubmit(s->queue, 1, &commands);
        assert(!s->errors);
        expect_output(s, &f, 1);
    }
    wgpuCommandBufferRelease(commands);
    release_fixture(&f);
}

typedef struct callback_fallback { state *state; WGPUCommandBuffer commands; void *program; unsigned errors; } callback_fallback;
static void fallback_in_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)device; (void)message; (void)unused;
    callback_fallback *fallback = userdata;
    assert(type == WGPUErrorType_OutOfMemory && !fallback->errors++);
    assert(!atomic_load(&submits));
    fault = FAULT_NONE;
    fallback->program = doeNativeComputeProgramPrepareHostReplay(fallback->state->queue, fallback->commands);
    assert(fallback->program); /* Abort preceded external error delivery. */
    wgpuCommandBufferRelease(fallback->commands);
    fallback->commands = NULL;
}

static void reentrant_fallback(state *s)
{
    fixture f = create_fixture(s, false);
    callback_fallback fallback = {.state=s,.commands=record(s, &f)};
    native_device *device = (native_device *)s->device;
    native_lock(device);
    WGPUUncapturedErrorCallbackInfo saved = device->uncaptured;
    device->uncaptured = (WGPUUncapturedErrorCallbackInfo){.callback=fallback_in_error,.userdata1=&fallback};
    native_unlock(device);
    fault = FAULT_SEAL;
    atomic_store(&submits, 0);
    assert(!doeNativeComputeProgramPrepare(s->queue, fallback.commands));
    assert(fallback.errors == 1 && !fallback.commands && fallback.program);
    native_lock(device);
    device->uncaptured = saved;
    native_unlock(device);
    assert(doeNativeComputeProgramSubmit(fallback.program) == 1);
    expect_output(s, &f, 1);
    doeNativeComputeProgramRelease(fallback.program);
    release_fixture(&f);
}

static void pending_program_release(void)
{
    state s = {0};
    consumer_init(&s);
    fixture f = create_fixture(&s, false);
    WGPUCommandBuffer commands = record(&s, &f);
    void *program = doeNativeComputeProgramPrepare(s.queue, commands);
    assert(program);
    wgpuCommandBufferRelease(commands);
    atomic_store(&observed_context, ((native_device *)s.device)->backend.value.vulkan.context);
    atomic_store(&closed_contexts, 0);
    atomic_store(&closed_recordings, 0);
    atomic_store(&hold_unknown, true);
    atomic_store(&arm_unknown, true);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Internal);
    assert(!doeNativeComputeProgramSubmit(program) && atomic_load(&unknown));
    expected_error expected = {.type=WGPUErrorType_Internal};
    WGPUPopErrorScopeCallbackInfo popped = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=failed_preparation,.userdata1=&expected};
    assert(wgpuDevicePopErrorScope(s.device, popped).id && expected.count == 1);
    doeNativeComputeProgramRelease(program); /* Pending replay retains its own native/resource holders. */
    release_fixture(&f);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    assert(!atomic_load(&closed_contexts) && !atomic_load(&closed_recordings));
    atomic_store(&hold_unknown, false);
    for (unsigned i = 0; i < 500 && !atomic_load(&closed_contexts); ++i) {
        struct timespec delay = {.tv_sec=0,.tv_nsec=10000000};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
    }
    assert(atomic_load(&closed_contexts) == 1 && atomic_load(&closed_recordings) == 1 && !atomic_load(&unknown));
    atomic_store(&observed_context, NULL);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    for (int selected = FAULT_ALLOCATION; selected <= FAULT_SEAL; ++selected)
        for (unsigned host = 0; host < 2; ++host)
            preparation_fallback(&s, (preparation_fault)selected, host != 0, false);
    preparation_fallback(&s, FAULT_NONE, true, true); /* Backend seal rejects texture state; host replay admits it. */
    preparation_fallback(&s, FAULT_NONE, false, true);
    reentrant_fallback(&s);
    bookkeeping_failure(&s, 1);
    bookkeeping_failure(&s, 2);
    consumer_finish(&s);
    reentrant_bookkeeping_release();
    pending_program_release();
    printf("PASS program preparation: real Vulkan allocation/record/seal faults restore same-command ordinary/host fallback, "
        "no preparation submissions, texture-binding seal rejection, permanent published ownership, reentrant error fallback "
        "first/second submit bookkeeping OOM consumes fresh batches with reentrant release-all, "
        "and unknown replay release-all retention %s\n", adapter_class_scope());
}
