/* SPDX-License-Identifier: MIT
 * Host operation control, not Metal SDK or GPU qualification. */
#include <cassert>
#include <cstdio>
#include "text_control.metal"
int main() {
    alignas(16) uint values[6] = {99, 99, 99, 99, 12345, 67890};
    const uint sizes[1] = {16};
    doe_entry0(reinterpret_cast<uchar *>(values), sizes);
    assert(values[0] == 42 && values[1] == 4 && values[2] == 0 && values[3] == 6);
    assert(values[4] == 12345 && values[5] == 67890);
    std::puts("Public ABI10 emitted C++ operation control passed helper calls, loops, array length and OOB guards");
}
