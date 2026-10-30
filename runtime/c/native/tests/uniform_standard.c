/* SPDX-License-Identifier: MIT
 * Public C composition with exact uniform binding ranges, independent expected
 * words and distinguishable well-typed wrong-index/wrong-layout controls. */
#include "consumer.h"

typedef struct uniform_case {
    const char *name, *source;
    uint32_t bytes, expected[5];
    bool tiny, matrix, wrong;
} uniform_case;

#define OUTPUT "@group(0) @binding(1) var<storage,read_write> result:array<u32,5>;"
#define MAIN "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u)"
#define TINY(N, LAST) "@group(0) @binding(0) var<uniform> u:array<u32," N ">;" OUTPUT MAIN \
    "{result[0]=u[result[0]];result[1]=u[i32(result[1])];result[2]=u[result[2]];" \
    "result[3]=u[result[3]];result[4]=u[" LAST "];}"
#define NESTED(ALIGNMENT) "struct Tiny{a:u32} struct U{values:array<u32,3>,tiny:Tiny,after:u32," \
    "rows:array<array<u32,2>,2>,@align(" ALIGNMENT ") gap:u32,@size(12) reserved:u32,end:u32}" \
    "@group(0) @binding(0) var<uniform> u:U;" OUTPUT MAIN "{let copy=u;" \
    "result[0]=copy.values[1]+copy.tiny.a+copy.after+copy.rows[1][1]+copy.gap+copy.reserved+copy.end;" \
    "result[1]=u.values[i32(id.x)-1];result[2]=u.rows[1][0];result[3]=copy.end;}"

static const uniform_case cases[] = {
    /* Out-of-range uniform indices (-1, N, UINT32_MAX) clamp to the last element. */
    {"tail4", TINY("1", "0"), 4, {3,3,3,3,3}, true, false, false},
    {"tail8", TINY("2", "1"), 8, {5,5,5,5,5}, true, false, false},
    {"tail12", TINY("3", "2"), 12, {7,7,7,7,7}, true, false, false},
    {"nested64", NESTED("8"), 64, {167,7,23,41,999}, false, false, false},
    {"matrix32", "struct U{v:array<f32,2>,m:mat2x2f,last:u32}"
        "@group(0) @binding(0) var<uniform> u:U;" OUTPUT MAIN
        "{result[0]=u32(u.v[1]+u.m[1][0])+u.last;}",
        32, {58,999,999,999,999}, false, true, false},
    {"wrong-index", "@group(0) @binding(0) var<uniform> u:array<u32,3>;" OUTPUT MAIN
        "{result[0]=u[1];result[1]=u[i32(result[1])];result[2]=u[result[2]];"
        "result[3]=u[result[3]];result[4]=u[2];}",
        12, {5,7,7,7,7}, true, false, true},
    {"wrong-layout", NESTED("4"), 64,
        {UINT32_C(0xaaaaab02),7,23,UINT32_C(0xbbbbbbbb),999}, false, false, true},
};

static void run(state *s, const uniform_case *test, bool requirement)
{
    char code[2048];
    int length = snprintf(code, sizeof(code), "%s%s",
        requirement ? "requires uniform_buffer_standard_layout;" : "", test->source);
    assert(length > 0 && (size_t)length < sizeof(code));
    WGPUComputePipeline program = pipeline(s, code);
    uint32_t input[16] = {3,5,7,11,13,17,19,23,29,UINT32_C(0xeeeeeeee),31,37,
        UINT32_C(0xaaaaaaaa),UINT32_C(0xbbbbbbbb),41,UINT32_C(0xcccccccc)};
    if (test->matrix) {
        const float values[] = {3,5,7,11,13,17};
        memcpy(input, values, sizeof(values));
        input[6] = 40;
    }
    uint32_t initial[5] = {999,999,999,999,999};
    if (test->tiny) {
        initial[0] = test->bytes / 4 - 1;
        initial[1] = UINT32_MAX;
        initial[2] = test->bytes / 4;
        initial[3] = UINT32_MAX;
    }
    WGPUBuffer uniform = buffer(s, test->bytes, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(s, sizeof(initial),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(s, sizeof(initial), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, uniform, 0, input, test->bytes);
    wgpuQueueWriteBuffer(s->queue, output, 0, initial, sizeof(initial));
    WGPUBindGroupEntry entries[2] = {{.binding=0,.buffer=uniform,.size=test->bytes},
        {.binding=1,.buffer=output,.size=sizeof(initial)}};
    WGPUBindGroup bindings = group(s, program, entries, 2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    assert(encoder);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass, program);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, sizeof(initial));
    /* Recorded commands retain their resources across early public releases. */
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    wgpuBufferRelease(uniform);
    wgpuBufferRelease(output);
    submit(s, encoder);
    const uint32_t *actual = map(s, readback, sizeof(initial));
    for (size_t i = 0; i < 5; ++i) {
        if (actual[i] != test->expected[i]) fprintf(stderr, "%s requires=%d word%zu: %u != %u\n",
            test->name, requirement, i, actual[i], test->expected[i]);
        assert(actual[i] == test->expected[i]);
    }
    if (test->wrong) assert(actual[0] != (test->tiny ? 7u : 167u));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    setbuf(stdout, NULL);
    state s = {0};
    consumer_init(&s);
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s.adapter, &info) == WGPUStatus_Success);
    printf("Selected adapter: %.*s; backend=%u type=%u; public description: %.*s\n",
        (int)info.device.length, info.device.data, (unsigned)info.backendType, (unsigned)info.adapterType,
        info.description.length ? (int)info.description.length : 12,
        info.description.length ? info.description.data : "not reported");
    wgpuAdapterInfoFreeMembers(info);
    assert(wgpuInstanceHasWGSLLanguageFeature(s.instance, WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout));
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        run(&s, &cases[i], false);
        run(&s, &cases[i], true);
    }
    consumer_finish(&s);
    printf("PASS public standard uniform layout: exact4/8/12/32/64-byte bindings, final scalar lane, "
        "dynamic signed/unsigned OOB clamped to the last element, nested/attributed aggregates and narrow matrix, distinguishable "
        "typed wrong-index/layout controls, requires and early releases %s\n", adapter_class_scope());
}
