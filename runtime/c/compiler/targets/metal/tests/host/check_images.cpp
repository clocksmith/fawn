/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <cassert>
#include <climits>
#include <cstdio>
#define DOE_METAL_IMAGE_CONTROL
#include "images.metal"

struct Inputs { int x, y, z, layer, lod, sample; float coord, level; };
struct Run {
    alignas(16) int out[32] = {};
    image_state tex{}, dst{}, ms{}, dep{}, one{}, volume{}, cube{}, cubes{};
    void execute(Inputs p) {
        std::fill(std::begin(out), std::end(out), -91);
        out[20] = 0;
        out[21] = 0;
        uint sizes[31] = {};
        sizes[0] = sizeof(out);
        tex.tag = 100;
        dst.tag = 200;
        ms.tag = 300;
        dep.tag = 400;
        one.tag = 500;
        volume.tag = 600;
        cube.tag = 700;
        cubes.tag = 800;
        doe_entry0(reinterpret_cast<uchar*>(out), reinterpret_cast<const uchar*>(&p),
            texture2d_array<float>(tex), sampler{17u}, texture2d_array<float, access::read_write>(dst),
            texture2d_ms<uint>(ms), depth2d<float>(dep), texture1d<float>(one), texture3d<int>(volume),
            texturecube<float>(cube), texturecube_array<float>(cubes), sizes);
        assert(out[3] == 3 && out[4] == static_cast<int>(tex.layers) && out[5] == 4);
        assert(out[11] == 1 && out[12] == 500);
        assert(out[18] == 100 && out[19] == 0); // Per-iteration result reset.
        assert(out[20] == 8 && out[21] == 12341234); // Effects execute once, in source order, even for invalid LOD.
        assert(out[30] == -91 && out[31] == -91);
        assert(one.last_coords[0] == p.coord);
        assert(cube.last_coords[0] == p.coord && cube.last_coords[2] == p.coord);
        assert(cube.last_level == p.level && cube.last_sampler == 17u);
    }
};
int main() {
    const Inputs valid{1, 1, 1, 1, 1, 2, -3.5f, 1.25f};
    Run normal;
    normal.execute(valid);
    assert(normal.out[0] == 213 && normal.out[1] == 4 && normal.out[2] == 2);
    assert(normal.out[6] == 2303 && normal.out[7] == 503);
    assert(normal.out[8] == 213 && normal.dst.writes == 1u);
    assert(normal.dst.last_data[0] == 7.0f && normal.dst.last_data[3] == 10.0f);
    assert(normal.out[9] == 101 && normal.tex.last_layer == 0u); // Last read is loop level0.
    assert(normal.out[10] == 0 && normal.one.reads == 0u);
    assert(normal.out[13] == 707 && normal.out[14] == 2 && normal.out[15] == 700 && normal.out[16] == 801);
    assert(normal.out[17] == 4);
    assert(normal.tex.last_coords[0] == -3.5f && normal.tex.last_coords[1] == -3.5f);
    assert(normal.tex.last_level == 1.25f && normal.tex.last_sampler == 17u);
    assert(normal.out[22] == 216 && normal.out[23] == 216);
    for (int invalid : {-1, INT_MIN, INT_MAX, 4}) {
        Run lod;
        Inputs p = valid;
        p.lod = invalid;
        lod.execute(p);
        assert(lod.out[22] == 0);
        assert(lod.out[0] == 0 && lod.out[1] == 0 && lod.out[2] == 0 && lod.out[7] == 0);
        assert(lod.out[10] == 0 && lod.out[13] == 0 && lod.out[14] == 0 && lod.out[17] == 0);
        assert(lod.dep.reads == 0u && lod.volume.reads == 0u);
    }
    for (int invalid : {-1, INT_MIN, INT_MAX, 8}) {
        Run coordinate;
        Inputs p = valid;
        p.x = invalid;
        coordinate.execute(p);
        assert(coordinate.out[0] == 0 && coordinate.out[6] == 0 && coordinate.out[7] == 0);
        assert(coordinate.out[8] == 0 && coordinate.out[13] == 0 && coordinate.dst.writes == 0u);
    }
    for (int invalid : {-1, INT_MIN, INT_MAX, 3}) {
        Run layer;
        Inputs p = valid;
        p.layer = invalid;
        layer.execute(p);
        assert(layer.out[0] == 0 && layer.out[8] == 0 && layer.dst.writes == 0u);
        assert(layer.out[9] == (invalid < 0 ? 100 : 102));
        assert(layer.out[16] == (invalid < 0 ? 800 : 802));
    }
    for (int invalid : {-1, INT_MIN, INT_MAX, 4}) {
        Run sample;
        Inputs p = valid;
        p.sample = invalid;
        sample.execute(p);
        assert(sample.out[6] == 0 && sample.ms.reads == 0u);
    }
    Run base;
    Inputs p = valid;
    p.lod = 0;
    base.execute(p);
    assert(base.out[10] == 501 && base.one.reads == 1u);
    Run y;
    p = valid;
    p.y = 4;
    y.execute(p);
    assert(y.out[0] == 0 && y.out[8] == 0 && y.dst.writes == 0u);
    Run z;
    p = valid;
    p.z = 2;
    z.execute(p);
    assert(z.out[13] == 0);
    for (int invalid : {-1, INT_MIN, INT_MAX, 8}) {
        Run u;
        Inputs input{invalid, 1, 1, invalid, invalid, invalid, -4.0f, 99.5f};
        uint sizes[31] = {};
        sizes[0] = sizeof(u.out);
        doe_entry1(reinterpret_cast<uchar*>(u.out), reinterpret_cast<const uchar*>(&input),
            texture2d_array<float>(u.tex), sampler{23u}, texture2d_ms<uint>(u.ms), sizes);
        assert(u.out[0] == 0 && u.out[1] == 0 && u.out[2] == 0 && u.out[3] == 102);
        assert(u.tex.reads == 0u && u.ms.reads == 0u && u.tex.last_layer == 2u);
        assert(u.tex.last_coords[0] == -4.0f && u.tex.last_level == 99.5f && u.tex.last_sampler == 23u);
    }
    Run alias;
    alias.tex.tag = 917;
    uint sizes[31] = {};
    sizes[0] = sizeof(alias.out);
    doe_entry2(reinterpret_cast<uchar*>(alias.out), texture2d_array<float>(alias.tex), sizes);
    assert(alias.out[0] == 917 && alias.tex.reads == 1u);
    std::puts("Emitted Metal image operation controls passed: bounds, methods, layer clamps, raw sampling operands, identity and ordered effects.");
}
