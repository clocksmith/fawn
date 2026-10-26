#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pinned Zig {d} comparison for shared canonical/epsilon decimal formatting."""
from __future__ import annotations

import math
import os
from pathlib import Path
import random
import shutil
import signal
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[6]
TSIR = ROOT / "runtime/c/compiler/src/tsir"


def run(args: list[str]) -> bytes:
    child = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.communicate()
        raise
    if child.returncode:
        raise AssertionError((args, child.returncode, stderr.decode(errors="replace")))
    return stdout


def main() -> int:
    zig = shutil.which(os.environ.get("ZIG", "zig"))
    if not zig or run([zig, "version"]).strip() != b"0.15.2":
        raise SystemExit("pinned Zig 0.15.2 is required")
    bits = [0, 1 << 63, 1, (1 << 63) | 1, 0x000FFFFFFFFFFFFF,
            0x0010000000000000, 0x7FEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF]
    for value in [1e-7, 1e-6, 1e20, 1e21, 1e100, 1e-100, 0.1, 1.2345678901234567]:
        for adjacent in [math.nextafter(value, 0.0), value, math.nextafter(value, math.inf)]:
            for signed in [adjacent, -adjacent]:
                bits.append(int.from_bytes(struct.pack(">d", signed), "big"))
    generator = random.Random(20261026)
    while len(bits) < 4152:
        candidate = generator.getrandbits(64)
        if math.isfinite(struct.unpack(">d", candidate.to_bytes(8, "big"))[0]):
            bits.append(candidate)
    arguments = [f"{value:016x}" for value in bits]
    with tempfile.TemporaryDirectory(prefix="tsir-f64-") as directory:
        temp = Path(directory)
        c_binary = temp / "format-c"
        run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra",
             "-Wpedantic", "-Werror", str(TSIR / "tests/test_f64_format.c"),
             *[str(TSIR / ("tsir_" + name + ".c")) for name in
               ("format", "digest", "sha256", "schema", "emit", "kernel_body")],
             "-lm", "-o", str(c_binary)])
        source = temp / "format.zig"
        source.write_text('''const std = @import("std");
pub fn main() !void {
    const allocator = std.heap.page_allocator;
    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);
    const writer = std.fs.File.stdout().deprecatedWriter();
    for (args[1..]) |arg| {
        const bits = try std.fmt.parseInt(u64, arg, 16);
        const value: f64 = @bitCast(bits);
        try writer.print("{d}\\n", .{value});
    }
}
''')
        zig_binary = temp / "format-zig"
        run([zig, "build-exe", str(source), "--cache-dir", str(temp / "cache"),
             "--global-cache-dir", str(temp / "global"), "-femit-bin=" + str(zig_binary)])
        candidate = run([str(c_binary), *arguments]).splitlines()
        reference = run([str(zig_binary), *arguments]).splitlines()
        assert len(candidate) == len(reference) == len(bits)
        for identity, actual, expected in zip(arguments, candidate, reference):
            assert actual == expected, (identity, actual, expected)
    print(f"TSIR f64 formatting: {len(bits)} exact Zig decimal cases; canonical identity and four epsilon emitters, caller errno/fenv and first-error controls passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
