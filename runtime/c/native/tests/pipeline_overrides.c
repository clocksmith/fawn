/* SPDX-License-Identifier: MIT
 * Public-ABI selected-entry override, deferred-layout and stage isolation control. */
#include "consumer.h"
#include <math.h>

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(result);
    return result;
}
static void compilation(WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info,
    void *userdata, void *unused)
{
    (void)unused;
    assert(status == WGPUCompilationInfoRequestStatus_Success && info && !info->messageCount);
    ++*(unsigned *)userdata;
}
static WGPUComputePipeline compute(state *s, WGPUShaderModule shader,
    const WGPUConstantEntry *values, size_t count, const char *entry)
{
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = shader;
    descriptor.compute.constants = values;
    descriptor.compute.constantCount = count;
    descriptor.compute.entryPoint = (WGPUStringView){entry,entry ? WGPU_STRLEN : 0};
    return wgpuDeviceCreateComputePipeline(s->device,&descriptor);
}
static void rejected(state *s, WGPUShaderModule shader, const WGPUConstantEntry *values,
    size_t count, const char *entry)
{
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!compute(s,shader,values,count,entry));
    expect_validation(s);
}
static WGPUConstantEntry value(const char *key, double number)
{
    return (WGPUConstantEntry){.key={key,WGPU_STRLEN},.value=number};
}
static void compute_cases(state *s)
{
    char source[] =
        "@id(17) override width:u32;override mul:i32=3;override bias:f32=0.5;"
        "override enabled:bool=true;override reciprocal:i32=12/mul;override other:u32;"
        "var<workgroup> cache:array<u32,width>;"
        "@group(0) @binding(0) var<storage,read_write> result:array<u32>;"
        "@compute @workgroup_size(width) fn selected(@builtin(local_invocation_index) lane:u32){"
        "cache[lane]=lane+1u;workgroupBarrier();if(lane==0u){result[0]=width;"
        "result[1]=bitcast<u32>(mul);result[2]=bitcast<u32>(bias);"
        "result[3]=select(0u,1u,enabled);result[4]=cache[width-1u];"
        "result[5]=cache[0]+cache[width-1u];result[6]=bitcast<u32>(reciprocal);}}"
        "@compute @workgroup_size(other) fn sibling(){}";
    WGPUShaderModule shader = module(s,source);
    source[0] = '#'; /* The module retains its original source, not this span. */
    unsigned reports = 0;
    WGPUCompilationInfoCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=compilation,.userdata1=&reports};
    assert(wgpuShaderModuleGetCompilationInfo(shader,callback).id && reports == 1);
    WGPUConstantEntry a[] = {value("17",3),value("mul",-5),value("bias",1.5),value("enabled",0)};
    WGPUConstantEntry b[] = {value("17",7),value("mul",9),value("bias",-2.25),value("enabled",1)};
    WGPUConstantEntry c[] = {value("17",2),value("mul",0),value("bias",0),value("enabled",2),value("reciprocal",99)};
    WGPUComputePipeline programs[] = {compute(s,shader,a,4,"selected"),compute(s,shader,b,4,"selected"),
        compute(s,shader,c,5,"selected")};
    assert(programs[0] && programs[1] && programs[2]);
    a[0].value = 31; b[1].value = 87; c[4].value = -93; /* Published pipelines are immutable. */
    rejected(s,shader,NULL,0,"selected"); /* Required width, not the unrelated sibling. */
    rejected(s,shader,NULL,0,NULL); /* Two compute entries make AUTO ambiguous. */
    WGPUConstantEntry bad[] = {value("17",3),value("17",7)};
    rejected(s,shader,bad,2,"selected");
    const char *invalid_keys[] = {"width","017","unknown"};
    for (size_t i = 0; i < sizeof(invalid_keys)/sizeof(*invalid_keys); ++i) {
        bad[0] = value(invalid_keys[i],3);
        rejected(s,shader,bad,1,"selected");
    }
    const double invalid_widths[] = {-1,0,3.5,INFINITY,NAN};
    for (size_t i = 0; i < sizeof(invalid_widths)/sizeof(*invalid_widths); ++i) {
        bad[0] = value("17",invalid_widths[i]);
        rejected(s,shader,bad,1,"selected");
    }
    bad[0] = value("17",3); bad[1] = value("mul",0);
    rejected(s,shader,bad,2,"selected"); /* Default division is evaluated when not supplied. */
    bad[1] = value("bias",1e300);
    rejected(s,shader,bad,2,"selected");
    WGPUChainedStruct chain = {.sType=WGPUSType_ShaderSourceWGSL};
    bad[0].nextInChain = &chain;
    rejected(s,shader,bad,1,"selected");
    wgpuShaderModuleRelease(shader);
    WGPUBuffer output[3], readback = buffer(s,104,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    for (size_t i = 0; i < 3; ++i) {
        output[i] = buffer(s,32,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
        WGPUBindGroupEntry entry = {.binding=0,.buffer=output[i],.size=32};
        WGPUBindGroup bindings = group(s,programs[i],&entry,1);
        dispatch(encoder,programs[i],bindings);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,output[i],0,readback,4+32*i,32);
        wgpuBindGroupRelease(bindings);
        wgpuComputePipelineRelease(programs[i]);
        wgpuBufferRelease(output[i]);
    }
    submit(s,encoder);
    const uint32_t expected[26] = {0,3,UINT32_C(0xfffffffb),UINT32_C(0x3fc00000),0,3,4,UINT32_C(0xfffffffe),0,
        7,9,UINT32_C(0xc0100000),1,7,8,1,0,2,0,0,1,2,3,99,0,0};
    const void *data = map(s,readback,sizeof(expected));
    assert(!memcmp(data,expected,sizeof(expected)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static WGPURenderPipeline graphics_pipeline(state *s, WGPUShaderModule shader, bool right)
{
    WGPUConstantEntry vertex_values[] = {value("scale",right ? 0.5 : 1),value("offset",right ? 0.5 : 0)};
    WGPUConstantEntry fragment_values[] = {value("red",right ? 0 : 1),value("green",right ? 1 : 0)};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.entryPoint={"fs",2},.constantCount=2,
        .constants=fragment_values,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = (WGPUStringView){"vs",2};
    descriptor.vertex.constantCount = 2;
    descriptor.vertex.constants = vertex_values;
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(result);
    return result;
}
static void graphics_cases(state *s)
{
    const char *source = "override scale:f32;override offset:f32;override red:f32;override green:f32;"
        "@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
        "return vec4f(p[i].x*scale+offset,p[i].y,0,1);}"
        "@fragment fn fs()->@location(0) vec4f{return vec4f(red,green,0,1);}";
    WGPUShaderModule shader = module(s,source);
    WGPURenderPipeline programs[] = {graphics_pipeline(s,shader,false),graphics_pipeline(s,shader,true)};
    wgpuShaderModuleRelease(shader);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUBuffer readback = buffer(s,1024,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    for (size_t i = 0; i < 2; ++i) {
        WGPUTexture image = texture(s,WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
        WGPUTextureView view = wgpuTextureCreateView(image,NULL);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = view; color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
        color.clearValue = (WGPUColor){0,0,0,1};
        WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        assert(pass);
        wgpuRenderPassEncoderSetPipeline(pass,programs[i]);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUTexelCopyTextureInfo from = {.texture=image};
        WGPUTexelCopyBufferInfo to = {.buffer=readback,.layout={.offset=512*i,.bytesPerRow=256,.rowsPerImage=2}};
        WGPUExtent3D size = {2,2,1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&to,&size);
        wgpuTextureViewRelease(view); wgpuTextureRelease(image); wgpuRenderPipelineRelease(programs[i]);
    }
    submit(s,encoder);
    const unsigned char *data = map(s,readback,1024);
    for (size_t image = 0; image < 2; ++image) for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 2; ++x) {
            const unsigned char expected[] = {image == 0 ? 255 : 0,image && x ? 255 : 0,0,255};
            assert(!memcmp(data+512*image+256*y+4*x,expected,4));
        }
        for (size_t padding = 8; padding < 256; ++padding) assert(data[512*image+256*y+padding] == 0);
    }
    wgpuBufferUnmap(readback); wgpuBufferRelease(readback);
}
int main(void)
{
    state s = {0}; consumer_init(&s);
    compute_cases(&s);
    graphics_cases(&s);
    consumer_finish(&s);
    printf("PASS public pipeline overrides: module validation/source ownership, selected sibling isolation, "
        "three immutable typed specializations, 3/7/2-element workgroup arrays, skipped supplied default, "
        "key/value failures and early release; exact26words; separate vertex/fragment overrides, "
        "exact8pixels+padding %s\n",adapter_class_scope());
}
