#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Execute emitted scalar/aggregate/guard operations, not HLSL or GPU semantics.

The mechanical shim removes HLSL resource/stage annotations, exposes word views,
and maps zero aggregate initialization to C++. Each storage view models a D3D12
buffer view: words past the bound count read zero, drop writes and are counted,
so a control can assert that clamped accesses never leave a nonempty binding.
The runtime size table is a plain host array the control fills like the runtime. The image guard uses asserting
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
switch_only = len(sys.argv) == 4 and sys.argv[2] == "--switch"
if switch_only:
    out.mkdir(parents=True, exist_ok=True)
    source = pathlib.Path(__file__).resolve().parents[2] / "tests" / "switch_source.wgsl"
    subprocess.run([sys.argv[3], "--shader-path", str(source), "--target", "hlsl",
                    "--out", str(out / "switch_control.hlsl")], check=True)
else:
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
uint min(uint a, uint b) { return a < b ? a : b; } /* HLSL uint min intrinsic. */
struct HostView {
    uint *words = nullptr;
    uint count = 0;
    unsigned beyond = 0;
    uint sink = 0;
    uint& operator[](uint index) {
        if (words != nullptr && index < count) return words[index];
        ++beyond;
        sink = 0;
        return sink;
    }
};
uint4 doe_storage_sizes[16] = {};
/* Binds like the runtime: one view of `words` words and the exact byte size row. */
#define BIND(view, row, data, words_) ((view).words = (data), (view).count = (words_), (view).beyond = 0, \
    doe_storage_sizes[(row) / 4u][(row) % 4u] = (words_) * 4u)
