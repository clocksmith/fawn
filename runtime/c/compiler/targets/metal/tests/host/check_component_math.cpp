/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <cassert>
#include <cstdio>
#include "metal_stdlib"
namespace metal {
inline float rsqrt(float x) { return 1.0f / std::sqrt(x); }
inline float mix(float x, float y, float a) { return x * (1.0f - a) + y * a; }
template<unsigned N> host_vector<float, N> mix(const host_vector<float, N>& x,
        const host_vector<float, N>& y, const host_vector<float, N>& a) {
    host_vector<float, N> result;
    for (unsigned i = 0; i < N; ++i) result[i] = mix(x[i], y[i], a[i]);
    return result;
}
}
#include "component_math.metal"
#include "../../../tests/component_math_source.h"
int main() {
    alignas(16) uint words[33] = {0xbe800000u};
    words[32] = 0x12345678u;
    uint sizes[31] = {128u};
    doe_entry0(reinterpret_cast<uchar*>(words), sizes);
    for (unsigned i = 0; i < sizeof(component_math_expected) / sizeof(*component_math_expected); ++i)
        assert(words[i] == component_math_expected[i]);
    assert(words[29] == 2u && words[30] == 2u && words[31] == 2u);
    assert(words[32] == 0x12345678u);
    std::puts("Metal emitted operations: exact rounding, fractional tail, minimum abs and once-only effects passed.");
}
