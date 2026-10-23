/* SPDX-License-Identifier: MIT
 * Emitted-operation host control, not a Metal compiler or floating-point oracle. */
#ifndef DOE_METAL_MATRIX_CONTROL_H
#define DOE_METAL_MATRIX_CONTROL_H
namespace metal {
template<unsigned C, unsigned R> struct host_matrix {
    host_vector<float, R> columns[C];
    host_matrix() {
        for (auto &column : columns)
            for (unsigned r = 0; r < R; ++r) column[r] = NAN;
    }
    explicit host_matrix(float diagonal) {
        for (unsigned c = 0; c < C; ++c)
            for (unsigned r = 0; r < R; ++r) columns[c][r] = c == r ? diagonal : 0.0f;
    }
    template<class... V, std::enable_if_t<sizeof...(V) == C, int> = 0>
    host_matrix(V... values) : columns{values...} {}
    template<class... V, std::enable_if_t<sizeof...(V) == C * R, int> = 0>
    host_matrix(V... values) {
        float v[] = {float(values)...};
        for (unsigned c = 0; c < C; ++c)
            for (unsigned r = 0; r < R; ++r) columns[c][r] = v[c * R + r];
    }
    host_vector<float, R>& operator[](unsigned c) { return columns[c]; }
    const host_vector<float, R>& operator[](unsigned c) const { return columns[c]; }
};
template<unsigned C, unsigned R>
host_matrix<C, R> operator+(const host_matrix<C, R>& a, const host_matrix<C, R>& b) {
    host_matrix<C, R> o;
    for (unsigned c = 0; c < C; ++c)
        for (unsigned r = 0; r < R; ++r) o[c][r] = a[c][r] + b[c][r];
    return o;
}
template<unsigned C, unsigned R>
host_matrix<C, R> operator-(const host_matrix<C, R>& a, const host_matrix<C, R>& b) {
    host_matrix<C, R> o;
    for (unsigned c = 0; c < C; ++c)
        for (unsigned r = 0; r < R; ++r) o[c][r] = a[c][r] - b[c][r];
    return o;
}
template<unsigned C, unsigned R>
host_matrix<C, R> operator*(const host_matrix<C, R>& a, float b) {
    host_matrix<C, R> o;
    for (unsigned c = 0; c < C; ++c)
        for (unsigned r = 0; r < R; ++r) o[c][r] = a[c][r] * b;
    return o;
}
template<unsigned C, unsigned R>
host_vector<float, R> operator*(const host_matrix<C, R>& a, const host_vector<float, C>& b) {
    host_vector<float, R> o;
    for (unsigned r = 0; r < R; ++r)
        for (unsigned c = 0; c < C; ++c) o[r] += a[c][r] * b[c];
    return o;
}
template<unsigned C, unsigned R>
host_vector<float, C> operator*(const host_vector<float, R>& a, const host_matrix<C, R>& b) {
    host_vector<float, C> o;
    for (unsigned c = 0; c < C; ++c)
        for (unsigned r = 0; r < R; ++r) o[c] += a[r] * b[c][r];
    return o;
}
template<unsigned C, unsigned R, unsigned K>
host_matrix<K, R> operator*(const host_matrix<C, R>& a, const host_matrix<K, C>& b) {
    host_matrix<K, R> o(0.0f);
    for (unsigned c = 0; c < K; ++c)
        for (unsigned r = 0; r < R; ++r)
            for (unsigned k = 0; k < C; ++k) o[c][r] += a[k][r] * b[c][k];
    return o;
}
template<unsigned C, unsigned R>
host_matrix<R, C> transpose(const host_matrix<C, R>& a) {
    host_matrix<R, C> o;
    for (unsigned c = 0; c < C; ++c)
        for (unsigned r = 0; r < R; ++r) o[r][c] = a[c][r];
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
}
#endif
