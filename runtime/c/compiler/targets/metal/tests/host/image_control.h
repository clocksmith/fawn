/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Operation spies, not texture storage, filtering, MSL validation or a GPU.
 * Invalid integer arguments assert: passing proves the emitted guards ran. */
#ifndef DOE_METAL_IMAGE_CONTROL_H
#define DOE_METAL_IMAGE_CONTROL_H
#include <cassert>
namespace metal {
enum class access { sample, read, write, read_write };
struct sampler { unsigned identity; };
struct level { float value; explicit level(float v) : value(v) {} };
struct image_state {
    uint width = 8u, height = 4u, depth = 4u, layers = 3u, levels = 3u, samples = 4u;
    int tag = 100;
    unsigned reads = 0u, writes = 0u, samples_called = 0u, queries = 0u;
    uint last_layer = 0u, last_lod = 0u, last_sample = 0u;
    float last_coords[3] = {}, last_level = 0.0f;
    unsigned last_sampler = 0u;
    float4 last_data{};
};
template<class T, access A> struct image_base {
    image_state *state;
    explicit image_base(image_state& s) : state(&s) {}
    uint dimension(uint size, uint lod) const {
        assert(lod < state->levels);
        ++state->queries;
        return std::max(1u, size >> lod);
    }
    uint get_width(uint lod = 0u) const { return dimension(state->width, lod); }
    uint get_height(uint lod = 0u) const { return dimension(state->height, lod); }
    uint get_depth(uint lod = 0u) const { return dimension(state->depth, lod); }
    uint get_num_mip_levels() const { ++state->queries; return state->levels; }
    uint get_array_size() const { ++state->queries; return state->layers; }
    uint get_num_samples() const { ++state->queries; return state->samples; }
    host_vector<T, 4> read_value(uint x, uint y, uint z, uint layer, uint lod, uint sample) const {
        assert(lod < state->levels && layer < state->layers && sample < state->samples);
        assert(x < std::max(1u, state->width >> lod));
        assert(y < std::max(1u, state->height >> lod));
        assert(z < std::max(1u, state->depth >> lod));
        ++state->reads;
        state->last_layer = layer;
        state->last_lod = lod;
        state->last_sample = sample;
        T v = static_cast<T>(state->tag + static_cast<int>(x + 2u*y + 4u*z + 10u*layer + 100u*lod + 1000u*sample));
        return host_vector<T, 4>(v, v + T(1), v + T(2), v + T(3));
    }
    host_vector<T, 4> sample_value(sampler s, float x, float y, float z, uint layer, float lod) const {
        assert(layer < state->layers);
        ++state->samples_called;
        state->last_layer = layer;
        state->last_coords[0] = x;
        state->last_coords[1] = y;
        state->last_coords[2] = z;
        state->last_level = lod;
        state->last_sampler = s.identity;
        return host_vector<T, 4>(static_cast<T>(state->tag + static_cast<int>(layer)));
    }
};
template<class T, access A = access::sample> struct texture1d : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    uint get_width(uint lod = 0u) const { assert(lod == 0u); return base::get_width(); }
    uint get_num_mip_levels() const { assert(false && "MSL 1D count must not drive WGSL bounds"); return 0u; }
    host_vector<T, 4> read(uint x, uint lod = 0u) const {
        assert(lod == 0u);
        return base::read_value(x, 0u, 0u, 0u, 0u, 0u);
    }
    host_vector<T, 4> sample(sampler s, float x) const { return base::sample_value(s, x, 0, 0, 0u, 0); }
};
template<class T, access A = access::sample> struct texture2d_array : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    host_vector<T, 4> read(uint2 c, uint layer, uint lod = 0u) const {
        return base::read_value(c[0], c[1], 0u, layer, lod, 0u);
    }
    void write(host_vector<T, 4> value, uint2 c, uint layer, uint lod = 0u) const {
        static_assert(A == access::write || A == access::read_write);
        assert(lod == 0u && layer < this->state->layers && c[0] < this->state->width && c[1] < this->state->height);
        ++this->state->writes;
        this->state->last_data = float4(value);
    }
    host_vector<T, 4> sample(sampler s, float2 c, uint layer, level l) const {
        static_assert(A == access::sample);
        return base::sample_value(s, c[0], c[1], 0.0f, layer, l.value);
    }
};
template<class T, access A = access::read> struct texture2d_ms : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    uint get_width() const { return base::get_width(); }
    uint get_height() const { return base::get_height(); }
    host_vector<T, 4> read(uint2 c, uint sample) const { return base::read_value(c[0], c[1], 0u, 0u, 0u, sample); }
};
template<class T, access A = access::sample> struct depth2d : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    T read(uint2 c, uint lod = 0u) const { return base::read_value(c[0], c[1], 0u, 0u, lod, 0u)[0]; }
};
template<class T, access A = access::sample> struct texture3d : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    host_vector<T, 4> read(uint3 c, uint lod = 0u) const { return base::read_value(c[0], c[1], c[2], 0u, lod, 0u); }
};
template<class T, access A = access::sample> struct texturecube : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    host_vector<T, 4> sample(sampler s, float3 c, level l) const { return base::sample_value(s, c[0], c[1], c[2], 0u, l.value); }
};
template<class T, access A = access::sample> struct texturecube_array : image_base<T, A> {
    using base = image_base<T, A>;
    using base::base;
    host_vector<T, 4> sample(sampler s, float3 c, uint layer, level l) const { return base::sample_value(s, c[0], c[1], c[2], layer, l.value); }
};
}
#endif
