/* SPDX-License-Identifier: MIT
 * HLSL row-major value model for emitted-operation controls only. Not DXC. */
#ifndef DOE_HLSL_TEST_MATRIX_CONTROL_H
#define DOE_HLSL_TEST_MATRIX_CONTROL_H
#include "../../metal/tests/host/vector_control.h"
template<class T> T host_zero() {
    static_assert(std::is_trivially_copyable_v<T>);
    T value;
    std::memset(&value, 0, sizeof(value));
    return value;
}
using metal::float2;
using metal::float3;
using metal::float4;
template<unsigned R, unsigned C> struct host_matrix {
    metal::host_vector<float, C> rows[R];
    host_matrix() {
        for (auto &row : rows) for (unsigned c = 0; c < C; ++c) row[c] = NAN;
    }
    explicit host_matrix(float value) {
        for (auto &row : rows) for (unsigned c = 0; c < C; ++c) row[c] = value;
    }
    template<class... V, std::enable_if_t<sizeof...(V) == R, int> = 0>
    host_matrix(V... values) : rows{values...} {}
    template<class... V, std::enable_if_t<sizeof...(V) == R * C, int> = 0>
    host_matrix(V... values) {
        float v[] = {float(values)...};
        for (unsigned r = 0; r < R; ++r)
            for (unsigned c = 0; c < C; ++c) rows[r][c] = v[r * C + c];
    }
    metal::host_vector<float, C>& operator[](unsigned r) { return rows[r]; }
    const metal::host_vector<float, C>& operator[](unsigned r) const { return rows[r]; }
};
template<unsigned R, unsigned C>
host_matrix<R, C> operator+(const host_matrix<R, C>& a, const host_matrix<R, C>& b) {
    host_matrix<R, C> o;
    for (unsigned r = 0; r < R; ++r)
        for (unsigned c = 0; c < C; ++c) o[r][c] = a[r][c] + b[r][c];
    return o;
}
template<unsigned R, unsigned C>
host_matrix<R, C> operator-(const host_matrix<R, C>& a, const host_matrix<R, C>& b) {
    host_matrix<R, C> o;
    for (unsigned r = 0; r < R; ++r)
        for (unsigned c = 0; c < C; ++c) o[r][c] = a[r][c] - b[r][c];
    return o;
}
template<unsigned R, unsigned C>
host_matrix<R, C> operator*(const host_matrix<R, C>& a, float b) {
    host_matrix<R, C> o;
    for (unsigned r = 0; r < R; ++r)
        for (unsigned c = 0; c < C; ++c) o[r][c] = a[r][c] * b;
    return o;
}
template<unsigned R, unsigned C>
metal::host_vector<float, R> mul(const host_matrix<R, C>& a, const metal::host_vector<float, C>& b) {
    metal::host_vector<float, R> o;
    for (unsigned r = 0; r < R; ++r)
        for (unsigned c = 0; c < C; ++c) o[r] += a[r][c] * b[c];
    return o;
}
template<unsigned R, unsigned C>
metal::host_vector<float, C> mul(const metal::host_vector<float, R>& a, const host_matrix<R, C>& b) {
    metal::host_vector<float, C> o;
    for (unsigned c = 0; c < C; ++c)
        for (unsigned r = 0; r < R; ++r) o[c] += a[r] * b[r][c];
    return o;
}
template<unsigned R, unsigned C, unsigned K>
host_matrix<R, K> mul(const host_matrix<R, C>& a, const host_matrix<C, K>& b) {
    host_matrix<R, K> o(0.0f);
    for (unsigned r = 0; r < R; ++r)
        for (unsigned k = 0; k < K; ++k)
            for (unsigned c = 0; c < C; ++c) o[r][k] += a[r][c] * b[c][k];
    return o;
}
template<unsigned R, unsigned C>
host_matrix<C, R> transpose(const host_matrix<R, C>& a) {
    host_matrix<C, R> o;
    for (unsigned r = 0; r < R; ++r)
        for (unsigned c = 0; c < C; ++c) o[c][r] = a[r][c];
    return o;
}
inline float determinant(const host_matrix<2, 2>& a) {
    return a[0][0] * a[1][1] - a[1][0] * a[0][1];
}
using float2x2 = host_matrix<2, 2>;
using float2x3 = host_matrix<2, 3>;
using float2x4 = host_matrix<2, 4>;
using float3x2 = host_matrix<3, 2>;
using float3x3 = host_matrix<3, 3>;
using float3x4 = host_matrix<3, 4>;
using float4x2 = host_matrix<4, 2>;
using float4x3 = host_matrix<4, 3>;
using float4x4 = host_matrix<4, 4>;
#endif
