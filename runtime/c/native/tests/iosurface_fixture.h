/* SPDX-License-Identifier: MIT */
#ifndef DOE_TEST_IOSURFACE_FIXTURE_H
#define DOE_TEST_IOSURFACE_FIXTURE_H
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

static inline IOSurfaceRef doe_test_iosurface_create(uint32_t format)
{
    const void *keys[] = {kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
    int values[] = {2,2,4,0};
    memcpy(&values[3],&format,sizeof(format));
    CFNumberRef numbers[4];
    const void *objects[4];
    for (size_t i = 0; i < 4; ++i) {
        numbers[i] = CFNumberCreate(NULL,kCFNumberIntType,&values[i]);
        assert(numbers[i]);
        objects[i] = numbers[i];
    }
    CFDictionaryRef properties = CFDictionaryCreate(NULL,keys,objects,4,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    assert(properties);
    IOSurfaceRef surface = IOSurfaceCreate(properties);
    CFRelease(properties);
    for (size_t i = 0; i < 4; ++i) CFRelease(numbers[i]);
    return surface;
}
#endif
