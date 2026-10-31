/* SPDX-License-Identifier: MIT
 * Unchanged logical reduction fixtures through public pipelines and readback. */
#include "consumer.h"
#include "shader_fixture.h"

enum { CASE_WORDS = 32, RETAINED_WORDS = 4, SUFFIX_WORDS = 2 };
#define CANARY UINT32_C(0xdeadbeef)

static const uint32_t retained_expected[RETAINED_WORDS] = {3,CANARY,CANARY,CANARY};
/* Independent finite tables: no expected value comes from an emitter, shader
 * execution or a host WGSL evaluator. All four seed words remain unchanged.
 * Traces1234/5678 distinguish eager evaluation from short-circuit reduction;
 * counts1/2 distinguish one vector argument from repeated scalar arguments.
 * Words 16, 17 and 19 read data[32u] and data[0xffffffffu] past the 32-word
 * binding; every target clamps them to data[31] (seed 99), giving 1, 0, 1
 * (compiler/targets/hlsl/AGENTS.md, compiler/src/spirv_clamp.c,
 * compiler/targets/metal/AGENTS.md). */
static const uint32_t cases_expected[2][CASE_WORDS] = {
    {0,1,0,1,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1,5678,0,1234,1,5678,1,1,2,0,7,0,99},
    {0,1,0,1,1,1,1,1,0,1,1,0,0,1,1,1,1,0,1,1,5678,0,1234,1,5678,1,1,2,1,7,0,99}
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
    const size_t count = retained ? RETAINED_WORDS : CASE_WORDS;
    const size_t prefix = prefix_words(s);
    assert(prefix <= SIZE_MAX / sizeof(uint32_t) - count - SUFFIX_WORDS);
    const size_t total = prefix + count + SUFFIX_WORDS;
    const size_t bytes = total * sizeof(uint32_t);
    uint32_t *seeds = malloc(bytes);
    assert(seeds);
    for (size_t i = 0; i < total; ++i) seeds[i] = CANARY;
    if (!retained) {
        assert(flag <= 1);
        seeds[prefix + 28] = flag;
        seeds[prefix + 29] = 7;
        seeds[prefix + 30] = 0;
        seeds[prefix + 31] = 99;
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
    /* One retained invocation avoids races on its literal out_data[0]. The
     * cases shader's descriptor-edge/max-u32 reads must ignore the suffix. */
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
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
        const uint32_t wanted = i < prefix || i >= prefix + count ? CANARY : expected[i - prefix];
        if (actual[i] != wanted)
            fprintf(stderr,"all/any %s allocation word%zu: %08x != %08x\n",label,i,actual[i],wanted);
        assert(actual[i] == wanted);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(int argc,char **argv)
{
    if (argc > 3) {
        fprintf(stderr,"usage: %s [all_any_retained.wgsl [all_any_cases.wgsl]]\n",argv[0]);
        return 2;
    }
    const char *retained_path = argc > 1 ? argv[1] : "../compiler/tests/all_any_retained.wgsl";
    const char *cases_path = argc > 2 ? argv[2] : "../compiler/tests/all_any_cases.wgsl";
    char *retained = native_test_shader(retained_path);
    char *cases = native_test_shader(cases_path);
    state s = {0};
    consumer_init(&s);
    run_case(&s,"retained",retained,true,0);
    run_case(&s,"false-scalar",cases,false,0);
    run_case(&s,"true-scalar",cases,false,1);
    free(retained);
    free(cases);
    consumer_finish(&s);
    printf("PASS public all/any: unchanged retained/cases WGSL, scalar and bool-vector reductions, eager traces1234/5678, once-only operand counts, 68 exact words, descriptor-edge/max-u32 OOB clamp, allocation prefix/suffix, early releases %s\n",
        adapter_class_scope());
    return 0;
}
