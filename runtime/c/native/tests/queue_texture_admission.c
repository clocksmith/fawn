/* SPDX-License-Identifier: MIT
 * Host caller-composition control: real resource owners, no native GPU device.
 * Staging construction is the injected boundary; no transfer is emulated. */
#include "../src/internal.h"
#include "../../resources/tests/fixture.h"

static native_device *active_device;
static unsigned errors, staging_calls, submits;
static WGPUErrorType error_type;
static bool check_staging_size;
static doe_resource_texture **release_during_staging;
static native_token *released_token;

void __wrap_native_error(native_device *device, WGPUErrorType type, const char *message)
{
    assert(device == active_device && message && *message);
    ++errors;
    error_type = type;
}

WGPUBuffer __wrap_native_device_create_buffer(native_device *device,
    const WGPUBufferDescriptor *descriptor, uint64_t bound)
{
    assert(device == active_device && descriptor && bound == 4096);
    assert(descriptor->usage == (WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst));
    ++staging_calls;
    if (check_staging_size)
        assert(descriptor->size == (device->backend.kind == WGPUBackendType_D3D12 ? 256u : 4u));
    if (release_during_staging) {
        doe_resource_texture_release(*release_during_staging);
        *release_during_staging = NULL;
        /* The in-call texture lease protects unlocked staging and its errors. */
        assert(released_token && !released_token->releases);
    }
    __wrap_native_error(device, WGPUErrorType_OutOfMemory, "injected staging allocation failure");
    return NULL;
}

void __wrap_wgpuQueueSubmit(WGPUQueue queue, size_t count, const WGPUCommandBuffer *commands)
{
    (void)queue;
    (void)count;
    (void)commands;
    ++submits;
    assert(false); /* Empty/invalid writes and failed staging must never submit. */
}

static bool host_alive(void *identity) { return !((native_device *)identity)->destroyed; }
static void release_texture(void *userdata, void *handle)
{
    (void)userdata;
    native_token *token = handle;
    assert(!token->releases);
    ++token->releases;
}

static doe_resource_texture *host_texture(native_device *device, native_token *token, uint64_t usage)
{
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *result = NULL;
    desc.width = desc.height = 1;
    desc.usage = usage;
    assert(doe_resource_texture_create(&device->resources, &desc, &limits,
        (doe_resource_native){token,NULL,release_texture}, &result) == DOE_RESOURCE_OK);
    return result;
}

static void check_write(fixture *f, doe_resource_texture *texture, const WGPUExtent3D *extent,
    WGPUErrorType expected)
{
    const unsigned previous_errors = errors, previous_staging = staging_calls;
    const unsigned references = atomic_load(&active_device->references);
    const size_t allocations = f->allocations, live = f->live;
    WGPUTexelCopyTextureInfo destination = {.texture=(WGPUTexture)texture};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    const uint32_t data = 0x12345678;
    const bool nonempty = extent->width && extent->height && extent->depthOrArrayLayers;
    check_staging_size = expected == WGPUErrorType_OutOfMemory;
    wgpuQueueWriteTexture((WGPUQueue)active_device, &destination, nonempty ? &data : NULL,
        nonempty ? sizeof(data) : 0, &layout, extent);
    assert(errors == previous_errors + (expected != WGPUErrorType_NoError));
    if (expected != WGPUErrorType_NoError) assert(error_type == expected);
    assert(staging_calls == previous_staging + (expected == WGPUErrorType_OutOfMemory));
    assert(!submits && f->allocations == allocations && f->live == live);
    assert(atomic_load(&active_device->references) == references);
}

static void backend_case(WGPUBackendType backend)
{
    fixture f = fresh();
    native_device device = {0};
    atomic_init(&device.references, 1);
    assert(doe_mutex_init(&device.mutex, true).status == DOE_HOST_OK);
    device.backend.kind = backend;
    if (backend == WGPUBackendType_Vulkan) device.backend.value.vulkan.caps.max_memory_allocation_size = 4096;
    else device.backend.value.d3d12.caps.limits.max_buffer_size = 4096;
    device.resources = config(&f);
    device.resources.device = (doe_resource_device){&device,host_alive,native_device_retain,native_device_release};
    active_device = &device;
    native_token valid_token = {0}, sampled_token = {0};
    doe_resource_texture *valid = host_texture(&device, &valid_token, WGPUTextureUsage_CopyDst);
    doe_resource_texture *sampled = host_texture(&device, &sampled_token, WGPUTextureUsage_TextureBinding);
    const WGPUExtent3D zero[] = {{0,1,1},{1,0,1},{1,1,0}}, nonempty = {1,1,1};
    f.fail_at = f.allocations; /* Queue admission and empty writes must not allocate. */
    for (size_t i = 0; i < sizeof(zero) / sizeof(zero[0]); ++i) {
        check_write(&f, sampled, &zero[i], WGPUErrorType_Validation);
        check_write(&f, valid, &zero[i], WGPUErrorType_NoError);
    }
    check_write(&f, sampled, &nonempty, WGPUErrorType_Validation);
    check_write(&f, valid, &nonempty, WGPUErrorType_OutOfMemory);
    doe_resource_texture_destroy(sampled);
    check_write(&f, sampled, &zero[0], WGPUErrorType_Validation);
    doe_resource_texture_release(sampled);
    assert(sampled_token.releases == 1);
    /* Reentrant caller release during a nonempty staging failure must defer
     * native finalization until this queue write retires its own texture lease. */
    release_during_staging = &valid;
    released_token = &valid_token;
    unsigned previous_errors = errors, previous_staging = staging_calls;
    WGPUTexelCopyTextureInfo destination = {.texture=(WGPUTexture)valid};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    const uint32_t data = 7;
    wgpuQueueWriteTexture((WGPUQueue)&device, &destination, &data, sizeof(data), &layout, &nonempty);
    assert(!valid && valid_token.releases == 1 && errors == previous_errors + 1 &&
        error_type == WGPUErrorType_OutOfMemory && staging_calls == previous_staging + 1 && !submits);
    release_during_staging = NULL;
    released_token = NULL;
    assert(atomic_load(&device.references) == 1);
    assert(doe_mutex_destroy(&device.mutex).status == DOE_HOST_OK);
    empty(&f);
}

int main(int argc, char **argv)
{
    if (argc == 1 || !strcmp(argv[1], "vulkan")) backend_case(WGPUBackendType_Vulkan);
    if (argc == 1 || !strcmp(argv[1], "d3d12")) backend_case(WGPUBackendType_D3D12);
    puts("PASS Vulkan/D3D12 queue texture admission before staging, allocation-free empty writes and reentrant lifetime; host control only");
    return 0;
}
