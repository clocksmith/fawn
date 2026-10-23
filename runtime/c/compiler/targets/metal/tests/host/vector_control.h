/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Sized ordinary host vectors, only for compiling emitted-operation controls. */
#ifndef DOE_METAL_VECTOR_CONTROL_H
#define DOE_METAL_VECTOR_CONTROL_H
#include <array>
#include <type_traits>
namespace metal {
template<class T, unsigned N> struct host_vector {
    std::array<T, N> lanes{};
    host_vector() = default;
    explicit host_vector(T value) { lanes.fill(value); }
    template<class U> explicit host_vector(const host_vector<U, N>& other) {
        for (unsigned i = 0u; i < N; ++i) lanes[i] = static_cast<T>(other[i]);
    }
    template<class... Values, std::enable_if_t<sizeof...(Values) == N, int> = 0>
    host_vector(Values... values) : lanes{static_cast<T>(values)...} {}
    T& operator[](unsigned i) { return lanes[i]; }
    const T& operator[](unsigned i) const { return lanes[i]; }
};
using uint2 = host_vector<uint, 2>;
using uint3 = host_vector<uint, 3>;
using uint4 = host_vector<uint, 4>;
using int2 = host_vector<int, 2>;
using int3 = host_vector<int, 3>;
using int4 = host_vector<int, 4>;
using float2 = host_vector<float, 2>;
using float3 = host_vector<float, 3>;
using float4 = host_vector<float, 4>;
using packed_uint3 = uint3;
using packed_int3 = int3;
using packed_float3 = float3;
}
#endif
