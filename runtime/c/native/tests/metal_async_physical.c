/* SPDX-License-Identifier: MIT
 * One explicitly authorized, bounded physical Metal correctness graph. */
#include "doe_platform.h"
#include "doe_native_queue.h"
#include "consumer.h"
#include "../../common/include/doe_sha256.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <unistd.h>

enum { WORDS = 262144, BYTES = WORDS * 4 };
static FILE *journal;
static const char *output_directory;
static bool unresolved, submitted;
static const char shader[] =
    "@group(0) @binding(0) var<storage,read> src:array<u32>;"
    "@group(0) @binding(1) var<storage,read_write> dst:array<u32>;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id:vec3<u32>){"
    "if(id.x<arrayLength(&src)){dst[id.x]=src[id.x]*3u+7u;}}";

static void checkpoint(const char *phase, const char *completion, int pending, unsigned errors)
{
    if (fprintf(journal,"{\"phase\":\"%s\",\"gpuCompletion\":\"%s\",\"pendingObserved\":%d,\"errors\":%u}\n",
            phase,completion,pending,errors) < 0 || fflush(journal) || fsync(fileno(journal))) _Exit(4);
}
static void failed(const char *condition)
{
    fprintf(stderr,"physical Metal gate failed: %s\n",condition);
    checkpoint("failed",!submitted ? "not-dispatched" : unresolved ? "unknown" : "completed",0,1);
    _Exit(3);
}
#define REQUIRE(condition) do { if (!(condition)) failed(#condition); } while (0)

