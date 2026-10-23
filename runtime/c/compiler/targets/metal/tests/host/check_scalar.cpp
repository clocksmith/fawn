/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <cassert>
#include <climits>
#include <cstdio>
#include "control.metal"
int main() {
    alignas(16) int values[9] = {};
    uint sizes[31] = {};
    sizes[0] = 7u * sizeof(int);
    values[7] = 12345;
    values[8] = 67890;
    doe_entry0(reinterpret_cast<uchar*>(values), sizes);
    assert(values[0] == 2);
    assert(values[1] == 12);
    assert(values[2] == INT_MIN);
    assert(values[3] == INT_MIN);
    assert(values[4] == -7);
    assert(values[5] == 0);
    assert(values[6] == 6);
    assert(values[7] == 12345 && values[8] == 67890);
    /* Smaller runtime view must preserve every word beyond the selected range. */
    for (int& value : values) value = 0;
    sizes[0] = 3u * sizeof(int);
    doe_entry0(reinterpret_cast<uchar*>(values), sizes);
    assert(values[0] == 2 && values[1] == 12 && values[2] == INT_MIN);
    for (unsigned i = 3u; i < 9u; ++i) assert(values[i] == 0);
    std::puts("Emitted scalar host control passed: continuing/effects/wrapping/robustness.");
}
