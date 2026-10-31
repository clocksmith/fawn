// DIAGNOSTIC ONLY: does reading GPU-written shared memory directly (lend) vs via a
// malloc shadow change the next CPU upload's source-read cost? Mirrors the image
// workload's buffer sizes and order without WebGPU.
#import <Metal/Metal.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile uint64_t sink;
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    @autoreleasepool {
    int mode = atoi(argv[1]); int iters = 96; size_t n = 4u << 20;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLBuffer> A = [dev newBufferWithLength:n options:MTLResourceStorageModeShared];
    id<MTLBuffer> O = [dev newBufferWithLength:n options:MTLResourceStorageModeShared];
    id<MTLBuffer> R = [dev newBufferWithLength:n options:MTLResourceStorageModeShared];
    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nkernel void inv(device const uint *a [[buffer(0)]], device uint *o [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = ~a[i]; }" options:nil error:&err];
    id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"inv"] error:&err];
    int compute = argc > 2 && atoi(argv[2]);
    unsigned char *input[2] = {malloc(n), malloc(n)}, *host = malloc(n), *expect = malloc(n);
    for (size_t i = 0; i < n; ++i) { input[0][i] = (unsigned char)i; input[1][i] = (unsigned char)(i * 7); expect[i] = (unsigned char)~i; }
    double src[96], cpy[96];
    for (int it = 0; it < iters; ++it) {
        int s = it & 1;
        uint64_t t0 = mach_absolute_time(); uint64_t x = 0;
        for (size_t i = 0; i < n; i += 64) x += *(const uint64_t *)(input[s] + i);
        uint64_t t1 = mach_absolute_time();
        memmove([A contents], input[s], n);
        uint64_t t2 = mach_absolute_time(); sink = x;
        src[it] = (double)(t1 - t0) / 24.0; cpy[it] = (double)(t2 - t1) / 24.0;
        id<MTLCommandBuffer> cb = [q commandBufferWithUnretainedReferences];
        if (compute) {
            id<MTLBlitCommandEncoder> f = [cb blitCommandEncoder]; [f fillBuffer:O range:NSMakeRange(0, n) value:0]; [f endEncoding];
            id<MTLComputeCommandEncoder> c = [cb computeCommandEncoder]; [c setComputePipelineState:pso];
            [c setBuffer:A offset:0 atIndex:0]; [c setBuffer:O offset:0 atIndex:1];
            [c dispatchThreadgroups:MTLSizeMake(n / 4 / 64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)]; [c endEncoding];
            id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder]; [b copyFromBuffer:O sourceOffset:0 toBuffer:R destinationOffset:0 size:n]; [b endEncoding];
        } else {
        id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
        [b fillBuffer:O range:NSMakeRange(0, n) value:0];
        [b copyFromBuffer:A sourceOffset:0 toBuffer:O destinationOffset:0 size:n];
        [b copyFromBuffer:O sourceOffset:0 toBuffer:R destinationOffset:0 size:n];
        [b endEncoding];
        } [cb commit]; [cb waitUntilCompleted];
        const unsigned char *mapped = [R contents];
        unsigned char *shadow = NULL;
        if (mode == 1) { shadow = malloc(n); memmove(shadow, mapped, n); mapped = shadow; }
        memcpy(host, mapped, n);
        free(shadow);
        uint64_t y = 0; for (size_t i = 0; i < n; i += 8) y += *(const uint64_t *)(host + i) ^ *(const uint64_t *)(expect + i); sink = y;
    }
    qsort(src + 1, iters - 1, sizeof(double), cmp); qsort(cpy + 1, iters - 1, sizeof(double), cmp);
    printf("mode %d srcread %.2f copy %.2f\n", mode, src[1 + (iters - 1) / 2], cpy[1 + (iters - 1) / 2]);
    }
    return 0;
}
