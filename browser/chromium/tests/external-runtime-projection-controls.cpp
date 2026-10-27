// Copyright 2026 The Doe Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/dawn/native/webgpu/InnerRuntimeDescriptor.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using dawn::native::webgpu::InnerRuntimeDescriptorChain;
using dawn::native::webgpu::InnerRuntimeRequiredFeatures;
using dawn::native::webgpu::ApplyExternalDeviceCachePolicy;
using dawn::native::webgpu::DisablePersistentCacheProc;

static std::vector<unsigned> policyCalls;
static WGPUStatus policyStatus = WGPUStatus_Success;
static WGPUStatus disableCache(WGPUDevice device) {
    assert(device);
    policyCalls.push_back(1);
    return policyStatus;
}
static void destroyDevice(WGPUDevice device) { assert(device); policyCalls.push_back(2); }
static void releaseDevice(WGPUDevice device) { assert(device); policyCalls.push_back(3); }
static void processEvents(WGPUInstance instance) { assert(instance); policyCalls.push_back(4); }

static void lost(WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {}
static void error(WGPUDevice const*, WGPUErrorType, WGPUStringView, void*, void*) {}
struct AdapterReply { unsigned calls = 0; WGPURequestAdapterStatus status{}; WGPUAdapter adapter{}; };
static void adapterReply(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                         WGPUStringView, void* userdata, void*) {
    auto* result = static_cast<AdapterReply*>(userdata);
    ++result->calls;
    result->status = status;
    result->adapter = adapter;
}

template <typename T>
static T load(void* image, const char* name) {
#if defined(_WIN32)
    auto address = GetProcAddress(static_cast<HMODULE>(image), name);
#else
    auto address = dlsym(image, name);
#endif
    assert(address);
    T result;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 3);
#if defined(_WIN32)
    void* image = LoadLibraryA(argv[1]);
#else
    void* image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!image) std::fprintf(stderr, "%s\n", dlerror());
#endif
    assert(image);
#if defined(_WIN32)
    auto cacheSymbol = GetProcAddress(static_cast<HMODULE>(image), "doeNativeDeviceDisablePersistentCache");
#else
    auto cacheSymbol = dlsym(image, "doeNativeDeviceDisablePersistentCache");
