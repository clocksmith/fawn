// Windows public ABI compute oracle: runtime arrayLength over a 32-byte allocation, bounded dispatch.
// zig cc -I vendor/webgpu-headers tests/d3d12_runtime_array_readback.c -o compute.exe
// With DXC on process PATH: compute.exe <DLL>
// Optional mode: short|rebind|fixture|float|int|repeat|reject|unsupported|recover
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
    *(int *)result = status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Internal ? 1 : -1;
}

#define LOAD(Name) \
    WGPUProc##Name Name = (WGPUProc##Name)GetProcAddress(library, "wgpu" #Name); \
    if (!Name) { fprintf(stderr, "missing wgpu%s\n", #Name); FreeLibrary(library); return 2; }

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    const int short_binding = argc == 3 && strcmp(argv[2], "short") == 0;
    const int rebind = argc == 3 && strcmp(argv[2], "rebind") == 0;
    const unsigned elements = short_binding ? 4u : 8u;
    const int floating = argc == 3 && strcmp(argv[2], "float") == 0;
    const int repeated = argc == 3 && strcmp(argv[2], "repeat") == 0;
    const int reject_dispatch = argc == 3 && strcmp(argv[2], "reject") == 0;
    const int signed_integer = argc == 3 && strcmp(argv[2], "int") == 0;
    const int unsupported = argc == 3 && strcmp(argv[2], "unsupported") == 0;
    const int fixture = argc == 3 && strcmp(argv[2], "fixture") == 0;
    const int recover = argc == 3 && strcmp(argv[2], "recover") == 0;
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
    LOAD(DeviceCreateShaderModule); LOAD(DeviceCreateComputePipeline);
    LOAD(DeviceCreateBindGroupLayout); LOAD(DeviceCreatePipelineLayout); LOAD(DeviceCreateBindGroup);
    LOAD(CommandEncoderBeginComputePass); LOAD(ComputePassEncoderSetPipeline);
    LOAD(ComputePassEncoderSetBindGroup); LOAD(ComputePassEncoderDispatchWorkgroups);
    LOAD(ComputePassEncoderEnd); LOAD(ComputePassEncoderRelease);
    LOAD(ShaderModuleRelease); LOAD(ComputePipelineRelease); LOAD(BindGroupRelease);
    LOAD(BindGroupLayoutRelease); LOAD(PipelineLayoutRelease);
    WGPUShaderModule shader = NULL;
    WGPUComputePipeline pipeline = NULL;
    WGPUBindGroupLayout bgl = NULL;
    WGPUPipelineLayout layout = NULL;
    WGPUBindGroup group = NULL, short_group = NULL;
    WGPUComputePassEncoder pass = NULL;
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
    uint32_t input_words[] = {0x12345678u, 0xabcdef01u, 0u, 0xffffffffu,
                                 0x55aa55aau, 0x01020304u, 0x87654321u, 0xdeadbeefu};
    uint32_t expected[8];
    for (unsigned i=0; i<8; ++i) expected[i] = input_words[i] * 3u + elements;
    if (repeated) for (unsigned i=0; i<8; ++i) expected[i] = expected[i] * 3u + elements;
    if (floating) {
        const float values[8] = {-3.5f,-1.0f,0.0f,0.25f,1.0f,2.0f,16.0f,1024.0f};
        float oracle[8];
        for (unsigned i=0; i<8; ++i) oracle[i] = values[i] * 1.5f + 2.0f;
        memcpy(input_words,values,sizeof(values)); memcpy(expected,oracle,sizeof(oracle));
    }
    if (reject_dispatch) memcpy(expected,input_words,sizeof(expected));
    if (signed_integer) {
        const int32_t values[8] = {-100000,-17,-1,0,1,16,128,100000};
        int32_t oracle[8];
        for (unsigned i=0; i<8; ++i) oracle[i] = values[i]*3+8;
        memcpy(input_words,values,sizeof(values)); memcpy(expected,oracle,sizeof(oracle));
    }
    if (short_binding) for(unsigned i=4;i<8;++i) expected[i]=input_words[i];
    if (rebind) for(unsigned i=0;i<4;++i) expected[i]=expected[i]*3u+4u;
    if (fixture) for(unsigned i=0;i<8;++i) expected[i]=input_words[i]+1u;
    stage = "buffers";
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.size = sizeof(expected); bd.usage = WGPUBufferUsage_CopySrc; bd.mappedAtCreation = WGPU_TRUE;
    source = DeviceCreateBuffer(device, &bd);
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst; bd.mappedAtCreation = WGPU_FALSE;
    destination = DeviceCreateBuffer(device, &bd);
    if (!source || !destination) goto cleanup;
    {
        bd.size = 32; bd.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        intermediate = DeviceCreateBuffer(device, &bd);
        if (!intermediate) goto cleanup;
    }
    void *input = BufferGetMappedRange(source, 0, sizeof(expected));
    if (!input) goto cleanup;
    memcpy(input, input_words, sizeof(input_words));
    BufferUnmap(source);
    stage = "compute pipeline";
    const char *wgsl = "@group(0) @binding(0) var<storage, read_write> data: array<u32>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) { "
        "data[id.x] = data[id.x] * 3u + arrayLength(&data); }";
    if (floating) wgsl = "@group(0) @binding(0) var<storage, read_write> data: array<f32>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) { "
        "data[id.x] = data[id.x] * 1.5 + f32(arrayLength(&data)) * 0.25; }";
    if (signed_integer) wgsl = "@group(0) @binding(0) var<storage, read_write> data: array<i32>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) { "
        "data[id.x] = data[id.x] * 3 + i32(arrayLength(&data)); }";
    const char *valid_wgsl = wgsl;
    if (unsupported || recover) wgsl = "@group(0) @binding(0) var<storage, read_write> data: array<vec2<u32>,4>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) { data[id.x] = vec2<u32>(7u,7u); }";
    // Unchanged shader from tests/native_recorded_compute.c. This harness still
    // supplies an explicit layout; it does not qualify that application's auto layout.
    if (fixture) wgsl =
        "@group(0) @binding(0) var<storage, read_write> values: array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {"
        "values[id.x] = values[id.x] + 1u; }";
    WGPUShaderSourceWGSL ws = WGPU_SHADER_SOURCE_WGSL_INIT;
    ws.code = (WGPUStringView){wgsl, WGPU_STRLEN};
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &ws.chain;
    shader = DeviceCreateShaderModule(device, &sd);
    if (!shader) goto cleanup;
    WGPUBindGroupLayoutEntry le = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    le.binding=0; le.visibility=WGPUShaderStage_Compute; le.buffer.type=WGPUBufferBindingType_Storage;
    le.buffer.minBindingSize=4;
    WGPUBindGroupLayoutDescriptor ld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    ld.entryCount=1; ld.entries=&le;
    bgl=DeviceCreateBindGroupLayout(device,&ld);
    if (!bgl) goto cleanup;
    WGPUPipelineLayoutDescriptor pd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pd.bindGroupLayoutCount=1; pd.bindGroupLayouts=&bgl;
    layout=DeviceCreatePipelineLayout(device,&pd);
    if (!layout) goto cleanup;
    WGPUComputePipelineDescriptor cp = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cp.layout=layout; cp.compute.module=shader; cp.compute.entryPoint=(WGPUStringView){"main",4};
    pipeline=DeviceCreateComputePipeline(device,&cp);
    if (recover) {
        if (pipeline) goto cleanup;
        ShaderModuleRelease(shader); shader=NULL;
        ws.code=(WGPUStringView){valid_wgsl,WGPU_STRLEN};
        shader=DeviceCreateShaderModule(device,&sd);
        if (!shader) goto cleanup;
        cp.compute.module=shader;
        pipeline=DeviceCreateComputePipeline(device,&cp);
    }
    if (unsupported) { if (!pipeline) result=0; goto cleanup; }
    if (!pipeline) goto cleanup;
    WGPUBindGroupEntry be = WGPU_BIND_GROUP_ENTRY_INIT;
    be.binding=0; be.buffer=intermediate; be.size=elements*4;
    WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    gd.layout=bgl; gd.entryCount=1; gd.entries=&be;
    group=DeviceCreateBindGroup(device,&gd);
    if (!group) goto cleanup;
    if (rebind) {
        be.size=16;
        short_group=DeviceCreateBindGroup(device,&gd);
        if (!short_group) goto cleanup;
    }
    stage = "recorded compute";
    if (reject_dispatch) {
        encoder=DeviceCreateCommandEncoder(device,NULL);
        if (!encoder) goto cleanup;
        CommandEncoderCopyBufferToBuffer(encoder,source,0,destination,0,32);
        commands=CommandEncoderFinish(encoder,NULL);
        if (!commands) goto cleanup;
        QueueSubmit(queue,1,&commands);
        CommandBufferRelease(commands); commands=NULL;
        CommandEncoderRelease(encoder); encoder=NULL;
    }
    encoder=DeviceCreateCommandEncoder(device,NULL);
    if (!encoder) goto cleanup;
    CommandEncoderCopyBufferToBuffer(encoder,source,0,intermediate,0,32);
    pass=CommandEncoderBeginComputePass(encoder,NULL);
    if (!pass) goto cleanup;
    ComputePassEncoderSetPipeline(pass,pipeline);
    ComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
    ComputePassEncoderDispatchWorkgroups(pass,reject_dispatch ? elements+1 : elements,1,1);
    if (repeated) ComputePassEncoderDispatchWorkgroups(pass,elements,1,1);
    if (rebind) {
        ComputePassEncoderSetBindGroup(pass,0,short_group,0,NULL);
        ComputePassEncoderDispatchWorkgroups(pass,4,1,1);
    }
    ComputePassEncoderEnd(pass);
    ComputePassEncoderRelease(pass); pass=NULL;
    CommandEncoderCopyBufferToBuffer(encoder,intermediate,0,destination,0,32);
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    ShaderModuleRelease(shader); shader=NULL;
    ComputePipelineRelease(pipeline); pipeline=NULL;
    BindGroupRelease(group); group=NULL;
    if(short_group) { BindGroupRelease(short_group); short_group=NULL; }
    PipelineLayoutRelease(layout); layout=NULL;
    BindGroupLayoutRelease(bgl); bgl=NULL;
    stage = "submit/map";
    if (reject_dispatch) DevicePushErrorScope(device,WGPUErrorFilter_Internal);
    QueueSubmit(queue, 1, &commands);
    if (reject_dispatch) {
        int rejected_dispatch=0;
        WGPUPopErrorScopeCallbackInfo scope = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        scope.mode=WGPUCallbackMode_AllowSpontaneous; scope.callback=scope_ready; scope.userdata1=&rejected_dispatch;
        DevicePopErrorScope(device,scope);
        for(int i=0;i<100 && !rejected_dispatch;++i) { InstanceProcessEvents(instance); Sleep(10); }
        if(rejected_dispatch != 1) goto cleanup;
    }
    // Submission owns the native resources until completion; application and
    // recorded-command references may be released before mapping the result.
    CommandBufferRelease(commands); commands = NULL;
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
    stage = "unsupported command rejection";
    int rejected = 0;
    DevicePushErrorScope(device, WGPUErrorFilter_Internal);
    encoder = DeviceCreateCommandEncoder(device, NULL);
    if (!encoder) goto cleanup;
    CommandEncoderClearBuffer(encoder, destination, 0, sizeof(expected));
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    QueueSubmit(queue, 1, &commands);
    WGPUPopErrorScopeCallbackInfo sc = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    sc.mode = WGPUCallbackMode_AllowSpontaneous; sc.callback = scope_ready; sc.userdata1 = &rejected;
    DevicePopErrorScope(device, sc);
    for (int i = 0; i < 100 && !rejected; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (rejected != 1) goto cleanup;
    BufferMapAsync(destination, WGPUMapMode_Read, 0, sizeof(expected), mc);
    for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (mapped != 1) goto cleanup;
    output = BufferGetConstMappedRange(destination, 0, sizeof(expected));
    if (!output || memcmp(output, expected, sizeof(expected)) != 0) goto cleanup;
    result = 0;
cleanup:
    if(pass) ComputePassEncoderRelease(pass);
    if(shader) ShaderModuleRelease(shader);
    if(pipeline) ComputePipelineRelease(pipeline);
    if(group) BindGroupRelease(group);
    if(short_group) BindGroupRelease(short_group);
    if(layout) PipelineLayoutRelease(layout);
    if(bgl) BindGroupLayoutRelease(bgl);
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
    else if (unsupported) printf("PASS: D3D12 aggregate array rejected at pipeline creation\n");
    else printf("PASS: D3D12 %s compute/readback, early release, exact 8-word output, unsupported clear rejected\n",
                recover ? "valid pipeline after rejected aggregate" : fixture ? "unchanged fixture shader" : short_binding ? "short runtime binding" : rebind ? "runtime descriptor rebinding" : floating ? "runtime f32" : signed_integer ? "i32" : repeated ? "repeated u32" : reject_dispatch ? "rejected dispatch preserves sentinel" : "u32");
    return result;
}
