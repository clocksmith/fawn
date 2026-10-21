// Windows public command-buffer consumption: duplicate/reused submission must not execute.
// zig cc -I vendor/webgpu-headers tests/d3d12_command_consumption.c -o readback.exe
// readback.exe <absolute path to webgpu_doe_full.dll>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
    *(int *)result = status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Validation ? 1 : -1;
}

#define LOAD(Name) \
    WGPUProc##Name Name = (WGPUProc##Name)GetProcAddress(library, "wgpu" #Name); \
    if (!Name) { fprintf(stderr, "missing wgpu%s\n", #Name); FreeLibrary(library); return 2; }

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    const int via_default = 0;
    const int reuse = argc == 3 && strcmp(argv[2], "reuse") == 0;
    HMODULE library = LoadLibraryA(argv[1]);
    if (!library) { fprintf(stderr, "LoadLibrary error=%lu\n", GetLastError()); return 2; }
    LOAD(CreateInstance); LOAD(InstanceRequestAdapter); LOAD(InstanceProcessEvents);
    LOAD(AdapterGetInfo); LOAD(AdapterInfoFreeMembers); LOAD(AdapterRequestDevice);
    LOAD(DeviceGetQueue); LOAD(DeviceCreateBuffer); LOAD(BufferGetMappedRange);
    LOAD(BufferUnmap); LOAD(DeviceCreateCommandEncoder); LOAD(CommandEncoderCopyBufferToBuffer);
    LOAD(CommandEncoderFinish); LOAD(QueueSubmit); LOAD(BufferMapAsync);
    LOAD(BufferGetConstMappedRange); LOAD(CommandBufferRelease); LOAD(CommandEncoderRelease);
    LOAD(BufferRelease); LOAD(QueueRelease); LOAD(DeviceRelease); LOAD(AdapterRelease); LOAD(InstanceRelease);
    LOAD(CommandEncoderClearBuffer); LOAD(DevicePushErrorScope); LOAD(DevicePopErrorScope);
    int result = 1, mapped = 0;
    const char *stage = "instance";
    WGPUInstance instance = CreateInstance(NULL);
    WGPUAdapter adapter = NULL;
    WGPUDevice device = NULL;
    WGPUQueue queue = NULL;
    WGPUBuffer source = NULL, destination = NULL, intermediate = NULL;
    WGPUCommandEncoder encoder = NULL;
    WGPUCommandBuffer commands = NULL, initial = NULL;
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
    const uint32_t expected[] = {0x12345678u, 0xabcdef01u, 0u, 0xffffffffu,
                                 0x55aa55aau, 0x01020304u, 0x87654321u, 0xdeadbeefu};
    stage = "buffers";
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.size = sizeof(expected); bd.usage = WGPUBufferUsage_CopySrc; bd.mappedAtCreation = WGPU_TRUE;
    source = DeviceCreateBuffer(device, &bd);
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst; bd.mappedAtCreation = WGPU_FALSE;
    destination = DeviceCreateBuffer(device, &bd);
    if (!source || !destination) goto cleanup;
    if (via_default) {
        bd.size = 64; bd.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        intermediate = DeviceCreateBuffer(device, &bd);
        if (!intermediate) goto cleanup;
    }
    void *input = BufferGetMappedRange(source, 0, sizeof(expected));
    if (!input) goto cleanup;
    memcpy(input, expected, sizeof(expected));
    BufferUnmap(source);
    stage = "recorded copy";
    encoder = DeviceCreateCommandEncoder(device, NULL);
    if (!encoder) goto cleanup;
    if (via_default) {
        CommandEncoderCopyBufferToBuffer(encoder, source, 0, intermediate, 16, sizeof(expected));
        CommandEncoderCopyBufferToBuffer(encoder, intermediate, 16, destination, 0, sizeof(expected));
    } else CommandEncoderCopyBufferToBuffer(encoder, source, 0, destination, 0, sizeof(expected));
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    stage = "submit/map";
    QueueSubmit(queue, 1, &commands);
    // Submission owns the native resources until completion; application and
    // recorded-command references may be released before mapping the result.
    initial = commands; commands = NULL;
    CommandEncoderRelease(encoder); encoder = NULL;
    BufferRelease(source); source = NULL;
    if (intermediate) { BufferRelease(intermediate); intermediate = NULL; }
    WGPUBufferMapCallbackInfo mc = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mc.mode = WGPUCallbackMode_AllowSpontaneous; mc.callback = map_ready; mc.userdata1 = &mapped;
    BufferMapAsync(destination, WGPUMapMode_Read, 0, sizeof(expected), mc);
    for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (mapped != 1) goto cleanup;
    stage = "exact readback";
    const uint32_t *output = BufferGetConstMappedRange(destination, 0, sizeof(expected));
    if (!output) goto cleanup;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        if (output[i] != expected[i]) {
            fprintf(stderr, "word[%zu]: expected=0x%08x actual=0x%08x\n", i, expected[i], output[i]);
            goto cleanup;
        }
        printf("word[%zu]: expected=0x%08x actual=0x%08x\n", i, expected[i], output[i]);
    }
    BufferUnmap(destination); mapped = 0;
    stage = "duplicate/reused submission rejection";
    int rejected = 0;
    uint32_t wanted[8];
    memcpy(wanted, expected, sizeof(wanted));
    encoder = DeviceCreateCommandEncoder(device, NULL);
    if (!encoder) goto cleanup;
    CommandEncoderClearBuffer(encoder, destination, 0, sizeof(expected));
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    if (reuse) {
        QueueSubmit(queue, 1, &commands); // Fresh clear is admitted.
        memset(wanted, 0, sizeof(wanted));
    }
    DevicePushErrorScope(device, WGPUErrorFilter_Validation);
    if (reuse) QueueSubmit(queue, 1, &initial);
    else {
        WGPUCommandBuffer duplicates[] = {commands, commands};
        QueueSubmit(queue, 2, duplicates);
    }
    WGPUPopErrorScopeCallbackInfo sc = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    sc.mode = WGPUCallbackMode_AllowSpontaneous; sc.callback = scope_ready; sc.userdata1 = &rejected;
    DevicePopErrorScope(device, sc);
    for (int i = 0; i < 100 && !rejected; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    printf("validation_rejected=%d expected=1\n", rejected);
    BufferMapAsync(destination, WGPUMapMode_Read, 0, sizeof(expected), mc);
    for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (mapped != 1) goto cleanup;
    output = BufferGetConstMappedRange(destination, 0, sizeof(expected));
    if (!output) goto cleanup;
    int matches = 1;
    for (int i = 0; i < 8; ++i) {
        printf("after[%d]: expected=0x%08x actual=0x%08x\n", i, wanted[i], output[i]);
        if (wanted[i] != output[i]) matches = 0;
    }
    if (rejected != 1 || !matches) goto cleanup;
    BufferUnmap(destination); mapped = 0;
    // Queue remains usable with a new command buffer after rejection.
    CommandBufferRelease(commands); commands = NULL;
    CommandEncoderRelease(encoder); encoder = DeviceCreateCommandEncoder(device, NULL);
    if (!encoder) goto cleanup;
    CommandEncoderClearBuffer(encoder, destination, 0, sizeof(expected));
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    QueueSubmit(queue, 1, &commands);
    BufferMapAsync(destination, WGPUMapMode_Read, 0, sizeof(expected), mc);
    for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (mapped != 1) goto cleanup;
    output = BufferGetConstMappedRange(destination, 0, sizeof(expected));
    if (!output) goto cleanup;
    for (int i = 0; i < 8; ++i) if (output[i] != 0) goto cleanup;
    result = 0;
cleanup:
    if (mapped == 1) BufferUnmap(destination);
    if (commands) CommandBufferRelease(commands);
    if (initial) CommandBufferRelease(initial);
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
    else printf("PASS: D3D12 %s rejected without mutation; fresh command recovery passed\n", reuse ? "resubmit" : "duplicate batch");
    return result;
}
