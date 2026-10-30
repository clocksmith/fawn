/* Diagnostic: fresh-process cost of the setup steps Doe performs on Metal. */
#define _DARWIN_C_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <time.h>
static double ms(void) { struct timespec t; clock_gettime(CLOCK_UPTIME_RAW, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(void) {
    double t0 = ms();
    void *objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW | RTLD_LOCAL);
    double t1 = ms();
    void *foundation = dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_NOW | RTLD_LOCAL);
    double t2 = ms();
    void *metal = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_NOW | RTLD_LOCAL);
    double t3 = ms();
    void *ios = dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface", RTLD_NOW | RTLD_LOCAL);
    void *cv = dlopen("/System/Library/Frameworks/CoreVideo.framework/CoreVideo", RTLD_NOW | RTLD_LOCAL);
    double t4 = ms();
    void *(*create)(void) = (void *(*)(void))dlsym(metal, "MTLCreateSystemDefaultDevice");
    void *device = create();
    double t5 = ms();
    printf("objc %.2f foundation %.2f metal %.2f iosurface+corevideo %.2f MTLCreateSystemDefaultDevice %.2f total %.2f ms (%p %p %p %p %p)\n",
        t1 - t0, t2 - t1, t3 - t2, t4 - t3, t5 - t4, t5 - t0, objc, foundation, ios, cv, device);
    return 0;
}