static void owner(const char *symbol, const char *expected)
{
    Dl_info info;
    char observed[PATH_MAX], required[PATH_MAX];
    void *address = dlsym(RTLD_DEFAULT,symbol);
    REQUIRE(address && dladdr(address,&info) && realpath(info.dli_fname,observed) && realpath(expected,required));
    REQUIRE(!strcmp(observed,required));
    printf("OWNER %s %s\n",symbol,observed);
}
static void driver_images(void)
{
    unsigned metal = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *name = _dyld_get_image_name(i);
        if (!name || (!strstr(name,"Metal.framework/") && !strstr(name,"AGXMetal"))) continue;
        const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(i);
        REQUIRE(header && header->magic == MH_MAGIC_64 && header->sizeofcmds <= 16u*1024u*1024u);
        const unsigned char *commands = (const unsigned char *)(header + 1);
        size_t offset = 0;
        for (uint32_t c = 0; c < header->ncmds; ++c) {
            REQUIRE(offset <= header->sizeofcmds && header->sizeofcmds - offset >= sizeof(struct load_command));
            const struct load_command *command = (const struct load_command *)(commands + offset);
            REQUIRE(command->cmdsize >= sizeof(*command) && command->cmdsize <= header->sizeofcmds - offset);
            if (command->cmd == LC_UUID) {
                REQUIRE(command->cmdsize >= sizeof(struct uuid_command));
                const struct uuid_command *uuid = (const struct uuid_command *)command;
                printf("DRIVER %s ",name);
                for (size_t n = 0; n < 16; ++n) printf("%02x",(unsigned)uuid->uuid[n]);
                putchar('\n');
            }
            offset += command->cmdsize;
        }
        ++metal;
    }
    REQUIRE(metal && !fflush(stdout) && !fsync(STDOUT_FILENO));
}
static WGPUBindGroup make_group(state *s, WGPUBindGroupLayout layout, WGPUBuffer source, WGPUBuffer target)
{
    WGPUBindGroupEntry entries[2] = {{.binding=0,.buffer=source,.size=BYTES},
        {.binding=1,.buffer=target,.size=BYTES}};
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=2,.entries=entries};
    WGPUBindGroup result = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    REQUIRE(result && !s->errors);
    return result;
}
static WGPUCommandBuffer record(state *s, WGPUComputePipeline pipeline, WGPUBindGroup bindings,
    WGPUBuffer storage, WGPUBuffer readback)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    REQUIRE(encoder);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    REQUIRE(pass);
    wgpuComputePassEncoderSetPipeline(pass,pipeline);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,WORDS/64,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,storage,0,readback,0,BYTES);
    WGPUCommandBuffer result = wgpuCommandEncoderFinish(encoder,NULL);
    wgpuCommandEncoderRelease(encoder);
    REQUIRE(result && !s->errors);
    return result;
}
static void completion_reply(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    if (status != WGPUQueueWorkDoneStatus_Success)
        fprintf(stderr,"queue completion failed: %.*s\n",(int)(message.length < 512 ? message.length : 512),message.data);
    REQUIRE(status == WGPUQueueWorkDoneStatus_Success);
    ++*(unsigned *)userdata;
}
static void observe(state *s, const char *phase)
{
    uint32_t info = doeNativeQueueSyncInfo(s->queue);
    REQUIRE((info & DOE_NATIVE_QUEUE_SYNC_BACKEND_METAL) && !s->errors);
    unresolved = (info & DOE_NATIVE_QUEUE_SYNC_DEFERRED_SUBMISSIONS) != 0;
    if (!unresolved) {
        unsigned done = 0;
        WGPUQueueWorkDoneCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
            .callback=completion_reply,.userdata1=&done};
        REQUIRE(wgpuQueueOnSubmittedWorkDone(s->queue,callback).id && done == 1);
    }
    checkpoint(phase,unresolved ? "unknown" : "completed",unresolved ? 1 : 0,s->errors);
}
static void submit_record(state *s, const char *phase, WGPUCommandBuffer command)
{
    unresolved = true; submitted = true;
    checkpoint(phase,"unknown",-1,s->errors);
    wgpuQueueSubmit(s->queue,1,&command);
    REQUIRE(!s->errors);
    wgpuCommandBufferRelease(command);
    observe(s,"submit-returned");
}
static void terminal(state *s)
{
    struct timespec now, deadline;
    REQUIRE(doe_monotonic_now(&now).status == DOE_HOST_OK);
    REQUIRE(doe_time_add(&now,UINT64_C(5000000000),&deadline).status == DOE_HOST_OK);
    do {
        observe(s,"completion-poll");
        if (!unresolved) break;
        REQUIRE(doe_monotonic_now(&now).status == DOE_HOST_OK && !doe_time_reached(&now,&deadline));
        REQUIRE(doe_sleep_for(UINT64_C(1000000)).status == DOE_HOST_OK);
    } while (true);
}
static void capture(state *s, WGPUBuffer readback, unsigned arm)
{
    unsigned maps = s->mapped;
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=mapped,.userdata1=s};
    REQUIRE(wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,BYTES,callback).id && s->mapped == maps + 1);
    const uint32_t *actual = wgpuBufferGetConstMappedRange(readback,0,BYTES);
    REQUIRE(actual);
    for (uint32_t i = 0; i < WORDS; ++i) {
        uint32_t expected = arm == 0 ? i*3u+10u : arm == 1 ? (i%1021u)*3u+22u : i*9u+37u;
        REQUIRE(actual[i] == expected && actual[i] != 0 && actual[i] != UINT32_C(0xdeadbeef));
    }
    char path[PATH_MAX];
    int length = snprintf(path,sizeof(path),"%s/capture-%u.bin",output_directory,arm);
    REQUIRE(length > 0 && (size_t)length < sizeof(path));
    int fd = open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    REQUIRE(fd >= 0);
    const unsigned char *bytes = (const unsigned char *)actual;
    for (size_t written = 0; written < BYTES;) {
        ssize_t count = write(fd,bytes+written,BYTES-written);
        if (count < 0 && errno == EINTR) continue;
        REQUIRE(count > 0); written += (size_t)count;
    }
    REQUIRE(!fsync(fd) && !close(fd));
    uint8_t digest[32]; char hex[65];
    doe_common_sha256(actual,BYTES,digest); doe_common_sha256_hex(digest,hex);
    printf("CAPTURE %u %u %s\n",arm,(unsigned)BYTES,hex);
    REQUIRE(!fflush(stdout));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    checkpoint("readback-validated","completed",0,s->errors);
}
int main(int argc, char **argv)
{
    if (argc != 5 || strcmp(argv[1],"--run") || !getenv("DOE_MTL_ALLOW_HARDWARE") ||
        strcmp(getenv("DOE_MTL_ALLOW_HARDWARE"),"1")) return 2;
    output_directory = argv[2];
    char path[PATH_MAX];
    int length = snprintf(path,sizeof(path),"%s/checkpoints.jsonl",output_directory);
    if (length <= 0 || (size_t)length >= sizeof(path)) return 2;
    int fd = open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd < 0 || !(journal = fdopen(fd,"w"))) return 2;
    checkpoint("started","not-dispatched",0,0);
    owner("wgpuQueueSubmit",argv[3]); owner("doe_compiler_compile",argv[4]);
    state s = {0}; consumer_init(&s);
    WGPUShaderSourceWGSL wgsl = {.chain={.sType=WGPUSType_ShaderSourceWGSL},.code={shader,sizeof(shader)-1}};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain=&wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device,&module_desc);
    REQUIRE(module && !s.errors);
    WGPUBindGroupLayoutEntry entries[2] = {{.binding=0,.visibility=WGPUShaderStage_Compute,
        .buffer={.type=WGPUBufferBindingType_ReadOnlyStorage,.minBindingSize=4}},
        {.binding=1,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_Storage,.minBindingSize=4}}};
    WGPUBindGroupLayoutDescriptor gd = {.entryCount=2,.entries=entries};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s.device,&gd);
    WGPUPipelineLayoutDescriptor pd = {.bindGroupLayoutCount=1,.bindGroupLayouts=&layout};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(s.device,&pd);
    WGPUComputePipelineDescriptor cd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cd.layout = pipeline_layout; cd.compute.module = module; cd.compute.entryPoint = (WGPUStringView){"main",4};
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(s.device,&cd);
    REQUIRE(layout && pipeline_layout && pipeline && !s.errors);
    driver_images();
    uint32_t *host = malloc(BYTES); REQUIRE(host);
    WGPUBuffer a = buffer(&s,BYTES,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
    WGPUBuffer b = buffer(&s,BYTES,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
    WGPUBuffer storage[3], readback[3];
    for (uint32_t i = 0; i < WORDS; ++i) host[i] = i+1;
    wgpuQueueWriteBuffer(s.queue,a,0,host,BYTES);
    for (uint32_t i = 0; i < WORDS; ++i) host[i] = i%1021u+5u;
    wgpuQueueWriteBuffer(s.queue,b,0,host,BYTES);
    for (uint32_t i = 0; i < WORDS; ++i) host[i] = UINT32_C(0xdeadbeef);
    for (unsigned i = 0; i < 3; ++i) {
        storage[i] = buffer(&s,BYTES,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
        readback[i] = buffer(&s,BYTES,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s.queue,storage[i],0,host,BYTES);
        wgpuQueueWriteBuffer(s.queue,readback[i],0,host,BYTES);
    }
    free(host);
    WGPUBindGroup groups[3] = {make_group(&s,layout,a,storage[0]),make_group(&s,layout,b,storage[1]),
        make_group(&s,layout,storage[0],storage[2])};
    submit_record(&s,"before-submit-a",record(&s,pipeline,groups[0],storage[0],readback[0]));
    wgpuBufferRelease(a); wgpuBindGroupRelease(groups[0]);
    submit_record(&s,"before-submit-b",record(&s,pipeline,groups[1],storage[1],readback[1]));
    wgpuBufferRelease(b); wgpuBindGroupRelease(groups[1]);
    submit_record(&s,"before-submit-dependent",record(&s,pipeline,groups[2],storage[2],readback[2]));
    wgpuBindGroupRelease(groups[2]);
    for (unsigned i = 0; i < 3; ++i) wgpuBufferRelease(storage[i]);
    wgpuComputePipelineRelease(pipeline); wgpuShaderModuleRelease(module);
    wgpuPipelineLayoutRelease(pipeline_layout); wgpuBindGroupLayoutRelease(layout);
    checkpoint("public-input-handles-dropped","unknown",-1,s.errors);
    terminal(&s);
    for (unsigned i = 0; i < 3; ++i) capture(&s,readback[i],i);
    REQUIRE(!s.errors);
    consumer_finish(&s);
    checkpoint("terminal","completed",0,0);
    if (fclose(journal)) return 4;
    puts("PASS physical Metal C async graph: disjoint/dependent1MiB compute and complete exact readbacks; early public input drops");
    return 0;
}
