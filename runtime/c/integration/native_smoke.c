/* SPDX-License-Identifier: MIT
 * Real public WebGPU DSO consumer, C11 and C++17. No simulated execution.
 * no-adapter: explicit loader rejection only. hardware: physical Vulkan copy.
 */
#define _POSIX_C_SOURCE 200809L
#include "webgpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
typedef HMODULE library_handle;
static library_handle library_open(const char *path) { return LoadLibraryA(path); }
static void library_close(library_handle library) { FreeLibrary(library); }
static void pause_events(void) { Sleep(1); }
#else
#include <dlfcn.h>
#include <time.h>
typedef void *library_handle;
static library_handle library_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void library_close(library_handle library) { (void)dlclose(library); }
static void pause_events(void)
{
    struct timespec delay = {0, 1000000};
    (void)nanosleep(&delay, NULL);
}
#endif

static int load(library_handle library, const char *name, void *out, size_t size)
{
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(library, name);
#else
    void *symbol = dlsym(library, name);
#endif
    if (symbol == NULL || size != sizeof(symbol)) {
        fprintf(stderr, "missing symbol or unsupported function-pointer ABI: %s\n", name);
        return 0;
    }
    memcpy(out, &symbol, size);
    return 1;
}

typedef struct adapter_result {
    WGPUAdapter adapter;
    WGPURequestAdapterStatus status;
    unsigned calls;
    int native_probe_error;
} adapter_result;
typedef struct device_result {
    WGPUDevice device;
    WGPURequestDeviceStatus status;
    unsigned calls;
} device_result;
typedef struct map_result {
    WGPUMapAsyncStatus status;
    unsigned calls;
} map_result;

static void print_message(WGPUStringView message)
{
    size_t length = message.length;
    if (message.data == NULL) return;
    if (length == WGPU_STRLEN) length = strlen(message.data);
    if (length > 4096) length = 4096;
    (void)fwrite(message.data, 1, length, stderr);
    fputc('\n', stderr);
}
static void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter value,
                          WGPUStringView message, void *userdata, void *unused)
{
    adapter_result *result = (adapter_result *)userdata;
    (void)unused;
    result->status = status;
    result->adapter = value;
    result->calls++;
    result->native_probe_error = message.data != NULL &&
        message.length == sizeof("vulkan physical adapter probe failed") - 1 &&
        memcmp(message.data, "vulkan physical adapter probe failed", message.length) == 0;
    if (status != WGPURequestAdapterStatus_Success) print_message(message);
}
static void device_ready(WGPURequestDeviceStatus status, WGPUDevice value,
                         WGPUStringView message, void *userdata, void *unused)
{
    device_result *result = (device_result *)userdata;
    (void)unused;
    result->status = status;
    result->device = value;
    result->calls++;
    if (status != WGPURequestDeviceStatus_Success) print_message(message);
}
static void map_ready(WGPUMapAsyncStatus status, WGPUStringView message,
                      void *userdata, void *unused)
{
    map_result *result = (map_result *)userdata;
    (void)unused;
    result->status = status;
    result->calls++;
    if (status != WGPUMapAsyncStatus_Success) print_message(message);
}

