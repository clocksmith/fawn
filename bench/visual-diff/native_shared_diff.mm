#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <dawn/webgpu.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
WGPUInstance instance;
WGPUAdapter adapter;
WGPUDevice device;
WGPUQueue queue;
WGPUComputePipeline pipeline;
WGPUBindGroupLayout layout;
WGPUBuffer maskBuffer, readback;
std::atomic_bool apiError{false};
WGPUStringView str(const char* s) { return {s, strlen(s)}; }
void errorMessage(const char* stage, WGPUStringView msg) {
    fprintf(stderr, "%s: %.*s\n", stage, int(msg.length), msg.data ? msg.data : "");
}
using Clock = std::chrono::steady_clock;
bool waitFor(std::atomic_bool& complete) {
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!complete && Clock::now() < deadline) {
        wgpuInstanceProcessEvents(instance);
        if (!complete) std::this_thread::yield();
    }
    if (!complete) fprintf(stderr, "native completion timed out\n");
    return complete && !apiError;
}
WGPUBuffer makeBuffer(WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = sizeof(uint32_t);
    desc.usage = usage;
    return wgpuDeviceCreateBuffer(device, &desc);
}
constexpr char shader[] = R"(
@group(0) @binding(0) var first: texture_2d<f32>;
@group(0) @binding(1) var second: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> mask: atomic<u32>;
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) p: vec3u) {
    let at = vec2i(p.xy);
    if any(textureLoad(first, at, 0) != textureLoad(second, at, 0)) {
        atomicOr(&mask, 1u << (p.x/64u + (p.y/64u)*8u));
    }
})";
struct Input {
    WGPUSharedTextureMemory memory = nullptr;
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    bool active = false;
    bool start(IOSurfaceRef source) {
        WGPUSharedTextureMemoryIOSurfaceDescriptor io = WGPU_SHARED_TEXTURE_MEMORY_IO_SURFACE_DESCRIPTOR_INIT;
        io.ioSurface = source;
        WGPUSharedTextureMemoryDescriptor desc = WGPU_SHARED_TEXTURE_MEMORY_DESCRIPTOR_INIT;
        desc.nextInChain = &io.chain;
        memory = wgpuDeviceImportSharedTextureMemory(device, &desc);
        WGPUSharedTextureMemoryProperties prop = WGPU_SHARED_TEXTURE_MEMORY_PROPERTIES_INIT;
        if (!memory || wgpuSharedTextureMemoryGetProperties(memory, &prop) != WGPUStatus_Success ||
            prop.size.width != 512 || prop.size.height != 256 || prop.format != WGPUTextureFormat_BGRA8Unorm) {
            fprintf(stderr, "IOSurface import failed or returned unexpected dimensions/format (%u x %u, %x)\n",
                    prop.size.width, prop.size.height, prop.format);
            return false;
        }
        WGPUTextureDescriptor tex = WGPU_TEXTURE_DESCRIPTOR_INIT;
        tex.dimension = WGPUTextureDimension_2D;
        tex.size = prop.size;
        tex.format = prop.format;
        tex.usage = WGPUTextureUsage_TextureBinding;
        texture = wgpuSharedTextureMemoryCreateTexture(memory, &tex);
        WGPUSharedTextureMemoryBeginAccessDescriptor begin = WGPU_SHARED_TEXTURE_MEMORY_BEGIN_ACCESS_DESCRIPTOR_INIT;
        begin.initialized = true;  // ScreenCaptureKit completed and retains both independent frames.
        if (!texture || wgpuSharedTextureMemoryBeginAccess(memory, texture, &begin) != WGPUStatus_Success)
            return false;
        active = true;
        view = wgpuTextureCreateView(texture, nullptr);
        return view && !apiError;
    }
    bool finish() {
        if (!active) return false;
        active = false;
        WGPUSharedTextureMemoryEndAccessState end = WGPU_SHARED_TEXTURE_MEMORY_END_ACCESS_STATE_INIT;
        bool ok = wgpuSharedTextureMemoryEndAccess(memory, texture, &end) == WGPUStatus_Success &&
                  end.fenceCount > 0 && end.signaledValueCount == end.fenceCount;
        for (size_t i = 0; ok && i < end.fenceCount; ++i) {
            WGPUSharedFenceMTLSharedEventExportInfo metal = WGPU_SHARED_FENCE_MTL_SHARED_EVENT_EXPORT_INFO_INIT;
            WGPUSharedFenceExportInfo exp = WGPU_SHARED_FENCE_EXPORT_INFO_INIT;
            exp.nextInChain = &metal.chain;
            wgpuSharedFenceExportInfo(end.fences[i], &exp);
            id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)metal.sharedEvent;
            if (!event || exp.type != WGPUSharedFenceType_MTLSharedEvent) { ok = false; break; }
            const auto deadline = Clock::now() + std::chrono::seconds(3);
            while (event.signaledValue < end.signaledValues[i] && Clock::now() < deadline)
                std::this_thread::yield();
            ok = event.signaledValue >= end.signaledValues[i];
        }
        if (!ok) fprintf(stderr, "EndAccess did not return a completed Metal shared event (%zu fences)\n", end.fenceCount);
        wgpuSharedTextureMemoryEndAccessStateFreeMembers(end);
        return ok;
    }
    ~Input() {
        if (active) finish();
        if (view) wgpuTextureViewRelease(view);
        if (texture) wgpuTextureRelease(texture);
        if (memory) wgpuSharedTextureMemoryRelease(memory);
    }
};
}  // namespace

