// Windows public ABI compute oracle: runtime arrayLength over a 32-byte allocation, bounded dispatch.
// zig cc -I vendor/webgpu-headers tests/d3d12_auto_layout_readback.c -o compute.exe
// With DXC on process PATH: compute.exe <DLL>
// Modes are documented in README.md, including dense multi-buffer views and rejection/recovery.
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
    const WGPUErrorType wanted = unused ? *(WGPUErrorType *)unused : WGPUErrorType_Internal;
    *(int *)result = status == WGPUPopErrorScopeStatus_Success && type == wanted ? 1 : -1;
}

#define LOAD(Name) \
    WGPUProc##Name Name = (WGPUProc##Name)GetProcAddress(library, "wgpu" #Name); \
    if (!Name) { fprintf(stderr, "missing wgpu%s\n", #Name); FreeLibrary(library); return 2; }

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    const int multi = argc == 3 && strncmp(argv[2], "multi", 5) == 0;
    const int dynamic_layout = multi && strcmp(argv[2], "multi-dynamic-layout") == 0;
    const int dynamic_offsets = multi && strcmp(argv[2], "multi-dynamic-offsets") == 0;
    const int multi_recover = multi && strcmp(argv[2], "multi-recover") == 0;
    const int multi_sparse = multi && strcmp(argv[2], "multi-sparse") == 0;
    const int multi_offset = multi && strcmp(argv[2], "multi-offset") == 0;
    const int multi_explicit = dynamic_layout || multi_recover || (multi && strcmp(argv[2], "multi-explicit") == 0);
    const int multi_write = multi && strcmp(argv[2], "multi-write") == 0;
    const int multi_short = multi && strcmp(argv[2], "multi-short") == 0;
    const int multi_rebind = multi && strcmp(argv[2], "multi-rebind") == 0;
    const int multi_alias = multi && strcmp(argv[2], "multi-alias") == 0;
    const unsigned binding_count = multi ? (strcmp(argv[2], "multi-eight") == 0 ? 8u : 3u) : 1u;
    const int atomic_signed = argc == 3 && strcmp(argv[2], "atomic-signed") == 0;
    const int atomic = atomic_signed || (argc == 3 && strcmp(argv[2], "atomic") == 0);
    const int clear_first = argc == 3 && strcmp(argv[2], "clear") == 0;
    const int clear_after = argc == 3 && strcmp(argv[2], "clear-after") == 0;
    const int mismatch = argc == 3 && strcmp(argv[2], "mismatch") == 0;
    const int fixed = argc == 3 && strcmp(argv[2], "fixed") == 0;
    const int short_binding = argc == 3 && strcmp(argv[2], "short") == 0;
    const int rebind = argc == 3 && strcmp(argv[2], "rebind") == 0;
    const unsigned elements = short_binding || multi_short ? 4u : 8u;
    const int floating = argc == 3 && strcmp(argv[2], "float") == 0;
    const int repeated = multi_write || (argc == 3 && strcmp(argv[2], "repeat") == 0);
    const int reject_dispatch = multi_offset || multi_alias || mismatch || (argc == 3 && strcmp(argv[2], "reject") == 0);
    const int signed_integer = atomic_signed || (argc == 3 && strcmp(argv[2], "int") == 0);
    const int unsupported = multi_sparse || (argc == 3 && strcmp(argv[2], "unsupported") == 0);
    const int fixture = argc == 3 && strcmp(argv[2], "fixture") == 0;
    const int recover = dynamic_layout || multi_recover || (argc == 3 && strcmp(argv[2], "recover") == 0);
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
    LOAD(ComputePipelineGetBindGroupLayout); LOAD(DeviceCreateShaderModule); LOAD(DeviceCreateComputePipeline);
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
    WGPUBuffer inputs[7] = {0};
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
    if (atomic) for(unsigned i=0;i<8;++i) expected[i]=input_words[i]+input_words[(i+1)%8];
    if (short_binding) for(unsigned i=4;i<8;++i) expected[i]=input_words[i];
    if (rebind) for(unsigned i=0;i<4;++i) expected[i]=expected[i]*3u+4u;
    if (fixture) for(unsigned i=0;i<8;++i) expected[i]=input_words[i]+1u;
    if (clear_first || clear_after) for(unsigned i=0;i<8;++i) expected[i]=clear_first ? elements : 0;
    uint32_t input_storage[8][8];
    memcpy(input_storage[0], input_words, sizeof(input_words));
    if (multi) {
        for (unsigned j=1; j<binding_count; ++j)
            for (unsigned i=0; i<8; ++i) input_storage[j][i]=input_words[(i+j)%8] + j*101u;
        memcpy(expected,input_words,sizeof(expected));
        if (!multi_alias && !multi_offset && !dynamic_offsets) for (unsigned pass_index=0; pass_index<(multi_write || multi_rebind ? 2u : 1u); ++pass_index) {
            const unsigned lanes=multi_rebind && pass_index == 1 ? 4u : elements;
            for (unsigned i=0; i<lanes; ++i) {
                expected[i]=expected[i]*3u + 8u;
                for (unsigned j=1; j<binding_count; ++j) {
                    const unsigned bound=(multi_short || (multi_rebind && pass_index == 1)) && j == binding_count-1 ? 4u : 8u;
                    expected[i]+=(input_storage[j][i]+(multi_write ? pass_index*j : 0u))*(j+1u)+bound;
                }
            }
        }
    }
    stage = "buffers";
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.size = multi ? binding_count*32u : sizeof(expected); bd.usage = WGPUBufferUsage_CopySrc; bd.mappedAtCreation = WGPU_TRUE;
    source = DeviceCreateBuffer(device, &bd);
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst; bd.mappedAtCreation = WGPU_FALSE;
    bd.size=sizeof(expected);
    destination = DeviceCreateBuffer(device, &bd);
    if (!source || !destination) goto cleanup;
    {
        bd.size = 32; bd.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        intermediate = DeviceCreateBuffer(device, &bd);
        if (!intermediate) goto cleanup;
    }
    for(unsigned j=1;j<binding_count;++j) {
        inputs[j-1]=DeviceCreateBuffer(device,&bd);
        if(!inputs[j-1]) goto cleanup;
    }
    void *input = BufferGetMappedRange(source, 0, multi ? binding_count*32u : sizeof(expected));
    if (!input) goto cleanup;
    memcpy(input, multi ? input_storage[0] : input_words, multi ? binding_count*32u : sizeof(input_words));
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
    if (fixed) wgsl = "@group(0) @binding(0) var<storage, read_write> data: array<u32,8>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) { data[id.x] = data[id.x] * 3u + 8u; }";
    if (atomic) wgsl =
        "var<workgroup> cells: array<atomic<u32>,8>; "
        "@group(0) @binding(0) var<storage,read_write> data: array<u32>; "
        "@compute @workgroup_size(8) fn main(@builtin(local_invocation_index) tid:u32) { "
        "var index=tid; let pointer=&cells[index]; index=0u; "
        "atomicStore(pointer,data[tid]); workgroupBarrier(); "
        "data[tid]=atomicLoad(pointer)+atomicLoad(&cells[(tid+1u)%8u]); }";
    if (atomic_signed) wgsl =
        "var<workgroup> cells: array<atomic<i32>,8>; "
        "@group(0) @binding(0) var<storage,read_write> data: array<i32>; "
        "@compute @workgroup_size(8) fn main(@builtin(local_invocation_index) tid:u32) { "
        "var index=tid; let pointer=&cells[index]; index=0u; "
        "atomicStore(pointer,data[tid]); workgroupBarrier(); "
        "data[tid]=atomicLoad(pointer)+atomicLoad(&cells[(tid+1u)%8u]); }";
    const char *valid_wgsl = wgsl;
    if (unsupported || recover) wgsl = "@group(0) @binding(0) var<storage, read_write> data: array<vec2<u32>,4>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) { data[id.x] = vec2<u32>(7u,7u); }";
    // Unchanged shader from tests/native_recorded_compute.c. This harness still
    // uses automatic layout, but does not qualify the full application.
    if (fixture) wgsl =
        "@group(0) @binding(0) var<storage, read_write> values: array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {"
        "values[id.x] = values[id.x] + 1u; }";
    char multi_wgsl[4096];
    if(multi) {
        size_t used=0;
        // Reverse declaration order and distinct input values expose accidental
        // declaration-order/register-order mapping rather than only buffer count.
        for(unsigned j=binding_count-1;j>0;--j)
            used+=(size_t)snprintf(multi_wgsl+used,sizeof(multi_wgsl)-used,
                "@group(0) @binding(%u) var<storage,%s> input%u:array<u32>; ",multi_sparse && j==binding_count-1 ? 7u : j,multi_write ? "read_write" : "read",j);
        used+=(size_t)snprintf(multi_wgsl+used,sizeof(multi_wgsl)-used,
            "@group(0) @binding(0) var<storage,read_write> data:array<u32>; "
            "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>) { "
            "data[id.x]=data[id.x]*3u+arrayLength(&data)");
        for(unsigned j=1;j<binding_count;++j)
            used+=(size_t)snprintf(multi_wgsl+used,sizeof(multi_wgsl)-used,"+input%u[id.x]*%uu+arrayLength(&input%u)",j,j+1,j);
        used+=(size_t)snprintf(multi_wgsl+used,sizeof(multi_wgsl)-used,";");
        if(multi_write) for(unsigned j=1;j<binding_count;++j)
            used+=(size_t)snprintf(multi_wgsl+used,sizeof(multi_wgsl)-used,"input%u[id.x]+=%uu;",j,j);
        used+=(size_t)snprintf(multi_wgsl+used,sizeof(multi_wgsl)-used,"}");
        if(used>=sizeof(multi_wgsl)) goto cleanup;
        wgsl=multi_wgsl; valid_wgsl=multi_wgsl;
        printf("WGSL: %s\n",wgsl);
    }
    if(multi_explicit) {
        WGPUBindGroupLayoutEntry entries[8];
        for(unsigned i=0;i<binding_count;++i) {
            const unsigned j=binding_count-1-i;
            entries[i]=(WGPUBindGroupLayoutEntry)WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entries[i].binding=j; entries[i].visibility=WGPUShaderStage_Compute;
            entries[i].buffer.type=j ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
            entries[i].buffer.minBindingSize=4;
            if(dynamic_layout && j==1) entries[i].buffer.hasDynamicOffset=WGPU_TRUE;
            if(multi_recover && j==0) entries[i].buffer.type=WGPUBufferBindingType_ReadOnlyStorage;
        }
        WGPUBindGroupLayoutDescriptor desc=WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount=binding_count;desc.entries=entries;
        bgl=DeviceCreateBindGroupLayout(device,&desc);
        if(!bgl) goto cleanup;
        WGPUPipelineLayoutDescriptor pd=WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&bgl;
        layout=DeviceCreatePipelineLayout(device,&pd);
        BindGroupLayoutRelease(bgl);bgl=NULL;
        if(!layout) goto cleanup;
    }
    WGPUShaderSourceWGSL ws = WGPU_SHADER_SOURCE_WGSL_INIT;
    ws.code = (WGPUStringView){wgsl, WGPU_STRLEN};
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &ws.chain;
    shader = DeviceCreateShaderModule(device, &sd);
    if (!shader) goto cleanup;
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
        if(multi_recover || dynamic_layout) cp.layout=NULL;
        pipeline=DeviceCreateComputePipeline(device,&cp);
    }
    if (unsupported) { if (!pipeline) result=0; goto cleanup; }
    if (!pipeline) goto cleanup;
    stage = "automatic bind group layout";
    bgl=ComputePipelineGetBindGroupLayout(pipeline,0);
    if (!bgl) goto cleanup;
    WGPUBindGroupLayout again=ComputePipelineGetBindGroupLayout(pipeline,0);
    const int same_layout=again == bgl;
    if(again) BindGroupLayoutRelease(again);
    if(!same_layout) goto cleanup;
    if(mismatch) {
        WGPUComputePipeline other=DeviceCreateComputePipeline(device,&cp);
        if(!other) goto cleanup;
        WGPUBindGroupLayout other_layout=ComputePipelineGetBindGroupLayout(other,0);
        ComputePipelineRelease(other);
        if(!other_layout) goto cleanup;
        if(other_layout == bgl) { BindGroupLayoutRelease(other_layout); goto cleanup; }
        BindGroupLayoutRelease(bgl); bgl=other_layout;
    }
    WGPUBindGroupEntry be = WGPU_BIND_GROUP_ENTRY_INIT;
    be.binding=0; be.buffer=intermediate; be.size=elements*4;
    WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    gd.layout=bgl; gd.entryCount=1; gd.entries=&be;
    WGPUBindGroupEntry multi_entries[8];
    if(multi) {
        for(unsigned i=0;i<binding_count;++i) {
            const unsigned j=binding_count-1-i;
            multi_entries[i]=(WGPUBindGroupEntry)WGPU_BIND_GROUP_ENTRY_INIT;
            multi_entries[i].binding=j;
            multi_entries[i].buffer=j && !multi_alias ? inputs[j-1] : intermediate;
            multi_entries[i].size=(multi_short || multi_offset) && j==binding_count-1 ? 16 : 32;
            if(multi_offset && j==binding_count-1) multi_entries[i].offset=4;
        }
        gd.entryCount=binding_count;gd.entries=multi_entries;
    }
    group=DeviceCreateBindGroup(device,&gd);
    if (!group) goto cleanup;
    if (rebind || multi_rebind) {
        if(multi_rebind) multi_entries[0].size=16;
        be.size=16;
        short_group=DeviceCreateBindGroup(device,&gd);
        if (!short_group) goto cleanup;
    }
    stage = "recorded compute";
    if (reject_dispatch || dynamic_offsets) {
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
    for(unsigned j=1;j<binding_count;++j) CommandEncoderCopyBufferToBuffer(encoder,source,j*32u,inputs[j-1],0,32);
    if (clear_first) CommandEncoderClearBuffer(encoder,intermediate,0,32);
    pass=CommandEncoderBeginComputePass(encoder,NULL);
    if (!pass) goto cleanup;
    ComputePassEncoderSetPipeline(pass,pipeline);
    const uint32_t dynamic_offset=16;
    if(dynamic_offsets) DevicePushErrorScope(device,WGPUErrorFilter_Validation);
    ComputePassEncoderSetBindGroup(pass,0,group,dynamic_offsets ? 1 : 0,dynamic_offsets ? &dynamic_offset : NULL);
    ComputePassEncoderDispatchWorkgroups(pass,atomic ? 1 : multi_offset ? 4 : reject_dispatch && !mismatch && !multi_alias ? elements+1 : elements,1,1);
    if (repeated) ComputePassEncoderDispatchWorkgroups(pass,elements,1,1);
    if (rebind || multi_rebind) {
        ComputePassEncoderSetBindGroup(pass,0,short_group,0,NULL);
        ComputePassEncoderDispatchWorkgroups(pass,4,1,1);
    }
    ComputePassEncoderEnd(pass);
    ComputePassEncoderRelease(pass); pass=NULL;
    if (clear_after) CommandEncoderClearBuffer(encoder,intermediate,0,32);
    CommandEncoderCopyBufferToBuffer(encoder,intermediate,0,destination,0,32);
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    ShaderModuleRelease(shader); shader=NULL;
    ComputePipelineRelease(pipeline); pipeline=NULL;
    BindGroupRelease(group); group=NULL;
    if(short_group) { BindGroupRelease(short_group); short_group=NULL; }
    if(layout) { PipelineLayoutRelease(layout); layout=NULL; }
    BindGroupLayoutRelease(bgl); bgl=NULL;
    stage = "submit/map";
    if (reject_dispatch) DevicePushErrorScope(device,WGPUErrorFilter_Internal);
    QueueSubmit(queue, 1, &commands);
    int dynamic_rejected=0;
    if(dynamic_offsets) {
        WGPUPopErrorScopeCallbackInfo scope=WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        WGPUErrorType wanted=WGPUErrorType_Validation;
        scope.mode=WGPUCallbackMode_AllowSpontaneous;scope.callback=scope_ready;
        scope.userdata1=&dynamic_rejected;scope.userdata2=&wanted;
        DevicePopErrorScope(device,scope);
        for(int i=0;i<100 && !dynamic_rejected;++i) { InstanceProcessEvents(instance);Sleep(10); }
        printf("dynamic offset validation captured=%d\n",dynamic_rejected);
    }
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
    for(unsigned j=1;j<binding_count;++j) { BufferRelease(inputs[j-1]);inputs[j-1]=NULL; }
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
    if(dynamic_offsets && dynamic_rejected != 1) goto cleanup;
    BufferUnmap(destination); mapped = 0;
    stage = "invalid clear rejection";
    int rejected = 0;
    DevicePushErrorScope(device, WGPUErrorFilter_Validation);
    encoder = DeviceCreateCommandEncoder(device, NULL);
    if (!encoder) goto cleanup;
    CommandEncoderClearBuffer(encoder, destination, 1, 4);
    commands = CommandEncoderFinish(encoder, NULL);
    if (!commands) goto cleanup;
    QueueSubmit(queue, 1, &commands);
    WGPUPopErrorScopeCallbackInfo sc = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    sc.mode = WGPUCallbackMode_AllowSpontaneous; sc.callback = scope_ready; sc.userdata1 = &rejected;
    WGPUErrorType clear_error_type = WGPUErrorType_Validation;
    sc.userdata2 = &clear_error_type;
    DevicePopErrorScope(device, sc);
    for (int i = 0; i < 100 && !rejected; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (rejected != 1) goto cleanup;
    BufferMapAsync(destination, WGPUMapMode_Read, 0, sizeof(expected), mc);
    for (int i = 0; i < 100 && !mapped; ++i) { InstanceProcessEvents(instance); Sleep(10); }
    if (mapped != 1) goto cleanup;
    output = BufferGetConstMappedRange(destination, 0, sizeof(expected));
    if (!output || memcmp(output, expected, sizeof(expected)) != 0) goto cleanup;
    if(dynamic_offsets) {
        stage="fresh encoder after dynamic-offset rejection";
        BufferUnmap(destination);mapped=0;
        CommandBufferRelease(commands);commands=NULL;
        CommandEncoderRelease(encoder);encoder=NULL;
        encoder=DeviceCreateCommandEncoder(device,NULL);
        if(!encoder) goto cleanup;
        CommandEncoderClearBuffer(encoder,destination,0,sizeof(expected));
        commands=CommandEncoderFinish(encoder,NULL);
        if(!commands) goto cleanup;
        QueueSubmit(queue,1,&commands);
        BufferMapAsync(destination,WGPUMapMode_Read,0,sizeof(expected),mc);
        for(int i=0;i<100 && !mapped;++i) { InstanceProcessEvents(instance);Sleep(10); }
        if(mapped != 1) goto cleanup;
        output=BufferGetConstMappedRange(destination,0,sizeof(expected));
        if(!output) goto cleanup;
        for(unsigned i=0;i<8;++i) if(output[i] != 0) goto cleanup;
        printf("fresh encoder recovery: exact 8 zero words\n");
    }
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
    for(unsigned j=1;j<binding_count;++j) if(inputs[j-1]) BufferRelease(inputs[j-1]);
    if (destination) BufferRelease(destination);
    if (intermediate) BufferRelease(intermediate);
    if (queue) QueueRelease(queue);
    if (device) DeviceRelease(device);
    if (adapter) AdapterRelease(adapter);
    if (instance) InstanceRelease(instance);
    FreeLibrary(library);
    if (result) fprintf(stderr, "FAIL stage=%s\n", stage);
    else if (unsupported) printf("PASS: D3D12 %s rejected at pipeline creation\n", multi_sparse ? "sparse public bindings" : "aggregate array");
    else printf("PASS: D3D12 %s compute/readback, early release, exact 8-word output, misaligned clear rejected\n",
                multi ? argv[2] : atomic_signed ? "signed shared atomic index capture" : atomic ? "unsigned shared atomic index capture" : clear_first ? "clear then compute" : clear_after ? "compute then clear" : mismatch ? "foreign automatic layout rejected" : fixed ? "fixed automatic layout" : recover ? "valid pipeline after rejected aggregate" : fixture ? "unchanged fixture shader" : short_binding ? "short runtime binding" : rebind ? "runtime descriptor rebinding" : floating ? "runtime f32" : signed_integer ? "i32" : repeated ? "repeated u32" : reject_dispatch ? "rejected dispatch preserves sentinel" : "u32");
    return result;
}
