/* SPDX-License-Identifier: MIT */
int main() {
    uint output[18], matrices[8], padded[8];
    std::fill(std::begin(output), std::end(output), asuint(-999.0f));
    std::fill(std::begin(matrices), std::end(matrices), asuint(-777.0f));
    std::fill(std::begin(padded), std::end(padded), 0x5a5a5a5au);
    for (unsigned i = 0; i < 6; ++i) r1_data[i / 4][i % 4] = asuint(float(i + 1));
    const float padded_values[] = {7, 8, 9, 10, 11, 12};
    for (unsigned r = 0; r < 2; ++r) {
        for (unsigned c = 0; c < 3; ++c) r3_data[r][c] = asuint(padded_values[r * 3 + c]);
        r3_data[r][3] = 0x6a6a6a6au;
    }
    r0 = output; r0_count = 17;
    r2 = matrices; r2_count = 6;
    r4 = padded; r4_count = 8;
    matrix_control();
    const float expected[] = {22, 28, 8, 18, 28, 35, 44, 56, -1, 0, 0, 1, 9, 12, 4, 6, 15};
    for (unsigned i = 0; i < 17; ++i) assert(asfloat(output[i]) == expected[i]);
    for (unsigned i = 0; i < 6; ++i) assert(asfloat(matrices[i]) == float(i + 1));
    assert(asfloat(output[17]) == -999.0f && asfloat(matrices[6]) == -777.0f);
    assert(asfloat(matrices[7]) == -777.0f);
    for (unsigned r = 0; r < 2; ++r) {
        for (unsigned c = 0; c < 3; ++c) assert(asfloat(padded[r * 4 + c]) == padded_values[r * 3 + c]);
        assert(padded[r * 4 + 3] == 0x5a5a5a5au && r3_data[r][3] == 0x6a6a6a6au);
    }
    std::puts("HLSL matrix host control: reversed mul, transpose, determinant, OOB zero and stride/padding sentinels.");
}
