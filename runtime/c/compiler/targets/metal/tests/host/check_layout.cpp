/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <cassert>
#include <cstdio>
#include <cstring>
#include "aggregate.metal"
int main() {
    alignas(16) int output[3] = {0, 1, 2345};
    alignas(16) unsigned char data[32];
    std::memset(data, 0x5a, sizeof(data));
    const uint fields[] = {2u, 5u, 7u, 9u, 13u};
    const unsigned offsets[] = {0u, 4u, 20u, 24u, 28u};
    for (unsigned i = 0u; i < 5u; ++i) std::memcpy(data + offsets[i], &fields[i], sizeof(uint));
    uint sizes[31] = {};
    sizes[0] = 2u * sizeof(int);
    sizes[1] = sizeof(data);
    doe_entry0(reinterpret_cast<uchar*>(output), data, sizes);
    assert(output[0] == 20 && output[1] == 0 && output[2] == 2345);
    const uint expected[] = {5u, 5u, 7u, 8u, 13u};
    for (unsigned i = 0u; i < 5u; ++i) {
        uint value;
        std::memcpy(&value, data + offsets[i], sizeof(value));
        assert(value == expected[i]);
    }
    for (unsigned i = 8u; i < 20u; ++i) assert(data[i] == 0x5au);
    std::puts("Emitted aggregate host control passed: typed copies, explicit layout, padding preservation.");
}
