/* Exercise two live native queues with the same user label. The independent
 * collector must attribute each submit to its actual queue and never to a page. */
#include "consumer.h"

extern void FawnSubmitResidentDocumentCommandV1(WGPUProcQueueSubmit, WGPUQueue,
    size_t, const WGPUCommandBuffer *, const char *, const char *);

static void check(state *s, uint32_t initial) {
    const uint32_t sizeof_data = 4 * sizeof(uint32_t);
    WGPUBuffer data = buffer(s, sizeof_data, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer out = buffer(s, sizeof_data, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUComputePipeline pipe = pipeline(s, "@group(0) @binding(0) var<storage,read_write> x:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) i:vec3<u32>){x[i.x]=x[i.x]+i.x+7u;}");
    uint32_t seed[4] = {initial, initial, initial, initial};
    wgpuQueueWriteBuffer(s->queue, data, 0, seed, sizeof_data);
    WGPUBindGroupEntry entry = {.binding=0, .buffer=data, .size=sizeof_data};
    WGPUBindGroup bind_group = group(s, pipe, &entry, 1);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    dispatch(enc, pipe, bind_group);
    wgpuCommandEncoderCopyBufferToBuffer(enc, data, 0, out, 0, sizeof_data);
    WGPUCommandBufferDescriptor desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    desc.label = (WGPUStringView){"same-page-controlled-label", WGPU_STRLEN};
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(enc, &desc);
    assert(commands);
    /* Both commands deliberately have the same page-controlled label. The
     * document and command below stand in for the two browser-minted IDs. */
    const char *document = initial == 11 ? "10000000000000000000000000000001" : "10000000000000000000000000000002";
    const char *command = initial == 11 ? "20000000000000000000000000000001" : "20000000000000000000000000000002";
    FawnSubmitResidentDocumentCommandV1(wgpuQueueSubmit, s->queue, 1, &commands, document, command);
    const uint32_t *words = map(s, out, sizeof_data);
    for (uint32_t i = 0; i < 4; ++i) assert(words[i] == initial + i + 7u);
    wgpuBufferUnmap(out);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(enc);
    wgpuBindGroupRelease(bind_group);
    wgpuComputePipelineRelease(pipe);
    wgpuBufferRelease(out);
    wgpuBufferRelease(data);
}

int main(void) {
    state first = {0}, second = {0};
    assert(!adapter_class_physical());
    consumer_init(&first);
    consumer_init(&second);
    assert(first.queue != second.queue);
    check(&first, 11); check(&second, 30);
    consumer_finish(&second); consumer_finish(&first);
    puts("Two distinct native queues returned all eight expected words");
    return 0;
}
