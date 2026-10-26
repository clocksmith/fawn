# SPDX-License-Identifier: MIT
"""Bounded, runtime-bit differential against the pinned retained Zig scalar owner."""
from __future__ import annotations

import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]
OWNER = ROOT / "runtime/c/compiler/src/tsir"
ZIG_SOURCE = r'''
const std = @import("std");
const scalar = @import("scalar");
fn bits(x: f32) u32 { return @bitCast(x); }
pub fn main() !void {
    const args = try std.process.argsAlloc(std.heap.page_allocator);
    defer std.process.argsFree(std.heap.page_allocator, args);
    const pairs = std.mem.eql(u8, args[1], "pairs");
    for (args[2..]) |arg| {
        const u = try std.fmt.parseInt(u32, arg, 16);
        const x: f32 = @bitCast(u);
        if (pairs) {
            for (args[2..]) |other| {
                const v = try std.fmt.parseInt(u32, other, 16);
                const y: f32 = @bitCast(v);
                std.debug.print("{x:0>8} {x:0>8} {x:0>8} {x:0>8}\n", .{
                    u, v, bits(scalar.combineF32(.min,x,y)), bits(scalar.combineF32(.max,x,y))});
            }
        } else {
            std.debug.print("{x:0>8} {x:0>8} {x:0>8} {x:0>8} {x:0>8} {x:0>8} {x:0>8} {x:0>8}\n", .{
                u, bits(std.math.exp(x)), bits(std.math.tanh(x)), bits(std.math.sqrt(x)),
                bits(scalar.combineF32(.min,x,1)), bits(scalar.combineF32(.max,x,1)),
                bits(scalar.combineF32(.min,1,x)), bits(scalar.combineF32(.max,1,x))});
        }
    }
}
'''
C_SOURCE = r'''
#include "tsir_transcendental.h"
#include "tsir_scalar.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static uint32_t bits(float f) { uint32_t u; memcpy(&u,&f,4); return u; }
static float value(uint32_t u) { float f; memcpy(&f,&u,4); return f; }
static float combine(TsirReductionOp op,float a,float b) {
    float out;
    if(tsir_combine_f32(op,a,b,&out)!=TSIR_OK) abort();
    return out;
}
int main(int argc,char **argv) {
    int pairs = argc > 1 && strcmp(argv[1],"pairs")==0;
    for(int i=2;i<argc;i++) {
        uint32_t u=(uint32_t)strtoul(argv[i],NULL,16);
        float x=value(u);
        if(pairs) {
            for(int j=2;j<argc;j++) {
                uint32_t v=(uint32_t)strtoul(argv[j],NULL,16);
                float y=value(v);
                printf("%08x %08x %08x %08x\n",u,v,
                    bits(combine(TSIR_REDUCTION_MIN,x,y)),bits(combine(TSIR_REDUCTION_MAX,x,y)));
            }
        } else printf("%08x %08x %08x %08x %08x %08x %08x %08x\n",u,
            bits(tsir_exp_f32(x)),bits(tsir_tanh_f32(x)),bits(tsir_sqrt_f32(x)),
            bits(combine(TSIR_REDUCTION_MIN,x,1)),bits(combine(TSIR_REDUCTION_MAX,x,1)),
            bits(combine(TSIR_REDUCTION_MIN,1,x)),bits(combine(TSIR_REDUCTION_MAX,1,x)));
    }
    return 0;
}
'''


def invoke(command: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, env=env, check=True, capture_output=True,
                          text=True, timeout=90)


def main() -> None:
    env = dict(os.environ)
    zig = env.get("ZIG", "zig")
    compiler = env.get("CC", "gcc")
    required = json.loads((ROOT / "config/toolchains.json").read_text())["toolchains"]["zig"]["version"]
    assert invoke([zig, "version"], env).stdout.strip() == required
    with tempfile.TemporaryDirectory(prefix="tsir-numeric-") as directory:
        work = Path(directory)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(work / "global-cache")
        (work / "probe.zig").write_text(ZIG_SOURCE)
        (work / "probe.c").write_text(C_SOURCE)
        c_binary, zig_binary = work / "probe-c", work / "probe-zig"
        invoke([compiler, "-std=c11", "-O2", "-fno-fast-math", "-ffp-contract=off",
                "-frounding-math", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                "-I" + str(OWNER), str(work / "probe.c"),
                *(str(OWNER / name) for name in ("tsir_transcendental.c", "tsir_scalar.c", "tsir_schema.c")),
                "-lm", "-o", str(c_binary)], env)
        invoke([zig, "build-exe", "-O", "ReleaseSafe", "--cache-dir", str(work / "local-cache"),
                "--dep", "scalar", "-Mroot=" + str(work / "probe.zig"),
                "-Mscalar=" + str(ROOT / "runtime/zig/src/compiler/tsir/reference_scalar.zig"),
                "-lc", "-femit-bin=" + str(zig_binary)], env)
        samples = [0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
                   0x3f000000, 0x3f800000, 0xbf800000, 0x40000000,
                   0x7f800000, 0xff800000, 0x7fc00000, 0x7fc01234,
                   0xffc01234, 0x7f801234, 0xff801456]
        random_source = random.Random(2056)
        samples += [struct.unpack("<I", struct.pack("<f", random_source.uniform(-15, 15)))[0]
                    for _ in range(400)]
        # Adjacent representable inputs at each pinned algorithm boundary.
        for boundary in (0x39000000, 0x3eb17218, 0x3f851592, 0x42aeac50,
                         0x42b17218, 0x42cff1b5, 0x3f0c9f54, 0x3e82c578, 0x41200000):
            for delta in (-1, 0, 1):
                samples += [boundary + delta, (boundary + delta) | 0x80000000]
        pairs = [0, 0x80000000, 0x3f800000, 0x7fc01234, 0xffc05678, 0x7f801234, 0xff801456]
        comparisons = zero_ties = 0
        for mode, inputs in (("values", samples), ("pairs", pairs)):
            arguments = [mode, *(f"{x:08x}" for x in inputs)]
            actual = invoke([str(c_binary), *arguments], env).stdout.splitlines()
            expected = invoke([str(zig_binary), *arguments], env).stderr.splitlines()
            assert len(actual) == len(expected) == (len(inputs) ** 2 if mode == "pairs" else len(inputs))
            for c_line, zig_line in zip(actual, expected, strict=True):
                c_fields, zig_fields = c_line.split(), zig_line.split()
                first_result = 2 if mode == "pairs" else 1
                assert c_fields[:first_result] == zig_fields[:first_result]
                for c, z in zip(c_fields[first_result:], zig_fields[first_result:], strict=True):
                    # LLVM's minimum-number intrinsic does not promise the sign
                    # of equal zero operands: runtime inline arrangements of
                    # the same retained Zig function produce either zero sign.
                    zero_pair = mode == "pairs" and all(int(x,16) & 0x7fffffff == 0 for x in c_fields[:2])
                    if zero_pair:
                        assert int(c,16) & 0x7fffffff == int(z,16) & 0x7fffffff == 0
                        zero_ties += 1
                    else:
                        assert c == z, (mode, c_fields[:first_result], c, z)
                    comparisons += 1
        print(f"TSIR numeric parity: {len(samples)} runtime inputs, {len(pairs)**2} operand pairs, "
              f"{comparisons} comparisons passed ({zero_ties} equal-zero ties checked numerically)")


if __name__ == "__main__":
    main()
