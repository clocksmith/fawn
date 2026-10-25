#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Execute emitted scalar/aggregate/guard operations, not HLSL or GPU semantics.

The mechanical shim removes HLSL resource/stage annotations, exposes word views,
and maps zero aggregate initialization to C++. The image guard uses asserting
query/load stubs and a sentinel result, not an image or sampling model.
It does not simulate WGSL, threads,
wave intrinsics, descriptor lifetime, a driver, or target compiler acceptance.
"""
import pathlib
import re
import signal
import subprocess
import sys

out = pathlib.Path(sys.argv[1])
subprocess.run([str(out / "test_hlsl"), str(out / "control.hlsl"),
                str(out / "aggregate.hlsl"), str(out / "effects.hlsl"), str(out / "image_guard.hlsl"),
                str(out / "graphics.hlsl"), str(out / "array_length.hlsl"), str(out / "matrices.hlsl"), str(out / "component_math.hlsl")], check=True)
prelude = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
using uint = std::uint32_t;
using uint3 = std::array<uint, 3>;
using uint4 = std::array<uint, 4>;
using std::abs;
using std::clamp;
using std::isnan;
uint asuint(uint v) { return v; }
uint asuint(int v) { uint r; std::memcpy(&r, &v, 4); return r; }
uint asuint(float v) { uint r; std::memcpy(&r, &v, 4); return r; }
int asint(uint v) { int r; std::memcpy(&r, &v, 4); return r; }
float asfloat(uint v) { float r; std::memcpy(&r, &v, 4); return r; }
"""
scalar = r"""
int main() {
    uint words[9] = {};
    r0 = words;
    r0_count = 7;
    words[7] = 12345;
    words[8] = 67890;
    control();
    assert(asint(words[0]) == 2 && asint(words[1]) == 12);
    assert(asint(words[2]) == INT_MIN && asint(words[3]) == INT_MIN);
    assert(asint(words[4]) == -7 && words[5] == 0 && words[6] == 6);
    assert(words[7] == 12345 && words[8] == 67890);
    std::fill(std::begin(words), std::end(words), 0);
    r0_count = 3;
    control();
    assert(words[0] == 2 && words[1] == 12 && asint(words[2]) == INT_MIN);
    for (unsigned i = 3; i < 9; ++i) assert(words[i] == 0);
    r0_stride = 8;
    control();
    assert(words[0] == 2 && words[1] == 12);
    r0_stride = 4;
    r0_count = 0xffffffffu;
    control();
    assert(words[0] == 2 && words[1] == 12);
    std::puts("HLSL scalar host control: effects, continuing, wrap, signed bounds, view extent.");
}
"""
aggregate = r"""
int main() {
    uint output[3] = {0, 1, 2345};
    uint data[8];
    std::fill(std::begin(data), std::end(data), 0x5a5a5a5au);
    data[0] = 2; data[1] = 5; data[5] = 7; data[6] = 9; data[7] = 13;
    r0 = output; r0_count = 2;
    r1 = data; r1_count = 8;
    aggregate_test();
    assert(output[0] == 20 && output[1] == 0 && output[2] == 2345);
    assert(data[0] == 5 && data[1] == 5 && data[5] == 7 && data[6] == 8 && data[7] == 13);
    for (unsigned i = 2; i < 5; ++i) assert(data[i] == 0x5a5a5a5au);
    std::puts("HLSL aggregate host control: typed byte offsets, array copies, padding preservation.");
}
"""
effects = r"""
int main() {
    uint words[9] = {};
    uint indices[1] = {3};
    r0 = words; r0_count = 8;
    r1 = indices; r1_count = 1;
    words[5] = 100; words[7] = 77; words[8] = 887;
    effects();
    assert(words[0] == 9 && words[1] == 102 && words[2] == 7);
    assert(words[3] == 77 && words[4] == 8 && words[5] == 106);
    assert(words[6] == 0 && words[7] == 77 && words[8] == 887);
    std::puts("HLSL effects host control: ordered arguments, index snapshots, independent OOB roots, discarded-store effects.");
}
"""
image_types = r"""
using float4 = std::array<float, 4>;
struct int2 {
    int values[2];
    int2() = default;
    int2(int x, int y) : values{x, y} {}
    int& operator[](unsigned i) { return values[i]; }
    int operator[](unsigned i) const { return values[i]; }
};
struct uint2 {
    uint values[2];
    uint2() = default;
    uint2(uint x, uint y) : values{x, y} {}
    uint& operator[](unsigned i) { return values[i]; }
    uint operator[](unsigned i) const { return values[i]; }
};
struct int3 {
    int x, y, z;
    int3(int2 xy, int level) : x(xy[0]), y(xy[1]), z(level) {}
};
unsigned image_load_calls;
unsigned image_query_calls;
struct ImageTexture {
    void GetDimensions(uint mip, uint& x, uint& y, uint& levels) {
        assert(mip < 3);
        ++image_query_calls;
        x = 4u >> mip; y = 4u >> mip; levels = 3;
    }
    float Load(int3 address) {
        assert(address.z >= 0 && address.z < 3);
        assert(address.x >= 0 && uint(address.x) < (4u >> address.z));
        assert(address.y >= 0 && uint(address.y) < (4u >> address.z));
        ++image_load_calls;
        return 42.0f;
    }
};
"""
image_guard = r"""
int main() {
    uint words[8] = {};
    words[7] = 0x12345678;
    r1 = words; r1_count = 7;
    image_guard();
    assert(asfloat(words[0]) == 42.0f);
    for (unsigned i = 1; i <= 5; ++i) assert(words[i] == 0);
    assert(asfloat(words[6]) == 2.0f && words[7] == 0x12345678);
    assert(image_load_calls == 1 && image_query_calls == 11);
    std::puts("HLSL image guard host control: negative/upper LOD query protection, independent coordinate checks, zero reads.");
}
"""
graphics = r"""
int main() {
    DoeStage1 input{};
    input.field0 = 17;
    input.field1 = 9;
    input.field2 = true;
    input.field3 = 5;
    DoeStage2 output = graphics(input);
    assert(output.field4 == 1709 && output.field5 == 5);
    input.field2 = false;
    output = graphics(input);
    assert(output.field4 == 1710 && output.field5 == 0);
    input.field0 = 0xffffffffu;
    input.field1 = 31;
    input.field2 = true;
    output = graphics(input);
    assert(output.field4 == (0xffffffffu * 100u + 31u) && output.field5 == 5);
    std::puts("HLSL graphics host control: typed interface mapping, formal identity, helper calls and return leaves.");
}
"""
array_length = r"""
int main() {
    uint output[3] = {0, 0, 0x12345678};
    r0 = nullptr; r1 = output; r1_count = 2;
    const uint words[] = {0, 3, 4, 7, 8, 16, 0xffffffffu};
    const uint expected[] = {0, 0, 0, 0, 1, 3, 0};
    for (unsigned i = 0; i < 7; ++i) {
        r0_count = words[i];
        array_length();
        assert(output[0] == expected[i] && output[1] == 2 && output[2] == 0x12345678);
    }
    r0_count = 16; r0_stride = 8;
    array_length();
    assert(output[0] == 0);
    std::puts("HLSL runtime array length: tail prefix, stride, partial elements and bound-view metadata.");
}
"""
for name, main in (("control", scalar), ("aggregate", aggregate), ("effects", effects),
                   ("image_guard", image_guard), ("graphics", graphics), ("array_length", array_length),
                   ("matrices", pathlib.Path("tests/matrix_main.cpp").read_text()),
                   ("component_math", pathlib.Path("tests/component_math_main.cpp").read_text())):
    text = (out / (name + ".hlsl")).read_text()
    if name == "image_guard":
        text = re.sub(r"Texture2D<float> (r\d+) : register\([^;]+;", r"ImageTexture \1;", text)
        text = text.replace("Texture2D<float>", "ImageTexture")
        text = re.sub(r"\((int2|uint2|float4)\)0", r"\1{}", text)
    text = re.sub(r"(?:RW)?StructuredBuffer<uint> (r\d+) : register\([^;]+;",
                  lambda m: f"uint* {m[1]}; uint {m[1]}_count; uint {m[1]}_stride = 4;", text)
    text = re.sub(r"(r\d+)\.GetDimensions\(count, stride\);",
                  lambda m: f"count = {m[1]}_count; stride = {m[1]}_stride;", text)
    text = re.sub(r"\[numthreads\([^\n]+\]\n", "", text)
    if name == "matrices":
        # HLSL's aggregate zero cast initializes every numeric member. C++ {}
        # would call the deliberately poisoned matrix default constructor.
        text = re.sub(r"\((DoeT\d+|DoeContext)\)0", r"host_zero<\1>()", text)
    text = re.sub(r"\((DoeT\d+|DoeContext|uint3|uint4)\)0", r"\1{}", text)
    text = text.replace("precise ", "")
    text = text.replace("inout DoeContext", "DoeContext&")
    if name == "graphics":
        text = re.sub(r" : (?:TEXCOORD|SV_)[A-Za-z0-9_]+", "", text)
        text = re.sub(r"\b(?:nointerpolation|noperspective|linear|centroid|sample) ", "", text)
        text = re.sub(r"\((DoeStage[12])\)0", r"\1{}", text)
    if name == "matrices":
        text = re.sub(r"cbuffer DoeUniform\d+ : register\([^\n]+\n(.*?)\n};", r"\1", text, flags=re.S)
    path = out / (name + "_host.cpp")
    extra = image_types if name == "image_guard" else ""
    if name == "matrices":
        extra = '#include "matrix_control.h"\n'
    if name == "component_math":
        # C++ round differs from HLSL round. Map only this documented intrinsic;
        # WGSL/IR interpretation is still the real emitted operation sequence.
        text = re.sub(r"\bround\(", "std::nearbyint(", text)
        extra = '#include "../../metal/tests/host/vector_control.h"\nusing metal::float2;\n'
        extra += "float rsqrt(float x) { return 1.0f / std::sqrt(x); }\n"
        extra += "float lerp(float x,float y,float a) { return x*(1.0f-a)+y*a; }\n"
        extra += "float2 lerp(float2 x,float2 y,float2 a) { return float2(lerp(x[0],y[0],a[0]),lerp(x[1],y[1],a[1])); }\n"
        extra += "float degrees(float x) { return x * 57.29577951308232f; }\n"
        extra += "float radians(float x) { return x * 0.017453292519943295f; }\n"
    path.write_text(prelude + extra + text + main)
    executable = out / (name + "_host")
    subprocess.run(["g++", "-std=c++17", "-O2", "-fno-fast-math", "-ffp-contract=off",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=undefined", "-Itests", str(path),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

    if name == "matrices":
        # Same-shaped noncommuting matrices still type-check if reversed. The
        # literal expected outputs must reject that wrong-but-well-typed edit.
        original = path.read_text()
        match = list(re.finditer(r"mul\((v\d+), (v\d+)\)", original))[-1]
        wrong = original[:match.start()] + f"mul({match[2]}, {match[1]})" + original[match.end():]
        wrong_path = out / "matrices_wrong_order.cpp"
        wrong_path.write_text(wrong)
        wrong_binary = out / "matrices_wrong_order"
        subprocess.run(["g++", "-std=c++17", "-O2", "-fno-fast-math", "-ffp-contract=off",
                        "-fsanitize=undefined", "-Itests", str(wrong_path), "-o", str(wrong_binary)], check=True)
        result = subprocess.run([str(wrong_binary)], capture_output=True, text=True)
        assert result.returncode == -signal.SIGABRT
        assert "asfloat(output[i]) == expected[i]" in result.stderr
        print("HLSL matrix wrong-order control rejected the same-shaped noncommuting product.")
