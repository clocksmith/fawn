#import <Metal/Metal.h>
#include <stdio.h>
int main(void) {
    NSArray *a = MTLCopyAllDevices(), *b = MTLCopyAllDevices();
    id<MTLDevice> d = MTLCreateSystemDefaultDevice();
    printf("count %lu same-across-copies %d same-as-default %d registryID %llu default-registryID %llu lowPower %d headless %d removable %d\n",
        (unsigned long)a.count, a[0] == b[0], a[0] == d, (unsigned long long)[a[0] registryID], (unsigned long long)d.registryID,
        [a[0] isLowPower], [a[0] isHeadless], [a[0] isRemovable]);
    return 0;
}
