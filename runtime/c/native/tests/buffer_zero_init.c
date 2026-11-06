/* SPDX-License-Identifier: MIT
 * Metal buffer creation writes no contents: newBufferWithLength:options:
 * returns zero-filled memory. After dirty allocations of the same sizes are
 * written by the host and by GPU copies and then destroyed, every fresh buffer
 * reads zero when mapped directly, through a GPU copy, and around a partial
 * write. */
#include "consumer.h"

enum { ROUNDS = 48 };
static const uint64_t SIZES[] = {256, 4096, 65536, UINT64_C(1) << 20, UINT64_C(4) << 20};

static void expect_zero(const unsigned char *bytes, uint64_t begin, uint64_t end)
{
    for (uint64_t i = begin; i < end; ++i) assert(bytes[i] == 0);
}

static void copy_whole(state *s, WGPUBuffer from, WGPUBuffer to, uint64_t size)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, from, 0, to, 0, size);
    submit(s, encoder);
    work_done(s);
}

static void retire(WGPUBuffer buffer)
{
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
}

/* Leaves recently freed allocations of this size holding nonzero bytes. */
static void dirty(state *s, uint64_t size, const unsigned char *fill)
{
    WGPUBuffer source = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);
    WGPUBuffer target = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s->queue, source, 0, fill, (size_t)size);
    copy_whole(s, source, target, size);
    const unsigned char *bytes = map(s, target, (size_t)size);
    assert(!memcmp(bytes, fill, (size_t)size));
    wgpuBufferUnmap(target);
    retire(source);
    retire(target);
}

static void fresh(state *s, uint64_t size)
{
    WGPUBuffer mapped_only = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    expect_zero(map(s, mapped_only, (size_t)size), 0, size);
    wgpuBufferUnmap(mapped_only);
    retire(mapped_only);

    const WGPUBufferUsage gpu = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage |
        WGPUBufferUsage_Index | WGPUBufferUsage_Vertex | WGPUBufferUsage_Uniform;
    WGPUBuffer untouched = buffer(s, size, gpu);
    WGPUBuffer readback = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    copy_whole(s, untouched, readback, size);
    expect_zero(map(s, readback, (size_t)size), 0, size);
    wgpuBufferUnmap(readback);
    retire(readback);

    const uint32_t marker = 0x5a5a5a5au;
    const uint64_t middle = size / 2;
    WGPUBuffer partial = buffer(s, size, gpu);
    readback = buffer(s, size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s->queue, partial, middle, &marker, sizeof(marker));
    copy_whole(s, partial, readback, size);
    const unsigned char *bytes = map(s, readback, (size_t)size);
    expect_zero(bytes, 0, middle);
    assert(!memcmp(bytes + middle, &marker, sizeof(marker)));
    expect_zero(bytes, middle + sizeof(marker), size);
    wgpuBufferUnmap(readback);
    retire(readback);
    retire(partial);
    retire(untouched);
}

int main(void)
{
    consumer_line_output();
    state s = {0};
    consumer_init(&s);
    const uint64_t largest = SIZES[sizeof(SIZES) / sizeof(SIZES[0]) - 1];
    unsigned char *fill = malloc((size_t)largest);
    assert(fill);
    for (uint64_t i = 0; i < largest; ++i) fill[i] = (unsigned char)((i * 31u + 0xa5u) | 1u);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); ++i) {
            dirty(&s, SIZES[i], fill);
            fresh(&s, SIZES[i]);
        }
    }
    free(fill);
    consumer_finish(&s);
    printf("PASS buffer zero initialization without creation writes: %u rounds of dirty reuse, mapped, copied and partially written reads %s\n",
        (unsigned)ROUNDS, adapter_class_scope());
    return 0;
}
