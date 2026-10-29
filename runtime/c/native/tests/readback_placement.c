/* SPDX-License-Identifier: MIT
 * Physical/public correctness, independent of the native heap representation. */
#include "consumer.h"

static void map_exact(state *s, WGPUBuffer target, const uint32_t expected[7])
{
    unsigned before = s->mapped;
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=mapped,.userdata1=s};
    assert(wgpuBufferMapAsync(target, WGPUMapMode_Read, 0, 28, callback).id);
    assert(s->mapped == before + 1);
    const void *bytes = wgpuBufferGetConstMappedRange(target, 0, 28);
    assert(bytes && !memcmp(bytes, expected, 28));
    wgpuBufferUnmap(target);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    const uint32_t zero[7] = {0};
    const uint32_t input[4] = {0x10203040, 0x7fffffff, 0xabcdef01, 0xfedcba98};
    const uint32_t copied[7] = {0, 0x10203040, 0x7fffffff, 0xabcdef01, 0xfedcba98, 0, 0};
    WGPUBuffer readback = buffer(&s, 28, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    map_exact(&s, readback, zero);
    WGPUBuffer source = buffer(&s, 16, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue, source, 0, input, sizeof(input));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 4, 16);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferRelease(source);
    wgpuQueueSubmit(s.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    map_exact(&s, readback, copied);
    wgpuBufferRelease(readback);
    WGPUBufferDescriptor descriptor = {.size=28,
        .usage=WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,.mappedAtCreation=WGPU_TRUE};
    WGPUBuffer initially_mapped = wgpuDeviceCreateBuffer(s.device, &descriptor);
    assert(initially_mapped);
    void *initial = wgpuBufferGetMappedRange(initially_mapped, 0, 28);
    assert(initial && !memcmp(initial, zero, sizeof(zero)));
    memcpy(initial, copied, sizeof(copied));
    wgpuBufferUnmap(initially_mapped);
    map_exact(&s, initially_mapped, copied);
    wgpuBufferRelease(initially_mapped);
    consumer_finish(&s);
    printf("PASS public readback placement: initial zeros, partial GPU copy with zero prefix/suffix, early source release, and mappedAtCreation writeback preserved %s\n", adapter_class_scope());
    return 0;
}
