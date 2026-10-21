// Windows D3D12 clearBuffer parity: ordered zeroing, exact ranges and recovery.
// zig cc -I vendor/webgpu-headers tests/d3d12_clear_readback.c -o clear.exe
// clear.exe <absolute path to webgpu_doe_full.dll> <mode>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "webgpu.h"

static void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter value,
                          WGPUStringView message, void *result, void *unused) {
    (void)message; (void)unused;
    if (status == WGPURequestAdapterStatus_Success) *(WGPUAdapter *)result = value;
}

static void device_ready(WGPURequestDeviceStatus status, WGPUDevice value,
                         WGPUStringView message, void *result, void *unused) {
    (void)message; (void)unused;
    if (status == WGPURequestDeviceStatus_Success) *(WGPUDevice *)result = value;
}

static void map_ready(WGPUMapAsyncStatus status, WGPUStringView message,
                     void *result, void *unused) {
    (void)message; (void)unused;
    *(int *)result = status == WGPUMapAsyncStatus_Success ? 1 : -1;
}

static void scope_ready(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                        WGPUStringView message, void *result, void *unused) {
    (void)message; (void)unused;
    *(int *)result = status == WGPUPopErrorScopeStatus_Success ? (int)type : -2;
}

#define LOAD(Name) \
    WGPUProc##Name Name = (WGPUProc##Name)GetProcAddress(library, "wgpu" #Name); \
    if (!Name) { fprintf(stderr, "missing wgpu%s\n", #Name); FreeLibrary(library); return 2; }

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    const char *mode = argc == 3 ? argv[2] : "full";
    const int via_default = strcmp(mode, "readback") != 0;
    const int range = strcmp(mode, "range") == 0;
    const int size_error = strcmp(mode, "size") == 0;
    const int zero_range = strcmp(mode, "zero-range") == 0;
    const int recovery = strcmp(mode, "recover") == 0 || range || size_error || zero_range;
    const int destroyed = strcmp(mode, "destroyed") == 0;
    const int partial = strcmp(mode, "partial") == 0;
    const int zero = strcmp(mode, "zero") == 0;
    const int whole = strcmp(mode, "whole") == 0;
    const size_t words = strcmp(mode, "large") == 0 ? 32776 : 8;
    const size_t bytes = words * sizeof(uint32_t);
    HMODULE library = LoadLibraryA(argv[1]);
    if (!library) { fprintf(stderr, "LoadLibrary error=%lu\n", GetLastError()); return 2; }
    LOAD(CreateInstance); LOAD(InstanceRequestAdapter); LOAD(InstanceProcessEvents);
    LOAD(AdapterGetInfo); LOAD(AdapterInfoFreeMembers); LOAD(AdapterRequestDevice);
    LOAD(DeviceGetQueue); LOAD(DeviceCreateBuffer); LOAD(BufferGetMappedRange);
    LOAD(BufferUnmap); LOAD(DeviceCreateCommandEncoder); LOAD(CommandEncoderCopyBufferToBuffer);
    LOAD(CommandEncoderFinish); LOAD(QueueSubmit); LOAD(BufferMapAsync);
    LOAD(BufferGetConstMappedRange); LOAD(CommandBufferRelease); LOAD(CommandEncoderRelease);
    LOAD(BufferRelease); LOAD(QueueRelease); LOAD(DeviceRelease); LOAD(AdapterRelease); LOAD(InstanceRelease);
    LOAD(BufferDestroy); LOAD(DeviceDestroy); LOAD(CommandEncoderClearBuffer); LOAD(DevicePushErrorScope); LOAD(DevicePopErrorScope);
    uint32_t *expected = NULL;
    int result = 1, mapped = 0;
    const char *stage = "instance";
    WGPUInstance instance = CreateInstance(NULL);
    WGPUAdapter adapter = NULL;
    WGPUDevice device = NULL;
    WGPUQueue queue = NULL;
    WGPUBuffer source = NULL, destination = NULL, intermediate = NULL;
    WGPUCommandEncoder encoder = NULL;
    WGPUCommandBuffer commands = NULL;
    if (!instance) goto cleanup;
    stage = "D3D12 adapter";
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_D3D12;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    ac.mode = WGPUCallbackMode_AllowSpontaneous; ac.callback = adapter_ready; ac.userdata1 = &adapter;
    InstanceRequestAdapter(instance, &options, ac);
    for (int i = 0; i < 20 && !adapter; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (!adapter) goto cleanup;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (AdapterGetInfo(adapter, &info) != WGPUStatus_Success) goto cleanup;
    const int admitted = info.backendType == WGPUBackendType_D3D12 && info.vendorID != 0 &&
                         info.deviceID != 0 && info.adapterType != WGPUAdapterType_CPU;
    printf("backend=%u vendor=0x%04x device=0x%04x type=%u name=%.*s\n", info.backendType,
           info.vendorID, info.deviceID, info.adapterType, (int)info.device.length, info.device.data);
    fflush(stdout);
    AdapterInfoFreeMembers(info);
    if (!admitted) goto cleanup;
    stage = "device";
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dc.mode = WGPUCallbackMode_AllowSpontaneous; dc.callback = device_ready; dc.userdata1 = &device;
    AdapterRequestDevice(adapter, &dd, dc);
    for (int i = 0; i < 20 && !device; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (!device) goto cleanup;
    queue = DeviceGetQueue(device);
    if (!queue) goto cleanup;
    expected = malloc(bytes);
    if (!expected) goto cleanup;
    for (size_t i = 0; i < words; ++i) expected[i] = 0xa5000000u ^ (uint32_t)(i * 7919u + 17u);
    stage = "buffers";
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.size = bytes; bd.usage = WGPUBufferUsage_CopySrc; bd.mappedAtCreation = WGPU_TRUE;
    source = DeviceCreateBuffer(device, &bd);
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst; bd.mappedAtCreation = WGPU_FALSE;
    destination = DeviceCreateBuffer(device, &bd);
    if (!source || !destination) goto cleanup;
    if (via_default) {
        bd.size = bytes + 32; bd.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        intermediate = DeviceCreateBuffer(device, &bd);
        if (!intermediate) goto cleanup;
    }
    void *input = BufferGetMappedRange(source, 0, bytes);
    if (!input) goto cleanup;
    memcpy(input, expected, bytes);
    BufferUnmap(source);
    stage = "recorded copy";
    encoder = DeviceCreateCommandEncoder(device, NULL);
    if (!encoder) goto cleanup;
    if (via_default) {
        CommandEncoderCopyBufferToBuffer(encoder, source, 0, intermediate, 16, bytes);
        CommandEncoderCopyBufferToBuffer(encoder, intermediate, 16, destination, 0, bytes);
    } else CommandEncoderCopyBufferToBuffer(encoder, source, 0, destination, 0, bytes);
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    stage = "submit/map";
    QueueSubmit(queue, 1, &commands);
    // Submission owns the native resources until completion; application and
    // recorded-command references may be released before mapping the result.
    CommandBufferRelease(commands); commands = NULL;
    CommandEncoderRelease(encoder); encoder = NULL;
    BufferRelease(source); source = NULL;

    WGPUBufferMapCallbackInfo mc = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mc.mode = WGPUCallbackMode_AllowSpontaneous; mc.callback = map_ready; mc.userdata1 = &mapped;
    BufferMapAsync(destination, WGPUMapMode_Read, 0, bytes, mc);
    for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (mapped != 1) goto cleanup;
    stage = "exact readback";
    const uint32_t *output = BufferGetConstMappedRange(destination, 0, bytes);
    if (!output) goto cleanup;
    for (size_t i = 0; i < words; ++i) {
        if (output[i] != expected[i]) {
            fprintf(stderr, "word[%zu]: expected=0x%08x actual=0x%08x\n", i, expected[i], output[i]);
            goto cleanup;
        }

    }
    BufferUnmap(destination); mapped = 0;
    for (int round = 0; round < (recovery ? 2 : 1); ++round) {
        stage = "clear submission and error scope";
        const int reject = recovery && round == 0;
        int scope = -1;
        DevicePushErrorScope(device, (destroyed || reject) ? WGPUErrorFilter_Validation : WGPUErrorFilter_Internal);
        encoder = DeviceCreateCommandEncoder(device, NULL);
        if (!encoder) goto cleanup;
        WGPUBuffer target = via_default ? intermediate : destination;
        const uint64_t start = partial || whole ? 8 : 0;
        const uint64_t length = zero ? 0 : partial ? 16 : whole ? WGPU_WHOLE_SIZE : bytes;
        if (reject) CommandEncoderClearBuffer(encoder, destination, 0, bytes);
        const uint64_t invalid_offset = reject ? (range || zero_range ? bytes + 64 : size_error ? 0 : 1) : 0;
        const uint64_t clear_length = reject ? (zero_range ? 0 : size_error ? 3 : range ? 4 : length) : length;
        CommandEncoderClearBuffer(encoder, target, (via_default ? 16 : 0) + start + invalid_offset, clear_length);
        if (via_default) CommandEncoderCopyBufferToBuffer(encoder, intermediate, 16, destination, 0, bytes);
        commands = CommandEncoderFinish(encoder, NULL);
        if (!commands) goto cleanup;
        if (destroyed) BufferDestroy(intermediate);
        QueueSubmit(queue, 1, &commands);
        WGPUPopErrorScopeCallbackInfo sc = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        sc.mode = WGPUCallbackMode_AllowSpontaneous; sc.callback = scope_ready; sc.userdata1 = &scope;
        DevicePopErrorScope(device, sc);
        for (int i = 0; i < 100 && scope == -1; ++i) { InstanceProcessEvents(instance); Sleep(10); }
        int expected_scope = destroyed ? WGPUErrorType_Validation : reject ? WGPUErrorType_Validation : WGPUErrorType_NoError;
        if (scope != expected_scope) { fprintf(stderr, "scope expected=%d actual=%d\n", expected_scope, scope); goto cleanup; }
        CommandBufferRelease(commands); commands = NULL;
        CommandEncoderRelease(encoder); encoder = NULL;
        // On the last round only submission retention keeps the native clear target alive.
        if (intermediate && (!recovery || round == 1)) { BufferRelease(intermediate); intermediate = NULL; }
        mapped = 0;
        BufferMapAsync(destination, WGPUMapMode_Read, 0, bytes, mc);
        for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
        if (mapped != 1) goto cleanup;
        output = BufferGetConstMappedRange(destination, 0, bytes);
        if (!output) goto cleanup;
        stage = "clear independent output oracle";
        for (size_t i = 0; i < words; ++i) {
            const int cleared = !reject && !destroyed && !zero && i * 4 >= start && (whole || i * 4 < start + length);
            uint32_t want = cleared ? 0 : expected[i];
            if (output[i] != want) { fprintf(stderr, "word[%zu] expected=%08x actual=%08x\n", i, want, output[i]); goto cleanup; }
        }
        BufferUnmap(destination); mapped = 0;
    }
    DeviceDestroy(device);
    result = 0;
cleanup:
    free(expected);
    if (mapped == 1) BufferUnmap(destination);
    if (commands) CommandBufferRelease(commands);
    if (encoder) CommandEncoderRelease(encoder);
    if (source) BufferRelease(source);
    if (destination) BufferRelease(destination);
    if (intermediate) BufferRelease(intermediate);
    if (queue) QueueRelease(queue);
    if (device) DeviceRelease(device);
    if (adapter) AdapterRelease(adapter);
    if (instance) InstanceRelease(instance);
    FreeLibrary(library);
    if (result) fprintf(stderr, "FAIL stage=%s\n", stage);
    else printf("PASS: D3D12 clear mode=%s words=%zu, exact oracle, early release, device teardown\n", mode, words);
    return result;
}