#endif
    if (std::strcmp(argv[2], "present") == 0) {
        assert(cacheSymbol);
        auto actualPolicy = load<DisablePersistentCacheProc>(image, "doeNativeDeviceDisablePersistentCache");
        assert(actualPolicy(nullptr) == WGPUStatus_Error);
        auto publicLookup = load<WGPUProcGetProcAddress>(image, "wgpuGetProcAddress");
        constexpr char privateName[] = "doeNativeDeviceDisablePersistentCache";
        assert(!publicLookup({privateName, sizeof(privateName) - 1}));
    } else {
        assert(std::strcmp(argv[2], "absent") == 0 && !cacheSymbol);
    }
    WGPUDevice privateDevice = reinterpret_cast<WGPUDevice>(uintptr_t{0x1234});
    const auto privateInstance = reinterpret_cast<WGPUInstance>(uintptr_t{0x5678});
    assert(ApplyExternalDeviceCachePolicy(privateDevice, privateInstance, disableCache,
                                         destroyDevice, releaseDevice, processEvents));
    assert(privateDevice && policyCalls == std::vector<unsigned>{1});
    policyCalls.clear(); policyStatus = WGPUStatus_Error;
    assert(!ApplyExternalDeviceCachePolicy(privateDevice, privateInstance, disableCache,
                                          destroyDevice, releaseDevice, processEvents));
    assert(!privateDevice && (policyCalls == std::vector<unsigned>{1, 2, 3, 4}));
    policyCalls.clear(); privateDevice = reinterpret_cast<WGPUDevice>(uintptr_t{0x1234});
    assert(!ApplyExternalDeviceCachePolicy(privateDevice, privateInstance, nullptr,
                                          destroyDevice, releaseDevice, processEvents));
    assert(!privateDevice && (policyCalls == std::vector<unsigned>{2, 3, 4}));
    policyCalls.clear();
    assert(!ApplyExternalDeviceCachePolicy(privateDevice, privateInstance, disableCache,
                                          destroyDevice, releaseDevice, processEvents));
    assert(!privateDevice && policyCalls == std::vector<unsigned>{4});
    auto create = load<WGPUProcCreateInstance>(image, "wgpuCreateInstance");
    auto release = load<WGPUProcInstanceRelease>(image, "wgpuInstanceRelease");
    auto getFeatures = load<WGPUProcGetInstanceFeatures>(image, "wgpuGetInstanceFeatures");
    auto getLimits = load<WGPUProcGetInstanceLimits>(image, "wgpuGetInstanceLimits");
    auto freeFeatures = load<WGPUProcSupportedInstanceFeaturesFreeMembers>(
        image, "wgpuSupportedInstanceFeaturesFreeMembers");
    auto requestAdapter = load<WGPUProcInstanceRequestAdapter>(image, "wgpuInstanceRequestAdapter");
    auto waitAny = load<WGPUProcInstanceWaitAny>(image, "wgpuInstanceWaitAny");

    WGPUDawnTogglesDescriptor toggles = WGPU_DAWN_TOGGLES_DESCRIPTOR_INIT;
    const char* disabled[] = {"allow_unsafe_apis"};
    toggles.disabledToggleCount = 1;
    toggles.disabledToggles = disabled;
    assert(!toggles.chain.next);
    assert(InnerRuntimeDescriptorChain(&toggles, &toggles.chain, false) == &toggles.chain);
    assert(!InnerRuntimeDescriptorChain(&toggles, &toggles.chain, true));
    assert(!InnerRuntimeDescriptorChain(nullptr, nullptr, false));

    // The actual upstream-header/actual C instance boundary: no GPU creation.
    WGPUSupportedInstanceFeatures features{};
    WGPUInstanceLimits limits{};
    getFeatures(&features);
    getLimits(&limits);
    WGPUInstanceDescriptor instance = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instance.requiredFeatureCount = features.featureCount;
    instance.requiredFeatures = features.features;
    instance.requiredLimits = &limits;
    instance.nextInChain = InnerRuntimeDescriptorChain(&toggles, &toggles.chain, false);
    assert(!create(&instance));  // C still rejects Dawn-private chains.
    instance.nextInChain = InnerRuntimeDescriptorChain(&toggles, &toggles.chain, true);
    assert(instance.requiredFeatureCount == features.featureCount);
    assert(instance.requiredFeatures == features.features && instance.requiredLimits == &limits);
    WGPUInstance actual = create(&instance);
    assert(actual);
    release(actual);

    WGPUChainedStruct unknown{nullptr, static_cast<WGPUSType>(0x6ffffff0)};
    toggles.chain.next = &unknown;
    instance.nextInChain = InnerRuntimeDescriptorChain(&toggles, &toggles.chain, true);
    assert(instance.nextInChain == &unknown && toggles.chain.next == &unknown);
    assert(!create(&instance));  // Required unknown tail survives projection.
    WGPUDawnCacheDeviceDescriptor cache = WGPU_DAWN_CACHE_DEVICE_DESCRIPTOR_INIT;
    cache.isolationKey = {"origin-isolation", 16};
    cache.chain.next = &toggles.chain;
    assert(InnerRuntimeDescriptorChain(&toggles, &cache.chain, true) == &unknown);
    assert(cache.chain.next == &toggles.chain && cache.isolationKey.length == 16);
    instance.nextInChain = nullptr;
    ++limits.timedWaitAnyMaxCount;
    assert(!create(&instance));  // The advertised limit was not dropped.
    --limits.timedWaitAnyMaxCount;
    const WGPUInstanceFeatureName unsupported[] = {WGPUInstanceFeatureName_ShaderSourceSPIRV};
    instance.requiredFeatureCount = 1;
    instance.requiredFeatures = unsupported;
    assert(!create(&instance));  // An unsupported public feature still rejects.

    WGPURequestAdapterWebGPUBackendOptions route = WGPU_REQUEST_ADAPTER_WEBGPU_BACKEND_OPTIONS_INIT;
    route.chain.next = &toggles.chain;
    WGPURequestAdapterOptions adapter = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter.featureLevel = WGPUFeatureLevel_Core;
    adapter.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter.forceFallbackAdapter = WGPU_TRUE;
    adapter.backendType = WGPUBackendType_D3D12;
    adapter.compatibleSurface = reinterpret_cast<WGPUSurface>(uintptr_t{0x1234});
    adapter.nextInChain = InnerRuntimeDescriptorChain(&toggles, &route.chain, true);
    assert(adapter.nextInChain == &unknown);
    assert(route.chain.next == &toggles.chain && toggles.chain.next == &unknown);
    assert(adapter.featureLevel == WGPUFeatureLevel_Core);
    assert(adapter.powerPreference == WGPUPowerPreference_HighPerformance);
    assert(adapter.forceFallbackAdapter == WGPU_TRUE && adapter.backendType == WGPUBackendType_D3D12);
    assert(adapter.compatibleSurface == reinterpret_cast<WGPUSurface>(uintptr_t{0x1234}));
    unknown.next = &route.chain;
    assert(InnerRuntimeDescriptorChain(&toggles, &unknown, true) == &unknown);
    unknown.next = nullptr;
    WGPUInstance callbackInstance = create(nullptr);
    assert(callbackInstance);
    AdapterReply reply;
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = adapterReply;
    callback.userdata1 = &reply;
    WGPUFutureWaitInfo completion = WGPU_FUTURE_WAIT_INFO_INIT;
    completion.future = requestAdapter(callbackInstance, &adapter, callback);
    assert(completion.future.id && !reply.calls);
    assert(waitAny(callbackInstance, 1, &completion, 1000000000) == WGPUWaitStatus_Success);
    assert(completion.completed && reply.calls == 1 && !reply.adapter);
    assert(reply.status != WGPURequestAdapterStatus_Success);
    release(callbackInstance);  // Invalid identity chain rejects before opening any backend.

    const WGPUFeatureName required[] = {WGPUFeatureName_TimestampQuery, WGPUFeatureName_ShaderF16};
    std::vector<WGPUFeatureName> projectedStorage;
    const auto coreFeatures = std::span<const WGPUFeatureName>(required);
    auto projectedFeatures = InnerRuntimeRequiredFeatures(coreFeatures, true, projectedStorage);
    assert(projectedFeatures.data() == required && projectedFeatures.size() == 2);
    const WGPUFeatureName mixed[] = {WGPUFeatureName_TimestampQuery,
        WGPUFeatureName_DawnInternalUsages, static_cast<WGPUFeatureName>(0x6ffffff0),
        WGPUFeatureName_ShaderF16};
    const auto mixedFeatures = std::span<const WGPUFeatureName>(mixed);
    auto nativeFeatures = InnerRuntimeRequiredFeatures(mixedFeatures, false, projectedStorage);
    assert(nativeFeatures.data() == mixed && nativeFeatures.size() == 4);
    auto externalFeatures = InnerRuntimeRequiredFeatures(mixedFeatures, true, projectedStorage);
    assert(externalFeatures.size() == 3);
    assert(externalFeatures[0] == mixed[0] && externalFeatures[1] == mixed[2] && externalFeatures[2] == mixed[3]);
    assert(mixed[1] == WGPUFeatureName_DawnInternalUsages);
    WGPULimits deviceLimits = WGPU_LIMITS_INIT;
    WGPUDeviceDescriptor device = WGPU_DEVICE_DESCRIPTOR_INIT;
    device.label = {"device", 6};
    device.requiredFeatureCount = 2;
    device.requiredFeatures = required;
    device.requiredLimits = &deviceLimits;
    device.defaultQueue.label = {"queue", 5};
    device.deviceLostCallbackInfo = {nullptr, WGPUCallbackMode_AllowProcessEvents, lost, &device, &adapter};
    device.uncapturedErrorCallbackInfo = {nullptr, error, &adapter, &device};
    device.nextInChain = InnerRuntimeDescriptorChain(&toggles, &toggles.chain, true);
    assert(device.nextInChain == &unknown);
    assert(device.label.length == 6 && device.label.data[0] == 'd');
    assert(device.requiredFeatureCount == 2 && device.requiredFeatures == required);
    assert(device.requiredFeatures[0] == WGPUFeatureName_TimestampQuery);
    assert(device.requiredFeatures[1] == WGPUFeatureName_ShaderF16);
    assert(device.requiredLimits == &deviceLimits && device.defaultQueue.label.length == 5);
    assert(device.deviceLostCallbackInfo.callback == lost);
    assert(device.deviceLostCallbackInfo.mode == WGPUCallbackMode_AllowProcessEvents);
    assert(device.deviceLostCallbackInfo.userdata1 == &device && device.deviceLostCallbackInfo.userdata2 == &adapter);
    assert(device.uncapturedErrorCallbackInfo.callback == error);
    assert(device.uncapturedErrorCallbackInfo.userdata1 == &adapter && device.uncapturedErrorCallbackInfo.userdata2 == &device);
    freeFeatures(features);
    std::puts("PASS external/native descriptor projection; core spans/limits/handles/callbacks and unsupported chain requirements retained; CPU only");
    return 0;
}
