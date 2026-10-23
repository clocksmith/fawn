/* SPDX-License-Identifier: MIT
 * Emitted MSL scalar/layout control; address spaces/entry attributes erased.
 * This establishes neither MSL SDK acceptance nor GPU execution. */
#include <cassert>
#include <cstring>
#include "nested.metal"
int main() {
    alignas(16) const uint input[16] = {3,5,7,11,13,17,19,23,29,0xeeeeeeeeu,31,37,0xaaaaaaaau,0xbbbbbbbbu,41,0xccccccccu};
    alignas(16) uint output[4] = {};
    doe_entry0(reinterpret_cast<const uchar *>(input), reinterpret_cast<uchar *>(output), uint3(0));
    assert(output[0] == 167 && output[1] == 0 && output[2] == 23 && output[3] == 41);
    alignas(16) uint wrong[16];
    std::memcpy(wrong, input, sizeof(input));
    wrong[3] = input[4]; /* Deliberately wrong nested-struct byte offset. */
    doe_entry0(reinterpret_cast<const uchar *>(wrong), reinterpret_cast<uchar *>(output), uint3(0));
    assert(output[0] != 167);
}
