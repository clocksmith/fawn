/* SPDX-License-Identifier: MIT
 * Public-ABI buffer creation zeros across memory reuse: every new buffer reads
 * zero, whether its memory is fresh or was written and released by an earlier
 * buffer of the same size and usage. A physical run is developer evidence on
 * the reported adapter only. */
#include "consumer.h"
#include <stdint.h>
#include <stdlib.h>

static void expect_zero(const uint8_t *bytes, size_t size, const char *what, unsigned round)
{
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i]) {
            fprintf(stderr, "%s round %u byte %zu: %u\n", what, round, i, bytes[i]);
            abort();
        }
    }
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    static const size_t sizes[] = {256, 65536, 1048576, 1048576 + 4096};
    uint8_t *pattern = malloc(1048576 + 4096);
    assert(pattern);
    memset(pattern, 0xab, 1048576 + 4096);
    WGPUBuffer readback = buffer(&s, 1048576 + 4096, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    for (unsigned round = 0; round < 3; ++round) {
        for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
            const size_t size = sizes[k];
            /* A storage buffer: zero when created, then dirtied and released. */
            WGPUBuffer storage = buffer(&s, size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, storage, 0, readback, 0, size);
            submit(&s, encoder);
            expect_zero(map(&s, readback, size), size, "storage", round);
            wgpuBufferUnmap(readback);
            wgpuQueueWriteBuffer(s.queue, storage, 0, pattern, size);
            work_done(&s);
            wgpuBufferRelease(storage);
            /* A mappable readback buffer: zero when created, dirtied by a copy. */
            WGPUBuffer mapped = buffer(&s, size, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
            expect_zero(map(&s, mapped, size), size, "readback", round);
            wgpuBufferUnmap(mapped);
            wgpuQueueWriteBuffer(s.queue, mapped, 0, pattern, size);
            work_done(&s);
            wgpuBufferRelease(mapped);
        }
    }
    free(pattern);
    wgpuBufferRelease(readback);
    assert(!s.errors);
    consumer_finish(&s);
    printf("PASS public zeroed creation across reuse: storage and mappable buffers of 256 B, 64 KiB, 1 MiB and 1 MiB + 4 KiB read zero when new, three rounds of dirty-and-release %s\n", adapter_class_scope());
    return 0;
}
