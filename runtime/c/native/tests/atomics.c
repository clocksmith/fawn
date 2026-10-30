/* SPDX-License-Identifier: MIT
 * Genuine ABI9 public composition, not a native atomic type reinterpretation. */
#include "consumer.h"
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUComputePipeline program = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> cells:array<atomic<u32>>;"
        "@group(0) @binding(1) var<storage,read_write> results:array<u32>;"
        "var<workgroup> workgroup_cells:array<atomic<u32>,4>;"
        "@compute @workgroup_size(6) fn main(@builtin(local_invocation_index) lane:u32,"
        "@builtin(workgroup_id) group:vec3u){let index=i32(lane)-1;let at=group.x*6u+lane;"
        "results[at]=atomicAdd(&workgroup_cells[index],1u);"
        "let result=atomicCompareExchangeWeak(&workgroup_cells[index],1u,7u);"
        "results[at+12u]=result.old_value+16u*u32(result.exchanged);atomicAdd(&cells[index],1u);}");
    WGPUBuffer cells = buffer(&s,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer results = buffer(&s,96,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s,112,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[2] = {{.binding=0,.buffer=cells,.size=16},{.binding=1,.buffer=results,.size=96}};
    WGPUBindGroup bindings = group(&s,program,entries,2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,2,1,1);
    wgpuComputePassEncoderDispatchWorkgroups(pass,2,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,cells,0,readback,0,16);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,results,0,readback,16,96);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    wgpuBufferRelease(cells);
    wgpuBufferRelease(results);
    submit(&s,encoder);
    const uint32_t *words = map(&s,readback,112);
    /* Storage out-of-range handling is the selected target's compiler contract.
     * HLSL clamps (compiler/targets/hlsl/AGENTS.md): lanes -1 and 4 act on
     * cells[3], so each of the four workgroups adds 1 to cells[0..2] and 3 to
     * cells[3]. SPIR-V and MSL keep the access guard (compiler/src/spirv.c
     * access_condition): the out-of-range atomics are dropped and every cell gets
     * 4. Workgroup indices keep their guard on every target, so results below
     * still see dropped out-of-range operations. */
    const bool clamps = adapter_class_backend() == WGPUBackendType_D3D12;
    for (size_t i = 0; i < 4; ++i) assert(words[i] == (clamps && i == 3 ? 12u : 4u));
    for (size_t i = 0; i < 12; ++i) {
        assert(words[4+i] == 0);
        assert(words[16+i] == (i%6 > 0 && i%6 < 5 ? 17u : 0u));
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public ABI9 atomics: storage accumulation, signed OOB suppression, compare-exchange result identity, fresh workgroup initialization across two groups and two dispatches, early releases %s\n", adapter_class_scope());
    return 0;
}
