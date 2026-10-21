// Named compute entry from the unchanged package OutputLayer shader.
// Native C ABI qualification; this does not qualify the JavaScript package.
#include <windows.h>
#include <stdint.h>
#include <math.h>
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
    const int automatic = argc == 3 && strcmp(argv[2], "auto") == 0;
    const int recover = argc == 3 && strcmp(argv[2], "recover") == 0;
    const int repeated = argc == 3 && strcmp(argv[2], "repeat") == 0;
    HMODULE library = LoadLibraryA(argv[1]);
    if (!library) return 2;
    LOAD(CreateInstance); LOAD(InstanceRequestAdapter); LOAD(InstanceProcessEvents);
    LOAD(AdapterGetInfo); LOAD(AdapterInfoFreeMembers); LOAD(AdapterRequestDevice);
    LOAD(DeviceGetQueue); LOAD(DeviceCreateBuffer); LOAD(BufferGetMappedRange); LOAD(BufferUnmap);
    LOAD(DeviceCreateShaderModule); LOAD(DeviceCreateBindGroupLayout); LOAD(DeviceCreatePipelineLayout);
    LOAD(DeviceCreateComputePipeline); LOAD(ComputePipelineGetBindGroupLayout); LOAD(DeviceCreateBindGroup);
    LOAD(DeviceCreateCommandEncoder); LOAD(CommandEncoderCopyBufferToBuffer); LOAD(CommandEncoderBeginComputePass);
    LOAD(ComputePassEncoderSetPipeline); LOAD(ComputePassEncoderSetBindGroup); LOAD(ComputePassEncoderDispatchWorkgroups);
    LOAD(ComputePassEncoderEnd); LOAD(CommandEncoderFinish); LOAD(QueueSubmit);
    LOAD(BufferMapAsync); LOAD(BufferGetConstMappedRange);
    LOAD(ComputePassEncoderRelease); LOAD(CommandBufferRelease); LOAD(CommandEncoderRelease);
    LOAD(ShaderModuleRelease); LOAD(ComputePipelineRelease); LOAD(BindGroupLayoutRelease); LOAD(PipelineLayoutRelease);
    LOAD(BindGroupRelease); LOAD(BufferRelease); LOAD(QueueRelease); LOAD(DeviceRelease); LOAD(AdapterRelease); LOAD(InstanceRelease);
    WGPUInstance instance = CreateInstance(NULL);
    WGPUAdapter adapter = NULL; WGPUDevice device = NULL; WGPUQueue queue = NULL;
    WGPUBuffer upload = NULL, buffers[3] = {NULL,NULL,NULL}, readback = NULL;
    WGPUShaderModule module = NULL; WGPUComputePipeline pipeline = NULL;
    WGPUBindGroupLayout bgl = NULL; WGPUPipelineLayout layout = NULL; WGPUBindGroup group = NULL;
    WGPUCommandEncoder encoder = NULL; WGPUComputePassEncoder pass = NULL; WGPUCommandBuffer commands = NULL;
    int result = 1, mapped = 0;
    const char *stage = "instance";
    if (!instance) goto cleanup;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_D3D12;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    ac.mode = WGPUCallbackMode_AllowSpontaneous; ac.callback = adapter_ready; ac.userdata1 = &adapter;
    InstanceRequestAdapter(instance,&options,ac);
    for(int i=0;i<20 && !adapter;++i) { InstanceProcessEvents(instance);Sleep(10); }
    if(!adapter) goto cleanup;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if(AdapterGetInfo(adapter,&info) != WGPUStatus_Success) goto cleanup;
    const int physical = info.backendType == WGPUBackendType_D3D12 && info.vendorID == 0x1002 && info.deviceID == 0x1681 && info.adapterType != WGPUAdapterType_CPU;
    printf("backend=%u vendor=0x%x device=0x%x type=%u name=%.*s\n",info.backendType,info.vendorID,info.deviceID,info.adapterType,(int)info.device.length,info.device.data);
    AdapterInfoFreeMembers(info);
    if(!physical) goto cleanup;
    stage = "device";
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dc.mode=WGPUCallbackMode_AllowSpontaneous;dc.callback=device_ready;dc.userdata1=&device;
    AdapterRequestDevice(adapter,&dd,dc);
    for(int i=0;i<20 && !device;++i) { InstanceProcessEvents(instance);Sleep(10); }
    if(!device) goto cleanup;
    queue=DeviceGetQueue(device);if(!queue) goto cleanup;
    const char *wgsl = "\n"
        "    @group(0) @binding(0) var<storage, read> weights: array<f32>;\n"
        "    @group(0) @binding(1) var<storage, read> hidden: array<f32>;\n"
        "    @group(0) @binding(2) var<storage, read_write> output: array<f32>;\n"
        "\n"
        "    @compute @workgroup_size(64, 1, 1)\n"
        "    fn OutputLayer(@builtin(global_invocation_id) id: vec3<u32>) {\n"
        "      let output_index = i32(id.x);\n"
        "      let base = output_index * 2;\n"
        "      let sum = weights[base] + hidden[0] * weights[base + 1];\n"
        "      output[output_index] = 1.0 / (1.0 + exp(-sum));\n"
        "    }\n"
        "  ";
    WGPUShaderSourceWGSL ws = WGPU_SHADER_SOURCE_WGSL_INIT;
    ws.code=(WGPUStringView){wgsl,WGPU_STRLEN};
    WGPUShaderModuleDescriptor sd=WGPU_SHADER_MODULE_DESCRIPTOR_INIT;sd.nextInChain=&ws.chain;
    module=DeviceCreateShaderModule(device,&sd);if(!module) goto cleanup;
    if(!automatic) {
        WGPUBindGroupLayoutEntry entries[3];
        for(unsigned i=0;i<3;++i) {
            entries[i]=(WGPUBindGroupLayoutEntry)WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entries[i].binding=i;entries[i].visibility=WGPUShaderStage_Compute;
            entries[i].buffer.type=i==2 ? WGPUBufferBindingType_Storage : WGPUBufferBindingType_ReadOnlyStorage;
        }
        WGPUBindGroupLayoutDescriptor bd=WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bd.entryCount=3;bd.entries=entries;bgl=DeviceCreateBindGroupLayout(device,&bd);if(!bgl) goto cleanup;
        WGPUPipelineLayoutDescriptor pd=WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&bgl;layout=DeviceCreatePipelineLayout(device,&pd);if(!layout) goto cleanup;
    }
    stage="named pipeline";
    WGPUComputePipelineDescriptor cp=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cp.layout=layout;cp.compute.module=module;
    if(recover) {
        cp.compute.entryPoint=(WGPUStringView){"main",4};
        pipeline=DeviceCreateComputePipeline(device,&cp);
        if(pipeline) goto cleanup;
        printf("missing entry rejected; recovering with OutputLayer\n");
    }
    cp.compute.entryPoint=(WGPUStringView){"OutputLayer",11};
    pipeline=DeviceCreateComputePipeline(device,&cp);if(!pipeline) goto cleanup;
    if(automatic) { bgl=ComputePipelineGetBindGroupLayout(pipeline,0);if(!bgl) goto cleanup; }
    stage="buffers";
    const uint64_t sizes[3]={80,4,40};const uint64_t offsets[3]={0,80,84};
    WGPUBufferDescriptor bd=WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.size=124;bd.usage=WGPUBufferUsage_CopySrc;bd.mappedAtCreation=WGPU_TRUE;
    upload=DeviceCreateBuffer(device,&bd);if(!upload) goto cleanup;
    float *initial=BufferGetMappedRange(upload,0,124);if(!initial) goto cleanup;
    for(unsigned i=0;i<31;++i) initial[i]=i<20 ? 1.0f : 0.0f;
    BufferUnmap(upload);bd.mappedAtCreation=WGPU_FALSE;
    for(unsigned i=0;i<3;++i) {
        bd.size=sizes[i];bd.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|(i==2 ? WGPUBufferUsage_CopySrc : 0);
        buffers[i]=DeviceCreateBuffer(device,&bd);if(!buffers[i]) goto cleanup;
    }
    bd.size=40;bd.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;
    readback=DeviceCreateBuffer(device,&bd);if(!readback) goto cleanup;
    WGPUBindGroupEntry entries[3];
    for(unsigned i=0;i<3;++i) {
        entries[i]=(WGPUBindGroupEntry)WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding=i;entries[i].buffer=buffers[i];entries[i].size=sizes[i];
    }
    WGPUBindGroupDescriptor gd=WGPU_BIND_GROUP_DESCRIPTOR_INIT;gd.layout=bgl;gd.entryCount=3;gd.entries=entries;
    group=DeviceCreateBindGroup(device,&gd);if(!group) goto cleanup;
    stage="record/submit";
    encoder=DeviceCreateCommandEncoder(device,NULL);if(!encoder) goto cleanup;
    for(unsigned i=0;i<3;++i) CommandEncoderCopyBufferToBuffer(encoder,upload,offsets[i],buffers[i],0,sizes[i]);
    pass=CommandEncoderBeginComputePass(encoder,NULL);if(!pass) goto cleanup;
    ComputePassEncoderSetPipeline(pass,pipeline);ComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
    ComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    if(repeated) ComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    ComputePassEncoderEnd(pass);ComputePassEncoderRelease(pass);pass=NULL;
    CommandEncoderCopyBufferToBuffer(encoder,buffers[2],0,readback,0,40);
    commands=CommandEncoderFinish(encoder,NULL);if(!commands) goto cleanup;
    ShaderModuleRelease(module);module=NULL;ComputePipelineRelease(pipeline);pipeline=NULL;
    BindGroupRelease(group);group=NULL;BindGroupLayoutRelease(bgl);bgl=NULL;
    if(layout) { PipelineLayoutRelease(layout);layout=NULL; }
    QueueSubmit(queue,1,&commands);
    CommandBufferRelease(commands);commands=NULL;CommandEncoderRelease(encoder);encoder=NULL;
    BufferRelease(upload);upload=NULL;
    for(unsigned i=0;i<3;++i) { BufferRelease(buffers[i]);buffers[i]=NULL; }
    stage="readback";
    WGPUBufferMapCallbackInfo mc=WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mc.mode=WGPUCallbackMode_AllowSpontaneous;mc.callback=map_ready;mc.userdata1=&mapped;
    BufferMapAsync(readback,WGPUMapMode_Read,0,40,mc);
    for(int i=0;i<100 && !mapped;++i) { InstanceProcessEvents(instance);Sleep(10); }
    if(mapped!=1) goto cleanup;
    const float *actual=BufferGetConstMappedRange(readback,0,40);if(!actual) goto cleanup;
    const double expected=1.0/(1.0+exp(-1.0));
    for(unsigned i=0;i<10;++i) {
        printf("value[%u]: expected=%.17g actual=%.17g\n",i,expected,(double)actual[i]);
        if(!isfinite(actual[i]) || fabs((double)actual[i]-expected)>1e-6) goto cleanup;
    }
    result=0;
cleanup:
    if(mapped==1 && readback) BufferUnmap(readback);
    if(pass) ComputePassEncoderRelease(pass);
    if(commands) CommandBufferRelease(commands);
    if(encoder) CommandEncoderRelease(encoder);
    if(group) BindGroupRelease(group);
    if(pipeline) ComputePipelineRelease(pipeline);
    if(module) ShaderModuleRelease(module);
    if(layout) PipelineLayoutRelease(layout);
    if(bgl) BindGroupLayoutRelease(bgl);
    if(upload) BufferRelease(upload);
    for(unsigned i=0;i<3;++i) if(buffers[i]) BufferRelease(buffers[i]);
    if(readback) BufferRelease(readback);
    if(queue) QueueRelease(queue);
    if(device) DeviceRelease(device);
    if(adapter) AdapterRelease(adapter);
    if(instance) InstanceRelease(instance);
    FreeLibrary(library);
    if(result) fprintf(stderr,"FAIL stage=%s\n",stage);
    else printf("PASS: named OutputLayer, all10 independent f32 outputs, early release, mode=%s\n",argc==3 ? argv[2] : "explicit");
    return result;
}
