/* SPDX-License-Identifier: MIT
 * Public-ABI queue-write ordering: every interleaving of writeBuffer, submit,
 * work-done and map below must observe WebGPU queue order with exact bytes.
 * A physical run is developer evidence on the reported adapter only. */
#include "consumer.h"
#include <stdint.h>
#include <stdlib.h>

enum { WORDS = 64, BYTES = WORDS * 4 };

static void fill(uint32_t *words, uint32_t tag)
{
    for (uint32_t i = 0; i < WORDS; ++i) words[i] = tag * 1000u + i;
}

static void copy(state *s, WGPUBuffer from, WGPUBuffer to, uint64_t size)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, from, 0, to, 0, size);
    submit(s, encoder);
}

static void expect(state *s, WGPUBuffer readback, WGPUBuffer source, const uint32_t *words)
{
    copy(s, source, readback, BYTES);
    const uint32_t *got = map(s, readback, BYTES);
    for (uint32_t i = 0; i < WORDS; ++i) {
        if (got[i] != words[i]) {
            fprintf(stderr, "word %u: got %u expected %u\n", i, got[i], words[i]);
            abort();
        }
    }
    wgpuBufferUnmap(readback);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const WGPUBufferUsage rw = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer a = buffer(&s, BYTES, rw), b = buffer(&s, BYTES, rw), c = buffer(&s, BYTES, rw);
    WGPUBuffer readback = buffer(&s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer direct = buffer(&s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    uint32_t one[WORDS], two[WORDS], three[WORDS], expected_b[WORDS], expected_a[WORDS];
    fill(one, 1); fill(two, 2); fill(three, 3);
    /* write, overlapping rewrite of the same destination, then submit reads both. */
    wgpuQueueWriteBuffer(s.queue, a, 0, one, BYTES);
    wgpuQueueWriteBuffer(s.queue, a, 64, two + 16, 64);
    copy(&s, a, b, BYTES);
    memcpy(expected_b, one, BYTES);
    memcpy(expected_b + 16, two + 16, 64);
    /* A write after a submit must not reach that submit's copy. */
    wgpuQueueWriteBuffer(s.queue, a, 0, three, BYTES);
    copy(&s, a, c, BYTES);
    /* Writes into different buffers in one batch, then work done covers them. */
    wgpuQueueWriteBuffer(s.queue, b, 128, three + 32, 64);
    memcpy(expected_b + 32, three + 32, 64);
    wgpuQueueWriteBuffer(s.queue, direct, 0, two, BYTES);
    work_done(&s);
    memcpy(expected_a, three, BYTES);
    expect(&s, readback, a, expected_a);
    expect(&s, readback, b, expected_b);
    expect(&s, readback, c, three);
    /* A queue write straight into a MAP_READ buffer is visible to its map. */
    const uint32_t *got = map(&s, direct, BYTES);
    assert(!memcmp(got, two, BYTES));
    wgpuBufferUnmap(direct);
    /* Map without any work-done or submit after the write. */
    wgpuQueueWriteBuffer(s.queue, direct, 0, one, BYTES);
    got = map(&s, direct, BYTES);
    assert(!memcmp(got, one, BYTES));
    wgpuBufferUnmap(direct);
    /* A submission's copy into a just-written buffer lands after the write
     * (write-after-write inside one execution), then a dispatch-free read. */
    wgpuQueueWriteBuffer(s.queue, b, 0, one, BYTES);
    wgpuQueueWriteBuffer(s.queue, c, 0, two, BYTES);
    copy(&s, c, b, BYTES);
    expect(&s, readback, b, two);
    /* Partial write then partial overwrite by the submission, then a later
     * write: each lands in queue order around the submit. */
    wgpuQueueWriteBuffer(s.queue, a, 0, one, BYTES);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, c, 0, a, 128, 64);
    submit(&s, encoder);
    wgpuQueueWriteBuffer(s.queue, a, 0, three, 64);
    memcpy(expected_a, one, BYTES);
    memcpy(expected_a + 32, two, 64);
    memcpy(expected_a, three, 64);
    expect(&s, readback, a, expected_a);
    /* Many writes across several submissions without any observation. */
    for (uint32_t round = 0; round < 70; ++round) {
        uint32_t words[WORDS];
        fill(words, 100 + round);
        wgpuQueueWriteBuffer(s.queue, round % 2 ? b : c, 0, words, BYTES);
        if (round % 7 == 6) copy(&s, round % 2 ? b : c, a, BYTES);
    }
    fill(expected_a, 100 + 69);
    expect(&s, readback, b, expected_a);
    fill(expected_a, 100 + 68);
    expect(&s, readback, c, expected_a);
    fill(expected_a, 100 + 69); /* Round 69: b copied into a after its write. */
    expect(&s, readback, a, expected_a);
    /* Release with an open, never-observed write batch must still tear down. */
    wgpuQueueWriteBuffer(s.queue, c, 0, one, BYTES);
    assert(!s.errors);
    wgpuBufferRelease(a); wgpuBufferRelease(b); wgpuBufferRelease(c);
    wgpuBufferRelease(readback); wgpuBufferRelease(direct);
    consumer_finish(&s);
    puts("PASS public queue-write ordering: overlapping same-buffer writes, write after submit, cross-buffer batch, work done, direct MAP_READ write/map, submission copies over written bytes, 70 writes across submissions and teardown with an open batch");
    return 0;
}
