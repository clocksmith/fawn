/* SPDX-License-Identifier: MIT
 * Uses the existing mechanical Metal scalar shim, not an SDK or GPU model. */
#include <cassert>
#include <cstdio>
#include "switch_control.metal"
int main() {
    alignas(16) uint words[16] = {};
    uint sizes[31] = {};
    sizes[0] = sizeof(words);
    for (uint lane = 0; lane < 8; ++lane)
        doe_entry0(reinterpret_cast<uchar*>(words), sizes, uint3(lane, 0, 0));
    const uint expected[16] = {11,11,23,8,99,99,99,99,1,1,1,1,1,1,1,1};
    for (unsigned i = 0; i < 16; ++i) assert(words[i] == expected[i]);
    std::puts("Metal switch host control: exact16words, nested break/continue, selector once; not SDK/GPU qualification.");
}
