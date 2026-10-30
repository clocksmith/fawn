// Diagnostic microprobe (not product code): per-submit host cost of the native
// Metal call sequences Doe and Dawn use for one 1 MiB buffer copy + readback.
// Each variant: 400 iterations, first 20 discarded, completion waited outside
// the timed interval. Prints median microseconds of encode+commit per variant.
#import <Metal/Metal.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
static double us(uint64_t t) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return (double)t * tb.numer / tb.denom / 1000.0; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double median(double *v, int n) { qsort(v, n, sizeof(double), cmp); return v[n / 2]; }
enum { N = 400, SKIP = 20 };
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLSharedEvent> ev = [dev newSharedEvent];
        const NSUInteger size = 1 << 20;
        id<MTLBuffer> srcShared = [dev newBufferWithLength:size options:MTLResourceStorageModeShared];
        id<MTLBuffer> srcPrivate = [dev newBufferWithLength:size options:MTLResourceStorageModePrivate];
        id<MTLBuffer> dst = [dev newBufferWithLength:size options:MTLResourceStorageModeShared];
        uint64_t value = 0;
        const char *names[] = {"doe: unretained cb, pool per call, shared src, signal+commit",
                               "dawn: retained cb (pre-made), shared src, handlers+signal+commit",
                               "doe-shape with retained cb", "doe-shape without per-call pools",
                               "doe-shape, private src", "commit only (empty unretained cb + signal)"};
        for (int variant = 0; variant < 6; ++variant) {
            double t[N];
            id<MTLCommandBuffer> next = [[q commandBuffer] retain];
            for (int i = 0; i < N; ++i) {
                id<MTLCommandBuffer> cb = nil;
                uint64_t a = mach_absolute_time();
                if (variant == 1) {
                    @autoreleasepool {
                        cb = next;
                        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                        [blit copyFromBuffer:srcShared sourceOffset:0 toBuffer:dst destinationOffset:0 size:size];
                        [blit endEncoding];
                        [cb addScheduledHandler:^(id<MTLCommandBuffer> c) { (void)c; }];
                        [cb addCompletedHandler:^(id<MTLCommandBuffer> c) { (void)c; }];
                        [cb encodeSignalEvent:ev value:++value];
                        [cb commit];
                        next = [[q commandBuffer] retain];
                    }
                } else if (variant == 5) {
                    @autoreleasepool { cb = [[q commandBufferWithUnretainedReferences] retain]; }
                    @autoreleasepool { [cb encodeSignalEvent:ev value:++value]; [cb commit]; }
                } else {
                    BOOL pools = variant != 3;
                    id<MTLBuffer> src = variant == 4 ? srcPrivate : srcShared;
                    if (pools) {
                        @autoreleasepool { id<MTLCommandBuffer> made = variant == 2 ? [q commandBuffer] : [q commandBufferWithUnretainedReferences]; cb = [made retain]; }
                        @autoreleasepool {
                            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                            [blit copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0 size:size];
                            [blit endEncoding];
                        }
                        @autoreleasepool { [cb encodeSignalEvent:ev value:++value]; [cb commit]; }
                    } else {
                        cb = [[q commandBufferWithUnretainedReferences] retain];
                        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                        [blit copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0 size:size];
                        [blit endEncoding];
                        [cb encodeSignalEvent:ev value:++value];
                        [cb commit];
                    }
                }
                uint64_t b = mach_absolute_time();
                t[i] = us(b - a);
                [cb waitUntilCompleted];
                [cb release];
            }
            [next release];
            printf("%-66s median %.2f us\n", names[variant], median(t + SKIP, N - SKIP));
        }
    }
    return 0;
}
