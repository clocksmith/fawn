/* SPDX-License-Identifier: MIT
 * Execute emitted size arithmetic only, not Metal SDK or a device. */
#include <cassert>
#include <cstdio>
#include "array_length.metal"
int main() {
    alignas(16) unsigned output[3] = {0,0,0x12345678};
    uint sizes[31] = {};
    sizes[1] = 8;
    const uint bytes[] = {0,12,16,28,32,64,0xffffffffu};
    const uint expected[] = {0,0,0,0,1,3,268435454};
    for (unsigned i=0; i<7; ++i) {
        sizes[0] = bytes[i];
        doe_entry0(nullptr,reinterpret_cast<uchar*>(output),sizes);
        assert(output[0]==expected[i] && output[1]==2 && output[2]==0x12345678);
    }
    std::puts("Metal runtime array length: typed tail prefix, stride, partial elements and size-slot identity.");
}
