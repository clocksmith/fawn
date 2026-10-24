/* SPDX-License-Identifier: MIT
 * Address spaces and attributes erased: shared-helper size identity control,
 * not MSL SDK validation, GPU emulation or physical qualification. */
#include <cassert>
#include "buffers.metal"
int main() {
    uint prefix[1] = {10}, shared[3] = {};
    uint first_sizes[2] = {4,8};
    doe_entry0(reinterpret_cast<const uchar *>(prefix), reinterpret_cast<uchar *>(shared), first_sizes);
    assert(shared[0] == 12);
    uint second_sizes[1] = {12};
    doe_entry1(reinterpret_cast<uchar *>(shared), second_sizes);
    assert(shared[0] == 3);
    uint wrong_sizes[2] = {8,4};
    doe_entry0(reinterpret_cast<const uchar *>(prefix), reinterpret_cast<uchar *>(shared), wrong_sizes);
    assert(shared[0] == 11 && shared[0] != 12);
}
