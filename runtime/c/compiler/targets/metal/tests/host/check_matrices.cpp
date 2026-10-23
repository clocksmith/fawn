/* SPDX-License-Identifier: MIT */
#define DOE_METAL_MATRIX_CONTROL
#include <cassert>
#include <cstdio>
#include "matrices.metal"
int main() {
    alignas(16) float output[18];
    for (auto &v : output) v = -999.0f;
    alignas(16) float input[8] = {1, 2, 3, 4, 5, 6, 0, 991};
    alignas(16) float matrices[8];
    for (auto &v : matrices) v = -777.0f;
    alignas(16) uchar padded[48], padded_output[32];
    std::memset(padded, 0x6a, sizeof(padded));
    std::memset(padded_output, 0x5a, sizeof(padded_output));
    const float padded_values[] = {7, 8, 9, 10, 11, 12};
    for (unsigned c = 0; c < 2; ++c)
        std::memcpy(padded + 16 * c, padded_values + 3 * c, 12);
    uint sizes[31] = {68, 28, 24, 48, 32};
    doe_entry0(reinterpret_cast<uchar*>(output), reinterpret_cast<const uchar*>(input),
        reinterpret_cast<uchar*>(matrices), padded, padded_output, sizes);
    const float expected[] = {22, 28, 8, 18, 28, 35, 44, 56, -1, 0, 0, 1, 9, 12, 4, 6, 15};
    for (unsigned i = 0; i < 17; ++i) {
        if (output[i] != expected[i])
            std::printf("mismatch%u got%g expected%g\n", i, output[i], expected[i]);
        assert(output[i] == expected[i]);
    }
    for (unsigned i = 0; i < 6; ++i) assert(matrices[i] == input[i]);
    assert(output[17] == -999.0f && matrices[6] == -777.0f && matrices[7] == -777.0f && input[7] == 991);
    for (unsigned c = 0; c < 2; ++c) {
        assert(std::memcmp(padded_output + 16 * c, padded_values + 3 * c, 12) == 0);
        for (unsigned b = 12; b < 16; ++b) assert(padded_output[16 * c + b] == 0x5a);
    }
    std::puts("Emitted matrix operation control passed: products, stride, zero and padding sentinels.");
}
