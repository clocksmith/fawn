/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include "../../backends/vulkan/tests/adapter_identity_fixture.h"

/* The real native bridge calls the real Vulkan selection owner. Only native
 * enumeration/property and logical-device allocation boundaries are modeled.
 * No device execution, public callback or lifetime qualification follows. */
static unsigned live_contexts, closed_contexts;
static bool wrong_identity, wrong_class, no_fragment_stores;
static bool actual_contexts;
static unsigned fresh_instances, shared_instances;
static const doe_vk_context *last_share_source;

doe_vk_status __real_doe_vk_context_create(const doe_vk_options *, doe_vk_context **, doe_vk_error *);
void __real_doe_vk_context_release(doe_vk_context *);
doe_vk_status __wrap_doe_vk_context_create(const doe_vk_options *, doe_vk_context **, doe_vk_error *);
void __wrap_doe_vk_context_release(doe_vk_context *);

doe_vk_status __wrap_doe_vk_context_create(const doe_vk_options *options,
    doe_vk_context **out, doe_vk_error *error)
{
    if (actual_contexts) return __real_doe_vk_context_create(options, out, error);
    if (options->share_instance) {
        ++shared_instances;
        last_share_source = options->share_instance;
    } else {
        ++fresh_instances;
    }
    doe_vk_context *value = malloc(sizeof(*value));
    assert(value);
    *value = context();
    doe_vk_status status = doe_vk_choose_device(value, options, error);
    if (status != DOE_VK_OK) {
        free(value);
        return status;
    }
    value->caps.surface_platforms = options->surface_platforms;
    value->caps.fragment_stores_and_atomics = !no_fragment_stores;
    if (wrong_identity) ++value->caps.identity.driver_uuid[0];
    if (wrong_class) value->caps.software_adapter = !value->caps.software_adapter;
    *out = value;
    ++live_contexts;
    return DOE_VK_OK;
}
void __wrap_doe_vk_context_release(doe_vk_context *value)
{
    if (actual_contexts) {
        __real_doe_vk_context_release(value);
        return;
    }
    if (!value) return;
    assert(live_contexts);
    --live_contexts;
    ++closed_contexts;
    free(value);
}
static WGPUAdapter requested;
static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)message; (void)userdata1; (void)userdata2;
    assert(status == WGPURequestAdapterStatus_Success && adapter);
    requested = adapter;
}
static WGPUAdapter request(WGPUInstance instance)
{
    WGPURequestAdapterOptions options = {.backendType = WGPUBackendType_Vulkan};
    WGPURequestAdapterCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = on_adapter};
    WGPUFutureWaitInfo wait = {wgpuInstanceRequestAdapter(instance, &options, callback), false};
    requested = NULL;
    assert(wgpuInstanceWaitAny(instance, 1, &wait, 0) == WGPUWaitStatus_Success && wait.completed);
    return requested;
}
/* Public adapter requests on one instance: the first opens a VkInstance and a
 * private source that borrows it; later requests borrow the source's instance
 * and still run selection. The source closes with the instance. */
