/* SPDX-License-Identifier: MIT
 * Unchanged select fixtures through public C pipelines and real GPU readback. */
#include "consumer.h"
#include "shader_fixture.h"

enum { CASE_WORDS = 32, RETAINED_WORDS = 4, SUFFIX_WORDS = 2 };
#define CANARY UINT32_C(0xdeadbeef)

static const uint32_t retained_seed[RETAINED_WORDS] = {
    UINT32_C(0x3e800000),UINT32_C(0x3f000000),UINT32_C(0x3f400000),UINT32_C(0xc0400000)};
static const uint32_t retained_expected[RETAINED_WORDS] = {0,0,UINT32_C(0x3f800000),0};
/* Independent finite table: no expected values are obtained from shader output
 * or an emitter. Trace123 proves eager operand/condition order; word19 preserves
 * the pre-change load when false is selected. Words24..27/30 remain untouched. */
static const uint32_t cases_expected[2][CASE_WORDS] = {
    {11,2,4,6,8,2,17,6,23,31,UINT32_C(0xfffffff5),UINT32_C(0x80000000),UINT32_C(0xc0200000),
        0,0,1,41,2,123,7,5,13,4,19,CANARY,CANARY,CANARY,CANARY,123,5,CANARY,0},
    {29,13,17,19,23,13,4,19,8,UINT32_C(0xfffffff9),37,UINT32_C(0x40e80000),UINT32_C(0x3fc00000),
        1,1,0,43,2,123,99,5,13,4,19,CANARY,CANARY,CANARY,CANARY,123,5,CANARY,1}
};

static size_t prefix_words(state *s)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device,&limits) == WGPUStatus_Success);
    assert(limits.minStorageBufferOffsetAlignment);
    size_t alignment = limits.minStorageBufferOffsetAlignment;
    if (alignment < 8) alignment = 8;
    assert(alignment % sizeof(uint32_t) == 0 && !(alignment & (alignment - 1)));
    return alignment / sizeof(uint32_t);
}

static void run_case(state *s,const char *label,const char *source,bool retained,uint32_t flag)
{
    WGPUComputePipeline program = pipeline(s,source);
    size_t count = retained ? RETAINED_WORDS : CASE_WORDS;
    size_t prefix = prefix_words(s);
    assert(prefix <= SIZE_MAX / sizeof(uint32_t) - count - SUFFIX_WORDS);
    size_t total = prefix + count + SUFFIX_WORDS;
    size_t bytes = total * sizeof(uint32_t);
    uint32_t *seeds = malloc(bytes);
    assert(seeds);
    for (size_t i = 0; i < total; ++i) seeds[i] = CANARY;
    if (retained) memcpy(seeds + prefix,retained_seed,sizeof(retained_seed));
    else {
        assert(flag <= 1);
        seeds[prefix + 28] = 0;
        seeds[prefix + 29] = 99;
        seeds[prefix + 31] = flag;
    }
    WGPUBuffer data = buffer(s,bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(s,bytes,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,data,0,seeds,bytes);
    free(seeds);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=data,
        .offset=prefix * sizeof(uint32_t),.size=count * sizeof(uint32_t)};
    WGPUBindGroup bindings = group(s,program,&entry,1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    assert(encoder && pass);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    /* Retained WGSL addresses six lanes in its four-word binding. The extra
     * invocations must not read/write the underlying allocation's suffix. */
    wgpuComputePassEncoderDispatchWorkgroups(pass,retained ? 6 : 1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,data,0,readback,0,bytes);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    wgpuBufferRelease(data);
    submit(s,encoder);
    const uint32_t *actual = map(s,readback,bytes);
    const uint32_t *expected = retained ? retained_expected : cases_expected[flag];
    for (size_t i = 0; i < total; ++i) {
        uint32_t wanted = i < prefix || i >= prefix + count ? CANARY : expected[i - prefix];
        if (actual[i] != wanted)
            fprintf(stderr,"select %s allocation word%zu: %08x != %08x\n",label,i,actual[i],wanted);
        assert(actual[i] == wanted);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(int argc,char **argv)
{
    if (argc > 3) {
        fprintf(stderr,"usage: %s [select_retained.wgsl [select_cases.wgsl]]\n",argv[0]);
        return 2;
    }
    const char *retained_path = argc > 1 ? argv[1] : "../compiler/tests/select_retained.wgsl";
    const char *cases_path = argc > 2 ? argv[2] : "../compiler/tests/select_cases.wgsl";
    char *retained = native_test_shader(retained_path), *cases = native_test_shader(cases_path);
    state s = {0};
    consumer_init(&s);
    run_case(&s,"retained",retained,true,0);
    run_case(&s,"false-mask",cases,false,0);
    run_case(&s,"true-mask",cases,false,1);
    free(retained);
    free(cases);
    consumer_finish(&s);
    printf("PASS public select: unchanged retained/cases WGSL, scalar/vector/bool masks, eager trace123, operand snapshots, signed-zero bits, 68 exact result words, allocation prefix/suffix and retained OOB lanes, early releases %s\n",
        adapter_class_scope());
    return 0;
}