/* Function names/types have one public owner: the pinned WebGPU header. */
#define DECLARE(Name) WGPUProc##Name Name = NULL
#define LOAD(Name) do { \
    if (!load(library, "wgpu" #Name, &Name, sizeof(Name))) goto cleanup; \
} while (0)

int main(int argc, char **argv)
{
    library_handle library = NULL;
    int result = 1;
    int expect_unavailable = 0;
    const char *stage = "arguments";
    WGPUInstance instance = NULL;
    WGPUQueue queue = NULL;
    WGPUBuffer source = NULL, destination = NULL;
    WGPUCommandEncoder encoder = NULL;
    WGPUCommandBuffer commands = NULL;
    adapter_result adapter = {NULL, WGPURequestAdapterStatus_Error, 0, 0};
    device_result device = {NULL, WGPURequestDeviceStatus_Error, 0};
    map_result mapped = {WGPUMapAsyncStatus_Error, 0};
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    WGPUBufferMapCallbackInfo mc = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    WGPUStringView proc_name = {"wgpuCreateInstance", WGPU_STRLEN};
    WGPUProc generic_proc = NULL;
    uint32_t expected[16];
    void *input = NULL;
    const void *output = NULL;
    const char *backend = getenv("DOE_BACKEND");
    const char *lane = getenv("FAWN_BACKEND_LANE");
    DECLARE(GetProcAddress); DECLARE(CreateInstance); DECLARE(InstanceRelease);
    DECLARE(InstanceRequestAdapter); DECLARE(InstanceProcessEvents);
    DECLARE(AdapterGetInfo); DECLARE(AdapterInfoFreeMembers); DECLARE(AdapterRequestDevice);
    DECLARE(AdapterRelease); DECLARE(DeviceGetQueue); DECLARE(DeviceCreateBuffer);
    DECLARE(DeviceCreateCommandEncoder); DECLARE(DeviceDestroy); DECLARE(DeviceRelease);
    DECLARE(BufferGetMappedRange); DECLARE(BufferGetConstMappedRange); DECLARE(BufferUnmap);
    DECLARE(BufferMapAsync); DECLARE(BufferRelease); DECLARE(BufferSetLabel);
    DECLARE(CommandEncoderCopyBufferToBuffer); DECLARE(CommandEncoderFinish);
    DECLARE(CommandEncoderRelease); DECLARE(CommandBufferRelease);
    DECLARE(QueueSubmit); DECLARE(QueueRelease);
    if (argc != 3 || (strcmp(argv[2], "no-adapter") != 0 && strcmp(argv[2], "hardware") != 0)) {
        fprintf(stderr, "usage: native_smoke ABSOLUTE_DSO_PATH no-adapter|hardware\n");
        return 2;
    }
    if (backend == NULL || strcmp(backend, "vulkan") != 0) {
        fputs("DOE_BACKEND must explicitly equal vulkan before this process starts\n", stderr);
        return 2;
    }
    if (lane == NULL || strcmp(lane, "vulkan_doe_app") != 0) {
        fputs("FAWN_BACKEND_LANE must explicitly equal vulkan_doe_app to select Doe ownership\n", stderr);
        return 2;
    }
    expect_unavailable = strcmp(argv[2], "no-adapter") == 0;
    stage = "load";
    library = library_open(argv[1]);
    if (library == NULL) goto cleanup;
    LOAD(GetProcAddress); LOAD(CreateInstance); LOAD(InstanceRelease);
    LOAD(InstanceRequestAdapter); LOAD(InstanceProcessEvents); LOAD(AdapterRelease);
    LOAD(AdapterGetInfo); LOAD(AdapterInfoFreeMembers); LOAD(AdapterRequestDevice);
    LOAD(DeviceGetQueue); LOAD(DeviceCreateBuffer); LOAD(DeviceCreateCommandEncoder);
    LOAD(DeviceDestroy); LOAD(DeviceRelease); LOAD(BufferGetMappedRange);
    LOAD(BufferGetConstMappedRange); LOAD(BufferUnmap); LOAD(BufferMapAsync);
    LOAD(BufferRelease); LOAD(BufferSetLabel); LOAD(CommandEncoderCopyBufferToBuffer);
    LOAD(CommandEncoderFinish); LOAD(CommandEncoderRelease); LOAD(CommandBufferRelease);
    LOAD(QueueSubmit); LOAD(QueueRelease);
    stage = "public procedure identity";
    generic_proc = GetProcAddress(proc_name);
    if (generic_proc == NULL || sizeof(generic_proc) != sizeof(CreateInstance) ||
        memcmp(&generic_proc, &CreateInstance, sizeof(generic_proc)) != 0) goto cleanup;
    stage = "instance";
    instance = CreateInstance(NULL);
    if (instance == NULL) goto cleanup;
    stage = "Vulkan adapter callback";
    options.backendType = WGPUBackendType_Vulkan;
    ac.mode = WGPUCallbackMode_AllowProcessEvents;
    ac.callback = adapter_ready;
    ac.userdata1 = &adapter;
    (void)InstanceRequestAdapter(instance, &options, ac);
    for (unsigned i = 0; i < 5000 && adapter.calls == 0; ++i) {
        InstanceProcessEvents(instance);
        pause_events();
    }
    if (adapter.calls == 0) {
        fputs("FAIL: adapter completion unknown; no explicit teardown\n", stderr);
        return 3;
    }
    if (adapter.calls != 1) goto cleanup;
    if (expect_unavailable) {
        /* The retained Vulkan probe reports Error, unlike Metal's Unavailable. */
        if (adapter.status != WGPURequestAdapterStatus_Error || adapter.adapter != NULL ||
            !adapter.native_probe_error) goto cleanup;
        puts("PASS: real Vulkan loader/no-adapter callback; no device or GPU execution claimed");
        result = 0;
        goto cleanup;
    }
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == NULL) goto cleanup;
    stage = "physical Vulkan identity";
    if (AdapterGetInfo(adapter.adapter, &info) != WGPUStatus_Success) goto cleanup;
    printf("backend=%u type=%u vendor=0x%04x device=0x%04x\n",
           (unsigned)info.backendType, (unsigned)info.adapterType, info.vendorID, info.deviceID);
    if (info.backendType != WGPUBackendType_Vulkan || info.adapterType == WGPUAdapterType_CPU ||
        info.adapterType == WGPUAdapterType_Unknown || info.vendorID == 0 || info.deviceID == 0) {
        AdapterInfoFreeMembers(info);
        goto cleanup;
    }
    AdapterInfoFreeMembers(info);
    stage = "device";
    dc.mode = WGPUCallbackMode_AllowProcessEvents;
    dc.callback = device_ready;
    dc.userdata1 = &device;
    (void)AdapterRequestDevice(adapter.adapter, &dd, dc);
    for (unsigned i = 0; i < 5000 && device.calls == 0; ++i) {
        InstanceProcessEvents(instance);
        pause_events();
    }
    if (device.calls == 0) {
        fputs("FAIL: device completion unknown; no explicit teardown\n", stderr);
        return 3;
    }
    if (device.calls != 1 || device.status != WGPURequestDeviceStatus_Success || device.device == NULL) goto cleanup;
    queue = DeviceGetQueue(device.device);
    if (queue == NULL) goto cleanup;
    stage = "buffer allocation";
    bd.size = sizeof(expected);
    bd.usage = WGPUBufferUsage_CopySrc;
    bd.mappedAtCreation = WGPU_TRUE;
    source = DeviceCreateBuffer(device.device, &bd);
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    bd.mappedAtCreation = WGPU_FALSE;
    destination = DeviceCreateBuffer(device.device, &bd);
    if (source == NULL || destination == NULL) goto cleanup;
    proc_name.data = "native integration copied label";
    BufferSetLabel(source, proc_name);
    for (size_t i = 0; i < 16; ++i) expected[i] = UINT32_C(0xa5000000) ^ (uint32_t)(i * 7919 + 17);
    input = BufferGetMappedRange(source, 0, sizeof(expected));
    if (input == NULL) goto cleanup;
    memcpy(input, expected, sizeof(expected));
    BufferUnmap(source);
    stage = "encode and submit";
    encoder = DeviceCreateCommandEncoder(device.device, NULL);
    if (encoder == NULL) goto cleanup;
    CommandEncoderCopyBufferToBuffer(encoder, source, 0, destination, 0, sizeof(expected));
    commands = CommandEncoderFinish(encoder, NULL);
    if (commands == NULL) goto cleanup;
    QueueSubmit(queue, 1, &commands);
    /* Real submission ownership must survive release of caller command handles. */
    CommandBufferRelease(commands);
    commands = NULL;
    CommandEncoderRelease(encoder);
    encoder = NULL;
    stage = "map completion and exact readback";
    mc.mode = WGPUCallbackMode_AllowProcessEvents;
    mc.callback = map_ready;
    mc.userdata1 = &mapped;
    (void)BufferMapAsync(destination, WGPUMapMode_Read, 0, sizeof(expected), mc);
    for (unsigned i = 0; i < 5000 && mapped.calls == 0; ++i) {
        InstanceProcessEvents(instance);
        pause_events();
    }
    if (mapped.calls != 1 || mapped.status != WGPUMapAsyncStatus_Success) {
        fputs("FAIL: GPU completion unproven; no explicit resource or device teardown\n", stderr);
        return 3;
    }
    output = BufferGetConstMappedRange(destination, 0, sizeof(expected));
    if (output == NULL || memcmp(output, expected, sizeof(expected)) != 0) goto cleanup;
    BufferUnmap(destination);
    puts("PASS: physical Vulkan buffer copy and exact readback; smoke only, not integrated qualification");
    result = 0;
cleanup:
    if (result != 0) fprintf(stderr, "FAIL: %s\n", stage);
    if (commands != NULL) CommandBufferRelease(commands);
    if (encoder != NULL) CommandEncoderRelease(encoder);
    if (source != NULL) BufferRelease(source);
    if (destination != NULL) BufferRelease(destination);
    if (queue != NULL) QueueRelease(queue);
    if (device.device != NULL) { DeviceDestroy(device.device); DeviceRelease(device.device); }
    if (adapter.adapter != NULL) AdapterRelease(adapter.adapter);
    if (instance != NULL) InstanceRelease(instance);
    if (library != NULL) library_close(library);
    return result;
}
