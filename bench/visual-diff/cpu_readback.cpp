#include <IOSurface/IOSurface.h>
#include <cstdint>
#include <cstring>
#include <vector>
extern "C" int prepare_comparison() { return 0; }
extern "C" int64_t compare_surfaces(IOSurfaceRef a, IOSurfaceRef b) {
    std::vector<uint8_t> bytes[2];
    IOSurfaceRef sources[] = {a, b};
    for (int i = 0; i < 2; ++i) {
        bytes[i].resize(512 * 256 * 4);
        if (IOSurfaceLock(sources[i], kIOSurfaceLockReadOnly, nullptr)) return -1;
        const auto* src = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(sources[i]));
        const auto stride = IOSurfaceGetBytesPerRow(sources[i]);
        for (int y = 0; y < 256; ++y)
            std::memcpy(bytes[i].data() + y * 512 * 4, src + y * stride, 512 * 4);
        if (IOSurfaceUnlock(sources[i], kIOSurfaceLockReadOnly, nullptr)) return -1;
    }
    uint32_t mask = 0;
    for (int y = 0; y < 256; ++y) for (int x = 0; x < 512; x += 64) {
        const size_t offset = (y * 512 + x) * 4;
        if (std::memcmp(bytes[0].data()+offset, bytes[1].data()+offset, 64*4))
            mask |= 1u << (y/64*8+x/64);
    }
    return mask;
}