extern "C" int prepare_comparison() {
    instance = wgpuCreateInstance(nullptr);
    if (!instance) return 1;
    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    ao.backendType = WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo acb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    static std::atomic_bool gotAdapter{false};
    acb.mode = WGPUCallbackMode_AllowProcessEvents;
    acb.callback = [](WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void*, void*) {
        if (s == WGPURequestAdapterStatus_Success) adapter = a; else errorMessage("adapter", m);
        gotAdapter = true;
    };
    wgpuInstanceRequestAdapter(instance, &ao, acb);
    if (!waitFor(gotAdapter) || !adapter) return 2;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) return 3;
    fprintf(stderr, "native adapter: %.*s / %.*s backend %u, type %u\n", int(info.device.length), info.device.data,
            int(info.description.length), info.description.data, info.backendType, info.adapterType);
    const bool hardware = info.backendType == WGPUBackendType_Metal &&
                          (info.adapterType == WGPUAdapterType_IntegratedGPU || info.adapterType == WGPUAdapterType_DiscreteGPU);
    wgpuAdapterInfoFreeMembers(info);
    const WGPUFeatureName features[] = {WGPUFeatureName_SharedTextureMemoryIOSurface, WGPUFeatureName_SharedFenceMTLSharedEvent};
    if (!hardware || !wgpuAdapterHasFeature(adapter, features[0]) || !wgpuAdapterHasFeature(adapter, features[1])) {
        fprintf(stderr, "physical Metal IOSurface import and shared event are required\n");
        return 4;
    }
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.requiredFeatures = features;
    dd.requiredFeatureCount = 2;
    dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView m, void*, void*) {
        apiError = true; errorMessage("WebGPU validation", m);
    };
    WGPURequestDeviceCallbackInfo dcb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    static std::atomic_bool gotDevice{false};
    dcb.mode = WGPUCallbackMode_AllowProcessEvents;
    dcb.callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void*, void*) {
        if (s == WGPURequestDeviceStatus_Success) device = d; else errorMessage("device", m);
        gotDevice = true;
    };
    wgpuAdapterRequestDevice(adapter, &dd, dcb);
    if (!waitFor(gotDevice) || !device) return 5;
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = str(shader);
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &source.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &sd);
    WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pd.compute.module = module;
    pd.compute.entryPoint = str("main");
    pipeline = wgpuDeviceCreateComputePipeline(device, &pd);
    wgpuShaderModuleRelease(module);
    if (!pipeline || apiError) return 6;
    layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    maskBuffer = makeBuffer(WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    readback = makeBuffer(WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    queue = wgpuDeviceGetQueue(device);
    return layout && maskBuffer && readback && queue && !apiError ? 0 : 7;
}

extern "C" int64_t compare_surfaces(IOSurfaceRef a, IOSurfaceRef b) {
    @autoreleasepool {
        Input first, second;
        if (!first.start(a) || !second.start(b)) return -1;
        WGPUBindGroupEntry entries[] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        entries[0].textureView = first.view;
        entries[1].binding = 1; entries[1].textureView = second.view;
        entries[2].binding = 2; entries[2].buffer = maskBuffer; entries[2].size = 4;
        WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        gd.layout = layout; gd.entryCount = 3; gd.entries = entries;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &gd);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        wgpuCommandEncoderClearBuffer(encoder, maskBuffer, 0, 4);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 64, 32, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, maskBuffer, 0, readback, 0, 4);
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue, 1, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(group);
        bool releasedFirst = first.finish(), releasedSecond = second.finish();
        if (!releasedFirst || !releasedSecond || apiError) return -2;

        static std::atomic_bool complete{false};
        static bool mapped;
        mapped = false;
        complete = false;
        WGPUBufferMapCallbackInfo cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.callback = [](WGPUMapAsyncStatus s, WGPUStringView msg, void*, void*) {
            mapped = s == WGPUMapAsyncStatus_Success;
            if (!mapped) errorMessage("mask readback", msg);
            complete = true;
        };
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, 4, cb);
        if (!waitFor(complete) || !mapped) return -3;
        const void* ptr = wgpuBufferGetConstMappedRange(readback, 0, 4);
        uint32_t mask = 0;
        if (ptr) memcpy(&mask, ptr, sizeof(mask));
        wgpuBufferUnmap(readback);
        return ptr ? int64_t(mask) : -4;
    }
}
