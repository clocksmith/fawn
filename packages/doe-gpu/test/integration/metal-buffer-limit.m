#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 1;
        printf("%llu\n", (unsigned long long)device.maxBufferLength);
    }
    return 0;
}
