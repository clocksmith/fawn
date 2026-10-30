/* Diagnostic: device creation when Metal is linked at load time (Dawn's arrangement). */
#import <Metal/Metal.h>
#include <stdio.h>
#include <time.h>
static double ms(void) { struct timespec t; clock_gettime(CLOCK_UPTIME_RAW, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(void) {
    double t0 = ms();
    NSArray *all = MTLCopyAllDevices();
    double t1 = ms();
    id<MTLDevice> d = MTLCreateSystemDefaultDevice();
    double t2 = ms();
    printf("linked: MTLCopyAllDevices %.2f then MTLCreateSystemDefaultDevice %.2f ms (%lu %p)\n", t1 - t0, t2 - t1, (unsigned long)all.count, d);
    return 0;
}