static void instance_adapter_source(void)
{
    available = 3;
    order[0] = 2; order[1] = 0; order[2] = 1;
    unsigned fresh = fresh_instances, shared = shared_instances;
    WGPUInstance instance = wgpuCreateInstance(NULL);
    assert(instance);
    WGPUAdapter first = request(instance);
    assert(fresh_instances == fresh + 1 && shared_instances == shared + 1 && live_contexts == 2);
    const doe_vk_context *source = last_share_source;
    assert(source == ((native_adapter *)first)->backend.value.vulkan.context);
    wgpuAdapterRelease(first);
    WGPUAdapter second = request(instance);
    assert(fresh_instances == fresh + 1 && shared_instances == shared + 2 && live_contexts == 2);
    assert(last_share_source != source && ((native_adapter *)second)->backend.value.vulkan.caps.adapter_ordinal == 1);
    wgpuAdapterRelease(second);
    assert(live_contexts == 1);
    wgpuInstanceRelease(instance);
    assert(live_contexts == 0);
    available = 3;
    order[0] = 0; order[1] = 1; order[2] = 2;
}
static void unchanged_failure(native_backend *adapter, native_result expected)
{
    native_backend result, saved;
    memset(&result, 0xa5, sizeof(result));
    memcpy(&saved, &result, sizeof(saved));
    unsigned live = live_contexts;
    assert(native_backend_device(adapter, &result) == expected);
    assert(memcmp(&result, &saved, sizeof(result)) == 0 && live_contexts == live);
}
int main(int argc, char **argv)
{
    assert(argc == 1 || (argc == 2 && strcmp(argv[1], "--software-context") == 0));
    native_backend adapter, device;
    order[0] = 2; /* Nonfallback skips a CPU at ordinal zero. */
    order[1] = 0;
    order[2] = 1;
    assert(native_backend_open(WGPUBackendType_Vulkan, false, DOE_VK_SURFACE_XCB, &adapter) == NATIVE_OK);
    assert(adapter.value.vulkan.options.adapter_selection == DOE_VK_SELECT_HARDWARE_ELSE_SOFTWARE);
    assert(adapter.value.vulkan.options.allow_cpu_adapter && adapter.value.vulkan.options.adapter_only);
    assert(!adapter.value.vulkan.caps.software_adapter && adapter.value.vulkan.caps.adapter_ordinal == 1);
    assert(adapter.value.vulkan.options.surface_platforms == DOE_VK_SURFACE_XCB);
    doe_vk_adapter_identity selected = adapter.value.vulkan.caps.identity;
    generation = 1;
    order[0] = 1;
    order[1] = 0;
    order[2] = 2;
    assert(native_backend_device(&adapter, &device) == NATIVE_OK);
    assert(device.value.vulkan.options.adapter_selection == DOE_VK_SELECT_IDENTITY);
    assert(!device.value.vulkan.options.adapter_only);
    assert(device.value.vulkan.options.surface_platforms == DOE_VK_SURFACE_XCB);
    assert(device.value.vulkan.caps.adapter_ordinal == 1);
    assert(memcmp(&device.value.vulkan.caps.identity, &selected, sizeof(selected)) == 0);
    native_backend_close(&device);
    unsigned closed = closed_contexts;
    wrong_identity = true;
    unchanged_failure(&adapter, NATIVE_UNAVAILABLE);
    assert(closed_contexts == closed + 1);
    wrong_identity = false;
    available = 2;
    order[1] = 2;
    unchanged_failure(&adapter, NATIVE_UNSUPPORTED);
    native_backend_close(&adapter);
    assert(live_contexts == 0);

    available = 3;
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;
    assert(native_backend_open(WGPUBackendType_Vulkan, true, 0, &adapter) == NATIVE_OK);
    assert(adapter.value.vulkan.options.adapter_selection == DOE_VK_SELECT_SOFTWARE);
    assert(adapter.value.vulkan.caps.software_adapter && adapter.value.vulkan.caps.adapter_ordinal == 2);
    order[0] = 2;
    order[2] = 0;
    generation = 0;
    assert(native_backend_device(&adapter, &device) == NATIVE_OK);
    assert(device.value.vulkan.options.adapter_selection == DOE_VK_SELECT_IDENTITY);
    assert(device.value.vulkan.caps.software_adapter && device.value.vulkan.caps.adapter_ordinal == 0);
    native_backend_close(&device);
    wrong_class = true;
    unchanged_failure(&adapter, NATIVE_UNAVAILABLE);
    wrong_class = false;
    available = 2;
    order[0] = 0;
    unchanged_failure(&adapter, NATIVE_UNSUPPORTED);
    native_backend unavailable, saved;
    memset(&unavailable, 0xa5, sizeof(unavailable));
    memcpy(&saved, &unavailable, sizeof(saved));
    assert(native_backend_open(WGPUBackendType_Vulkan, true, 0, &unavailable) == NATIVE_UNSUPPORTED);
    assert(memcmp(&unavailable, &saved, sizeof(unavailable)) == 0);
    native_backend_close(&adapter);
    assert(live_contexts == 0);
    /* A device without fragmentStoresAndAtomics is refused and its context closed. */
    available = 3;
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;
    no_fragment_stores = true;
    closed = closed_contexts;
    assert(native_backend_open(WGPUBackendType_Vulkan, true, 0, &unavailable) == NATIVE_UNAVAILABLE);
    assert(memcmp(&unavailable, &saved, sizeof(unavailable)) == 0);
    assert(live_contexts == 0 && closed_contexts == closed + 1);
    no_fragment_stores = false;
    /* A default request on a host whose only device is a CPU implementation
     * selects it from the one adapter context, without a failed attempt. */
    available = 1;
    order[0] = 2;
    closed = closed_contexts;
    assert(native_backend_open(WGPUBackendType_Vulkan, false, 0, &adapter) == NATIVE_OK);
    assert(adapter.value.vulkan.caps.software_adapter && closed_contexts == closed && live_contexts == 1);
    native_backend_close(&adapter);
    assert(live_contexts == 0);
    available = 3;
    order[0] = 0;
    instance_adapter_source();
    puts("PASS native Vulkan identity bridge: changed order/handles, explicit CPU fallback, missing identity and mismatch rollback, "
        "refused adapter without fragmentStoresAndAtomics, default CPU-only selection in one adapter-only context, "
        "later public adapter requests borrowing one instance-owned source closed with the instance");
    if (argc == 2) {
        actual_contexts = true;
        assert(native_backend_open(WGPUBackendType_Vulkan, true, 0, &adapter) == NATIVE_OK);
        assert(adapter.value.vulkan.caps.software_adapter);
        assert(native_backend_device(&adapter, &device) == NATIVE_OK);
        assert(device.value.vulkan.caps.software_adapter);
        assert(memcmp(&device.value.vulkan.caps.identity, &adapter.value.vulkan.caps.identity,
            sizeof(adapter.value.vulkan.caps.identity)) == 0);
        printf("PASS actual Vulkan context identity chain and adapter-to-device creation: %s; vendor=%u device=%u driver=%u software=%u; no dispatch or physical qualification\n",
            device.value.vulkan.caps.device_name, device.value.vulkan.caps.vendor_id,
            device.value.vulkan.caps.device_id, device.value.vulkan.caps.driver_version,
            device.value.vulkan.caps.software_adapter ? 1u : 0u);
        native_backend_close(&device);
        native_backend_close(&adapter);
    }
    return 0;
}