"""
scalar = r"""
int main() {
    uint words[9] = {};
    BIND(r0, 0, words, 7);
    words[6] = 4242;
    words[7] = 12345;
    words[8] = 67890;
    control();
    /* output[-1] clamps to the last element (6), read before output[6] is written. */
    assert(asint(words[0]) == 2 && asint(words[1]) == 12);
    assert(asint(words[2]) == INT_MIN && asint(words[3]) == INT_MIN);
    assert(asint(words[4]) == -7 && words[5] == 4242 && words[6] == 6);
    assert(words[7] == 12345 && words[8] == 67890 && r0.beyond == 0);
    std::fill(std::begin(words), std::end(words), 0);
    BIND(r0, 0, words, 3);
    control();
    /* Indices 3..6 and -1 clamp to element 2, whose last store is bump(5). */
    assert(words[0] == 2 && words[1] == 12 && words[2] == 6);
    for (unsigned i = 3; i < 9; ++i) assert(words[i] == 0);
    assert(r0.beyond == 0);
    std::fill(std::begin(words), std::end(words), 0x5a5a5a5au);
    BIND(r0, 0, words, 0);
    control();
    /* A zero-size binding clamps to element zero, past the empty view: reads
     * are zero, stores drop, and no bound word changes. */
    for (unsigned i = 0; i < 9; ++i) assert(words[i] == 0x5a5a5a5au);
    assert(r0.beyond != 0);
    std::puts("HLSL scalar host control: effects, continuing, wrap, clamped signed/upper indices inside the binding, zero-size binding.");
}
"""
aggregate = r"""
int main() {
    uint output[3] = {0, 1, 2345};
    uint data[8];
    std::fill(std::begin(data), std::end(data), 0x5a5a5a5au);
    data[0] = 2; data[1] = 5; data[5] = 7; data[6] = 9; data[7] = 13;
    BIND(r0, 0, output, 2);
    BIND(r1, 1, data, 8);
    aggregate_test();
    /* value.tail[-1] indexes a local copy, which keeps its zero-load guard. */
    assert(output[0] == 20 && output[1] == 0 && output[2] == 2345);
    assert(data[0] == 5 && data[1] == 5 && data[5] == 7 && data[6] == 8 && data[7] == 13);
    for (unsigned i = 2; i < 5; ++i) assert(data[i] == 0x5a5a5a5au);
    assert(r0.beyond == 0 && r1.beyond == 0);
    std::puts("HLSL aggregate host control: typed byte offsets, array copies, padding preservation.");
}
"""
effects = r"""
int main() {
    uint words[9] = {};
    uint indices[1] = {3};
    BIND(r0, 0, words, 8);
    BIND(r1, 1, indices, 1);
    words[5] = 100; words[7] = 77; words[8] = 887;
    effects();
    /* indices[-1] clamps to indices[0] == 3, so output[4] = output[3] = 77;
     * output[10] clamps to element 7, so the late tick (8) lands there and
     * output[6] = output[10] reads it back. Word 8 lies outside the binding. */
    assert(words[0] == 9 && words[1] == 102 && words[2] == 7);
    assert(words[3] == 77 && words[4] == 77 && words[5] == 106);
    assert(words[6] == 8 && words[7] == 8 && words[8] == 887);
    assert(r0.beyond == 0 && r1.beyond == 0);
    std::puts("HLSL effects host control: ordered arguments, index snapshots, independent clamped roots, clamped-store effects.");
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
    BIND(r1, 1, words, 7);
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
    BIND(r1, 1, output, 2);
    /* Byte sizes of the 16-byte-prefixed vec4u tail come from the table row. */
    const uint bytes[] = {0, 12, 16, 28, 32, 64, 47};
    const uint expected[] = {0, 0, 0, 0, 1, 3, 1};
    for (unsigned i = 0; i < 7; ++i) {
        doe_storage_sizes[0][0] = bytes[i];
        array_length();
        assert(output[0] == expected[i] && output[1] == 2 && output[2] == 0x12345678 && r1.beyond == 0);
    }
    std::puts("HLSL runtime array length: tail prefix, stride, partial elements and runtime size-table rows.");
}
"""
switch_main = r"""
int main() {
    uint words[16] = {};
    BIND(r0, 0, words, 16);
    for (uint lane = 0; lane < 8; ++lane) switch_entry(uint3{lane, 0, 0});
    const uint expected[16] = {11,11,23,8,99,99,99,99,1,1,1,1,1,1,1,1};
    for (unsigned i = 0; i < 16; ++i) assert(words[i] == expected[i]);
    std::puts("HLSL switch host control: exact16words, nested break/continue, selector once; not SDK/GPU qualification.");
}
"""
controls = (("control", scalar), ("aggregate", aggregate), ("effects", effects),
                   ("image_guard", image_guard), ("graphics", graphics), ("array_length", array_length),
                   ("matrices", pathlib.Path("tests/matrix_main.cpp").read_text()),
                   ("component_math", pathlib.Path("tests/component_math_main.cpp").read_text()))
if switch_only:
    controls = (("switch_control", switch_main),)
for name, main in controls:
    text = (out / (name + ".hlsl")).read_text()
    if name == "image_guard":
        text = re.sub(r"Texture2D<float> (r\d+) : register\([^;]+;", r"ImageTexture \1;", text)
        text = text.replace("Texture2D<float>", "ImageTexture")
        text = re.sub(r"\((int2|uint2|float4)\)0", r"\1{}", text)
    text = re.sub(r"(?:RW)?StructuredBuffer<uint> (r\d+) : register\([^;]+;",
                  lambda m: f"HostView {m[1]};", text)
    text = re.sub(r"cbuffer DoeStorageSizes : register\([^\n]+\n", "", text)
    assert "GetDimensions(count" not in text
    text = re.sub(r"\[numthreads\([^\n]+\]\n", "", text)
    if name == "matrices":
        # HLSL's aggregate zero cast initializes every numeric member. C++ {}
        # would call the deliberately poisoned matrix default constructor.
        text = re.sub(r"\((DoeT\d+|DoeContext)\)0", r"host_zero<\1>()", text)
    text = re.sub(r"\((DoeT\d+|DoeContext|uint3|uint4)\)0", r"\1{}", text)
    text = text.replace("precise ", "")
    text = text.replace("inout DoeContext", "DoeContext&")
    if name in ("graphics", "switch_control"):
        text = re.sub(r" : (?:TEXCOORD|SV_)[A-Za-z0-9_]+", "", text)
        text = re.sub(r"\b(?:nointerpolation|noperspective|linear|centroid|sample) ", "", text)
        text = re.sub(r"\((DoeStage[12])\)0", r"\1{}", text)
    if name == "matrices":
        text = re.sub(r"cbuffer DoeUniform\d+ : register\([^\n]+\n(.*?)\n};", r"\1", text, flags=re.S)
    if name == "switch_control":
        text = re.sub(r"\bvoid main\(", "void switch_entry(", text)
    path = out / (name + "_host.cpp")
    extra = image_types if name == "image_guard" else ""
    if name == "switch_control":
        extra = "#define DOE_HLSL_ENTRY 0\n"
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
